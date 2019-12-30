---
title:      "[笔记] The Chubby lock service for loosely-coupled distributed systems"
date:       2020-08-28 11:59:16
tags:
    - 笔记
    - Paxos
    - 分布式锁
    - Google
---

> 原文：[The Chubby lock service for loosely-coupled distributed systems](https://www.usenix.org/event/osdi06/tech/full_papers/burrows/burrows_html/)

## TL;DR

本文idea：基于普通商用机构建出强一致的、高可用的分布式锁服务。

论证过程：
1. 如何实现强一致：Paxos。
1. 实现为库还是外部服务：库是侵入式的，应用接入困难；外部服务能把应用与Chubby解耦开。
1. 提供什么样的接口：类UNIX文件系统，建议式锁，尽可能降低应用接入和开发者的使用难度。
1. 针对读远多于写的使用场景，如何降低server负载：尽可能地cache，为了降低开发者的理解难度，使用一致性cache而不是基于TTL的cache。
1. 如何提高可用性：粗粒度锁，client在session过期后有一个grace period。
1. 未来可以做哪些改进：使用proxy和partitioning进一步提升服务规模。

<!--more-->

## Introduction

Chubby是一个**锁服务**。通过Chubby，不同client可以实现临界区，或达成共识。Chubby的主要设计目的是可靠性、可用性（服务大量普通商用机器）、语义容易理解，次要目的是吞吐和存储容量。Chubby提供了类似于文件系统的API（每个操作都会读写整个文件），建议式锁和事件通知。

Chubby预期提供**粗粒度**的同步，尤其是选举leader。

Chubby内核是Paxos算法。

## Design

### Rationale

为什么不实现一个Paxos库（client会成为一个Paxos系统的一个node），而是实现一个外部的锁服务：
- 应用一开始可能对可用性没那么高要求，等到后面再想接入Paxos库就困难了（比如要重新实现一个Paxos需要的状态机）；而接入一个外部的锁不需要做很多修改。
- 许多应用需要有办法能知道当前的共识是什么（谁是leader、某块数据是什么），外部服务可以提供一致的cache，而库只能实现基于时间过期的cache。
- 开发者更熟悉基于锁的API。
- 使用Paxos库会导致replica数量与client数量成正比，而外部服务将其解耦开，只需要少量机器运行Chubby就可以满足大量机器的一致性需求。

基于以上理由产生的两个关键决策：
- 锁服务而不是Paxos库或服务。
- 提供小文件的读写服务以广播和获取共识结果，而不是实现第二种服务（name service）。

一些根据使用场景而做出的决策：
- 需要支持一个Chubby file被数千个client读取，最好不需要大量机器。
- 需要提供事件通知，这样client不需要轮询结果。
- 需要有cache来支持大量client轮询。
- 需要一致性cache以降低开发者理解难度。
- 需要支持ACL。

Chubby不打算支持细粒度锁（通常只持有若干秒或更短时间），原因：
- 粗粒度锁的获取频率更低，降低server压力。
- 受server failover影响的client更少（粗粒度锁在failover期间client很可能不需要与server交互）。

应用可以基于Chubby的粗粒度锁自己实现细粒度锁：通过Chubby lock来维持若干个app lock server，每个app lock server管理若干个app lock，与app client之间通过lease维持。

### System structure

Chubby通常有5个replica，之间使用共识协议算法选举master。master与其它replica之间的lease为若干秒。每个replica都维护一个小DB，但只有master提供读写服务。

client通过DNS获取replica列表，与非master的replica通信时会被告知master地址。

如果有replica挂掉数小时没有恢复，它会被自动替换掉，更新DNS，master从而知道replica列表变动。

### Files, directories, and handles

Chubby提供的文件路径类似于：

```
/ls/foo/wombat/pouch
```

其中`ls`表示lock service，`foo`是cell name（如果是`local`表示访问应用所在的cell）。

Chubby因此可以被其它文件系统使用，可以使用现成的工具，极大降低了教育成本。

Chubby与UNIX文件系统间的设计差异是为了降低分布难度：不同的目录可能被不同的Chubby集群服务，因此：
- 不支持跨目录的文件移动；
- 不支持目录修改时间；
- 避免依赖于路径的权限机制（文件权限只取决于文件自身而不是目录权限）；
- 为了方便缓存meta数据，不显示最近访问时间。

文件或目录都被称为node，不同的node有不同的路径，不存在软硬链接。永久的node需要显式删除，临时的node在没有client使用时自动删除。临时node通常用于临时文件或标识client存活。

node的meta数据包含三类ACL（读、写、修改ACL），这些ACL在node创建时从它的父node处继承来（可覆盖）。ACL本身也是文件，存放在每个cell的固定路径下。Chubby的ACL机制类似于Plan9的groups。

node的meta数据中来包含4个单调增的64位整数：
- instance number，重复创建node时增加；
- content generation number，文件被写入时增加；
- lock generation number，node的锁被持有时增加；
- ACL generation number，node的ACL名字发生变化时增加。

Chubby也提供了每个文件的checksum，可以用来检测文件内容变化。

client持有的handle包含：
- 标识handle是否创建完成的位，这样只在创建handle时做完整的权限检查。
- sequence number，允许master检查这是不是之前master创建的handle。
- 打开文件时使用的模式，允许新master重建handle对应的状态。

### Locks and sequencers

Chubby提供的lock都是建议式的读写锁。不提供强制锁的原因：
- 应用可能使用Chubby保护lock对应的文件之外的资源。
- 通过查看被锁住的文件来debug或做维护工作。
- 相信开发者，强制锁也没办法保护错误或恶意的使用。

分布式锁常见的一个问题是一个client先获得锁，进一步操作时已经丢掉锁了但自己不知道，最终导致数据错误。常见解法是使用sequence number。

Chubby默认不使用sequence number（开销大），但允许client获取锁时同时也获得一个sequencer（包含lock name、mode、generation number的字符串），client后续操作可以带上这个sequencer从而允许server检测lock是否还有效。

对于早期的不支持sequencer的Chubby服务，有另一种机制，在一个lock holder挂掉后不立即允许其它client获取，而是等一段时间（如1分钟），这样避免出错的client破坏整个系统。

### Events

client在创建handle时可以订阅若干事件，包括：
- 文件内容修改；
- 子node的增删改（从而可以监控临时文件）；
- Chubby master自己的failover，表明可能有其它event丢失，client要自己处理这种情况，比如重新扫描文件；
- handle和lock不可用；
- 锁被人获取成功；
- 与另一个client的锁请求冲突。

事件投递晚于事件发生。

最后两个事件很少被用到：
- client通常不仅要知道选举结果产生了，还要知道leader是谁，因此它直接订阅文件内容修改就好了；
- 订阅另一个client的锁请求冲突可以实现一种场景，即当前持有锁的client提前释放锁，但实践中没有应用这么用。

### API

- `open`：返回handle。
- `close`：不会失败，调用后不再允许使用该handle（进程可能会挂掉）。
- `poison`：使该handle的后续操作失败，可以跨线程使用。
- `GetContentsAndStat`：返回文件数据和meta。
- `GetStat`：只返回meta。
- `ReadDir`：返回目录的子结点的名字和meta。
- `SetContents`：覆盖整个文件，可以传入content generation number以实现CAS。
- `SetACL`：设置文件的ACL。
- `Delete`：删除没有子结点的结点。
- `Acquire`、`TryAcquire`、`Release`：与锁相关。
- `GetSequencer`：返回sequencer。
- `CheckSequencer`：检测sequencer是否有效。

### Caching

client有一个文件内容和node meta的一致的写穿透的cache。master与client间通过lease维护cache，因此master知道client可能cache了什么。当文件内容或meta变动时，master会向可能缓存了这个文件的client发送invalidation。invalidation包含在了KeepAlive请求中，client收到后会在下次KeepAlive请求中回复master。文件内容或meta的修改要等到master获得了所有client的回复或cache lease过期后才生效。

如果client不回复invalidation，master就认为这个node不能缓存，这样invalidation只需要一轮RPC。Chubby的场景下读远多于写，因此可以这么做。另一个方案是阻塞住对这个node的后续操作，从而避免太多对这个node的操作击垮master，但会增加延迟。

Chubby的cache协议只失效，不更新，从而简化设计，也避免了访问过一个文件的client就要接收这个文件的后续的无穷无尽的更新消息。

Chubby没有选择更弱化的一致性cache是因为那样的cache很难使用。那些需要client在每个消息中都交换sequence number的方案不适用于已经存在多套通信协议的环境。

client还会cache handle，因此一个client重复`open`一个文件时，会返回相同的handle，避免每次都发RPC给master。

Chubby协议允许client缓存lock，即在lock失效后继续持有lock，直到另一个client冲突再主动释放。

### Sessions and KeepAlives

只要client与master之间的session有效，client的handl、lock、cache都有效。如果client不持有任何handle且1分钟以上无调用，session自动失效。

master延长client的lease timeout的情况：
- 创建session时
- master发生failover时
- 回复KeepAlive时

master收到KeepAlive时会hold，直到当前lease快过期时再回复。client收到回复后会立即发送下个KeepAlive。

master会把event和invalidation也加到KeepAlive的回复中，保证了client如果不告知invalidation是否成功，就没办法保有session。

client自己也维护一个local lease timeout，比master的lease timeout略大一点点（包含KeepAlive网络传输时间）。一旦local lease过期，client就会清空cache，等待grace period（45秒）后如果仍不能收到master回复，client就假设session已失效。

### Fail-overs

master一旦丢掉了master资格，就会清空掉session、handle、lock的内存状态。但session timer会直到新master产生才结束（不立即让client的session失效）。这样如果client能在grace period与新master建立通信，它的所有现存session都不会受影响。

在grace period中，client会阻塞住应用调用，以避免应用看到不一致的数据。

新master产生后通过读取持久化数据以及从client获取数据来重建内存状态。

新master要执行的操作：
- 选择新的epoch给client后续API使用。
- 可以回复master-location请求。
- 从DB中读取session和lock，将session lease延长至前一个master可能达到的最大值。
- 可以回复client的KeepAlive请求，但不做其它session相关的操作。
- 向每个session发送failover事件，收到的client会清空cache。
- 等待直到每个client要么告知了failover事件已收到，要么session过期。
- 可以执行任何操作。
- 将上一个master创建的handle延长至当前epoch。在当前epoch内该handle不可重建，从而避免网络上飘的请求创建出已经close的handle。
- 等待一段时间后删除没人打开的临时文件。client在收到failover事件后应该重新刷新临时文件的handle。但如果某个持有该临时文件的client的session过期了，临时文件可能不会立即删除。

### Database implementation

Chubby一开始使用BDB，但不需要那么完整的功能，同时BDB的replication带来了过高的风险，最终Chubby选择了实现一个简化版的使用WAL和snapshot的DB。

### Backup

Chubby会定期把DB的snapshot写入另一个cell的GFS上。

### Mirroring

Chubby可以在一个cell上镜像另一个cell的若干文件。`/ls/global/master`在各个cell都有镜像，路径为`/ls/cellname/slave`。

## Mechanisms for scaling

Chubby为了提高伸缩性而采取的部分措施：
- 多创建cell，保证不同client可以就近访问Chubby。
- master在高负载时可能会把lease timeout从12秒上调到60秒。
- Chubby client会缓存文件数据、meta、文件不存在、handle等，以降低rpc call。
- 有server将Chubby协议转换为更简单的协议如DNS。

Proxy和Partitioning是已设计但未实现的功能，原因是目前压力还没那么大：
- 一个数据中心的机器数量有限。
- 硬件性能的提升也会带来Chubby容量的提升。

### Proxies

Proxy可以处理KeepAlive和读请求，但不能处理写请求。使用Proxy会降低可用性，Proxy和Chubby master不可用都会导致client受影响。

### Partitioning

一个Chubby cell可以分为N个partition，每个目录根据其路径的hash只由一个partition服务，这样目录与父目录可能在不同partition上。

需要跨partition的操作：
- 访问ACL。但ACL非常适合缓存（数据量小、修改极少）。
- 删除目录时可能需要跨partition以确认它为空。

partitioning可以降低读写的压力，但每个client仍然要与多数partition保持KeepAlive，因此KeepAlive的压力没办法降低。

## Use, surprises and design errors

### Use and behaviour

一些结论：
- 多数（60%）文件需要按名字访问。
- 配置、ACL、元数据文件很常见。
- 缓存文件不存在的信息是很重要的。
- 平均每个被缓存的文件有10个client使用。
- 很少的client会持有锁，共享锁很罕见。锁主要用在选举leader和数据分片上。
- RPC主要由KeepAlive贡献。

9次服务中断：
- 网络维护：4次。
- 未知网络链路问题：2次。
- 软件错误：2次。
- 过载：1次。

6次丢失数据：
- DB软件错误：4次。
- 误操作：2次。

上调lease timeout可以降低KeepAlive的压力。group commit可以降低写压力，但通常不需要。

Chubby伸缩性的关键不是server性能，而是降低通信次数。

### Java clients

略

### Use as a name service

用作name service时，对比DNS，Chubby的优势是一致性cache，不需要设置TTL。但Chubby也会遇到性能问题，尤其是大量job启动时，此时可以用batch name lookup来缓解。

Chubby cache的一致性已经超出了name service需要的程度，因此可以使用专门为name service设计的协议转换server来进一步降低Chubby的负载。

### Problems with fail-over

Chubby的旧的failover方案需要master在创建session时写DB，使用BDB时写入压力太大，Chubby选择只在session第一次执行写（包括open和lock）操作时写DB。但这带来一个问题，readonly的session不在DB中，在master发生failover后很可能已经失效了，存在一个窗口期client可能读到过期的数据。

新方案中master不会在DB中记录session，而是在启动后等待最长可能的lease timeout，保证在此期间没有与master建立连接的client的session过期。这也允许proxy来管理与client的session。

### Abusive clients

- 应用接入前review很重要。用户经常没办法预计业务增长速度，因此review时要找出使开销线性化的因素，想办法弥补。
- 多数文档中缺乏性能方面的建议，导致一个调用了Chubby的接口未来被其它应用误用。
- 一开始Chubby没有缓存handle和文件不存在，导致`open`开销很大，经常有业务反复`open`。
- 一开始Chubby没有限制文件大小，导致有应用拿它作为存储用。最终Chubby上线了256KB的文件大小限制。
- 有应用拿Chubby作为PubSub用，但Chubby的重一致性导致了它不适合这个场景。

### Lessons learned

- 开发者经常忽视可用性：
    - Chubby稍有波动就可能导致应用出现严重问题。
    - 服务在线与服务可用之间有差别。如Chubby的global cell的总在线时间是超过local cell的，但从某个client来看，global cell的可用性却低于local cell，因为local cell与client之间网络通常不会分区，它们的维护时间通常也是重合的。
    - Chubby提供了master failover的事件，但发现应用经常收到这个event后直接crash。
    - Chubby提供了三种机制：接入前review；client中提供接口以自动处理Chubby中断；为Chubby中断提供事后报告。
- 可以不实现细粒度锁。
- 将KeepAlive与invalidation合并可以强制client一定要回复invalidation，否则session就会过期，但这样就给KeepAlive的协议选择带来了压力。TCP的退避重试可能会影响到lease timeout，因此Chubby选择了用UDP发送KeepAlive。

## Comparison with related work

略

## Summary

略
