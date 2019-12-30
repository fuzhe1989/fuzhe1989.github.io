---
title:      "[笔记] The Google File System"
date:       2020-09-15 21:20:10
tags:
    - 笔记
    - FileSystem
    - DistributedSystem
    - Google
---

> 原文：[The Google File System](https://research.google/pubs/pub51.pdf)

## Introduction

GFS在设计上与传统的文件系统的几个区别：
- 分布式系统中错误是常态，因此持续监控、错误检测、容错机制、自动恢复都是必要的。
- 文件普遍非常大，达到GB级别以上，要重新审视对I/O操作和块大小的预期。
- 文件的追加写远多于覆盖写。

<!--more-->

## Design Overview

### Assumptions

- 整个系统由廉价易坏的硬件组成。
- 典型文件大小在100MB以上。支持小文件但不做特殊优化。
- 主要有两种读模式，大块顺序读和小块随机读。应用也可以自行将多个随机读排序转化为一次顺序读。
- 要支持高效的并发追加写同一个文件。
- 高吞吐优先于低延时。

### Architecture

一个GFS集群由单master和若干个chunkserver组成。文件由固定大小的chunk组成。

master负责维护：
- 所有metadata，包括namespace、ACL、file到chunk的映射、chunk当前所在的chunkserver。
- 全局动作，包括chunk的lease管理和chunk的迁移。

client的数据读写都直接与chunkserver通信，只在需要metadata时才与master通信。

client和chunkserver都不缓存数据，前者不需要，后者通过Linux的page cache就可以达到目的了。

### Single Master

单master极大简化了设计，但要非常小心地避免它成为读写路径的瓶颈。client只在第一次访问一个chunk时找master要它的位置，之后会缓存这一信息，直接与对应的chunkserver通信完成读写。

### Chunk Size

GFS的chunk size默认为64MB，远大于传统文件系统的block size。大的chunk size的好处：
- 减少了client与master的通信次数。
- client一次在大chunk上完成的操作数会更多，减少了每次与chunkserver建立TCP连接的开销。
- 减少了master要维护的metadata。

但大chunk使小文件经常只有非常少量的chunk，容易成为热点。一个场景是应用先写入一个单chunk的文件，之后大量job同时读取这个文件。解法是给这个文件分配更多的replica，同时应用端在job启动时加一个随机的延迟。

### Metadata

metadata主要为两类：
- namespace和file-chunk映射，修改会记录在operation log中。
- chunk的位置，不持久化，master启动时和有chunkserver加入时，master主动访问chunkserver得到。

#### In-Memory Data Structures

master管理的所有metadata都常驻内存，好处略。隐患是chunk太多的话master的内存可能成问题（注：pangu1的常见问题），但相比而言给master机器加内存还是比较容易的。

#### Chunk Locations

master通过与chunkserver的心跳获得chunk位置，而不持久化这些信息，原因是：
- 持久化的话就要保持master与chunkserver关于chunk位置的一致，考虑到chunkserver的扩缩容、failover、改名等情况，比较复杂。
- chunkserver才拥有chunk位置的最终话语权，比如chunk损坏了等。

#### Operation Log

operation log不仅记录事件，还充当一个逻辑时钟。

checkpoint文件本身组织为B-tree，可以直接mmap到内存中访问。

### Consistency Model

#### Guarantees by GFS

一个文件区域在被修改后，如果所有client从无论哪个replica总是看到相同的状态，称其为一致的；如果不光是一致的，client知道修改后的状态，称其为确定的。

成功的串行修改总是确定的，成功的并行修改是一致但不确定的，失败的修改是不一致的。

一次成功的追加写至少会写入一份数据，并返回确定区域的offset。

GFS通过两点保证多次成功的修改后，被修改的文件区域是确定的，包含最后一次写入的内容：
- 所有replica应用相同的修改顺序。
- 使用chunk version来标识哪些replica已过期。

过期的repilca不会参与后续的读写，后面会被gc掉。

在client的chunk位置的cache过期前，有一个小的时间窗口client可能读到过期数据。但因为过期的replica通常都提前结束了，client会与master通信，从而获取到最新的chunk位置。

master会在chunk有replica损坏或过期后尽快恢复出一个新的replica，除非非常短时间内所有replica都损坏了，此时数据也只是丢失，而不是写坏了，应用可以得到确定的异常，而不是错误的数据。

#### Implications for Applications

- 依赖追加写而不是覆盖写，从而可以只用checkpoint就知道哪块文件区域是确定的。
- 记录应用自己的checkpoint，读的时候只读到最新的checkpoint，避免读到那些GFS认为成功写入但应用认为没有的区域。
- 数据要能自检验、自识别，能知道哪些记录是无效的或重复的。

## System Interactions

### Leases and Mutation Order

master会为每个chunk挑选一个primary，primary负责决定这个chunk的所有修改的顺序。

master与primary chunk间的lease默认为60s，但master可以提前结束lease（如禁止修改改名中的文件）。假设master与primary chunk断开了，它可以在当前lease过期后再挑选新的primary。

client执行一次修改操作的过程（类似于2PC）：
1. client找master要chunk对应的replica，此时如果这个chunk没有primary，master会临时授予一个。
1. client将数据推给所有replica，不需要考虑顺序。chunkserver会将这些数据缓存在LRU队列中（如果过期了后续会导致请求失败）。
1. client向primary发送写请求，primary为所有它收到还未处理的数据分配sequence number，并在本地执行修改。
1. primary将写请求转发给所有replica，每个replica按相同顺序执行这些写请求。
1. secondary回复primary。
1. primary回复client。

任何replica的失败都会使写请求失败，文件的对应区域此时是不一致的。GFS的client默认会重试请求（如何处理留下来的不一致区域？）。

如果一次写跨chunk，GFS会将其分为多次写，可能导致并发写的数据交织在一起（追加写）或相互覆盖（覆盖写），即一致但未定义。

### Data Flow

为了充分利用整个集群的网络带宽，数据在不同chunkserver间是串行传输的，每个chunkserver会把数据发送给离自己最近的还没收到数据的chunkserver。这里距离是直接用IP地址计算出来的。

### Atomic Record Appends

primary接收到一个atomic append请求时，它先判断当前chunk是否有足够空间，如果没有的话，它会先补全当前chunk，再返回client一个可重试的错误。否则primary先在本地追加，再告诉secondary具体的offset，最后回复client。

如果任一replica写失败，整个请求就失败了，client重试可能导致不同repilca不完全一致：每个replica上可能写了一次或多次或不完整的记录。GFS不保证每个replica完全一致，它只保证数据至少被原子写入一次。成功的请求表明数据一定在所有replica上写到了相同的offset上；每个replica的数据长度都至少等于这条记录的结束位置，因此任何后续的记录都会写到更高的offset或新chunk上（不会覆盖老数据）。追加写保证会得到确定（因此一致）的结果，而交替的覆盖写则是不一致（因此不确定）的。

### Snapshot

master收到snapshot请求时，先取消所有涉及到的chunk的lease，保证后续写请求都必须先与master通信。之后master将snapshot记入opeartion log，再复制源文件或文件树的metadata。新创建出来的snapshot文件指向与源文件相同的chunk。

client后续要写snapshot过的chunk C时，它会先找master，master发现C有多个引用，于是要求所有持有C的replica的chunkserver复制出一个C'，再为C'挑选一个primary，此后的流程与正常写相同。

## Master Operation

### Namespace Management and Locking

GFS没有使用树形结构来维护namespace，而是将所有namespace保存在一个表中，每条记录的key就是完整path，如"/d1/d2/.../dn/leaf"对应着"/d1"、"/d1/d2"、"/d1/d2/.../dn/leaf"等一系列key。每条记录对应一个读写锁，在访问一个路径时，要持有它的所有前缀记录的读锁。文件创建不需要持有前缀记录的写锁，只要读锁就足够保证前缀记录不被删除了。

这一性质的一大优点是它允许在同一目录内并发地修改。

### Replica Placement

chunk的replica位置服务于两个目的：
- 最大化数据可靠性和可用性；
- 最大化网络带宽利用率。

因此要将replica分布于不同rack的机器上，从而避免某个rack出问题导致整个chunk数据丢失，也有利于读的时候充分利用整个rack的网络带宽。代价就是写的时候数据要流经多个rack。

### Creation, Re-replication, Rebalancing

新创建chunk时位置选择的考虑：
- 磁盘空闲的chunkserver优先；
- 控制每个chunkserver上新创建的chunk不要太多，避免后续流量集中在这个chunkserver上；
- 分散在不同rack上。

一旦master发现某个chunk的replica少了，就会触发re-replication。它的优先级是看replica少了几份，chunk是否活跃，以及是否阻塞了client的请求。

master定期会做rebalance，但不会直接填满新加入的chunkserver，而是渐进式的。

### Garbage Collection

文件被删除时，master先记录到operation log中，再将文件rename为一个名字包含删除时间戳的隐藏文件。之后master会定期扫描这些文件，删除超过若干天的就被真正删除掉。master给chunkserver的心跳回复中会包含chunkserver持有的哪些chunk需要被删除。

通过这种异步的、基于状态对比的、批量通知的方式，GFS的回收可以处理多种情况造成的垃圾，简化了流程，也平摊了开销，缺点就是回收延迟比较大。补救措施是，隐藏文件被删除时会立即执行删除、用户可以指定强制删除、用户可以指定某个路径下的文件不参与replication。

### Stale Replica Detection

对于每个chunk，master都维护了一个version，每次授予lease时增加，master和replica都会持久化这个version。master如果遇到了比它自己记录的更大的version，可能是master之前failover过了，master会将自己记录的version提升上去；如果遇到了更小的version就会认为这个replica过期了，变成垃圾了。

master在回复replica位置给client时，也会带上这个version，从而避免client访问过期的replica。

## Fault Tolerance and Diagnosis

GFS的容量决定了错误是常态。

### High Availability

#### Fast Recovery

master和chunkserver不区分正常退出和意外退出，都走一样的快速恢复流程。

#### Chunk Replication

- 不同namespace可以有不同的replication level。
- master会尽快复制replica不足的chunk。
- 正在探索奇偶校验码、纠删码等其它冗余方案，这是基于GFS追加和读远多于小块随机写的场景的。

#### Master Replication

master的修改要等到自身和所有replica的operation log落盘才算提交成功。

master的replica是shadow而不是mirror，状态可能会落后于primary，但它们也可以提供非最新的读能力。

shadow replica也会用与primary相同的顺序执行修改，也会从chunkserver获取chunk列表，只有新创建的chunk和被回收的chunk必须从primary处得知。

### Data Integrity

GFS一个集群会有大量磁盘，时常会有磁盘错误，但一一比较每个chunk replica很不现实。而且不同replica也可能不一致。因此chunkserver会通过checksum来校验chunk是否正确。

chunk的每个block都有checksum，是常驻内存的，读的时候会校验。这对性能的影响非常小，因为对比checksum不会有I/O，而计算checksum的开销会被I/O平摊掉。client会按block大小对齐读的范围。

追加写的时候直接基于旧的checksum计算新的checksum。如果之前这个block状态已经不一致了，checksum不对，后续读的时候会发现。

覆盖写需要读出整个范围的block重新计算checksum。

chunkserver会在空闲时挑选一些不活跃的chunk校验数据。

# Experiences

GFS发展过程中遇到的问题：
- 后续才增加了权限和配额的支持。
- 磁盘驱动版本。
- Linux 2.2的`fsync()`有性能bug。
- Linux一个进程读写页面时有一个大的读写锁，加载page和通过`mmap`更新page互斥。后续通过将`mmap`改为`pread`解决。

> pangu1也是单master，与GFS的区别主要在于：
> - 似乎没有primary chunk的概念。
> - master会记录chunk的长度。
>
> pangu1中文件分成了LogFile、NormalFile、以及一个可以随机写的File（不记得名字了）。
>
> NormalFile类似于GFS的File，链式replication，只支持追加写。它的主要的写接口是`append()`、`commit()`和`truncate()`，`append`写入的数据不会改变master端记录的size，这个是在`commit`时确定的。当出现写失败时，不同replica可能不一致，此时可以通过`truncate`到上次`commit`时的offset来解决这种不一致。对于`commit`超时的情况，client需要重新打开文件，触发master主动获取各个replica的size。
>
> LogFile是专门针对小的record设计的类型，星型写，只支持追加写，不需要额外的`commit`，也不支持`truncate`。它的size更新逻辑似乎是如果三份都成功，则不立即更新；当写失败时，client需要重新打开文件，触发master主动封存当前chunk，此时master会确认chunk的size。
>
> master也会在与chunkserver的日常通信过程中确认chunk的size。
>
> pangu2将单master按功能拆成若干种master node，从而解决了单master内存爆炸的问题。另外不同的文件类型也统一掉了，具体的细节不清楚。