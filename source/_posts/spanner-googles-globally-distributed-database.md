---
title:      "[笔记] Spanner: Google's Globally-Distributed Database"
date:       2020-12-14 12:22:39
tags:
    - 笔记
    - OLTP
    - Google
    - Paxos
    - 2PC
---

> 原文：[Spanner: Google's Globally-Distributed Database](https://dl.acm.org/doi/abs/10.1145/2491245)

**TL;DR**

Spanner 是划时代的 OLTP 系统，它的创新点是：
1. 用 TrueTime 实现了广域的物理 timestamp，这样不引入全局唯一的 TSO 就提供了基于 2PC 的分布式事务与 Snapshot Isolation。
1. 将数据分为了若干个 PaxosGroup，使用 MultiPaxos（但后来 [透露他们的实现更接近于当时还未提出的 Raft](http://wcl.cs.rpi.edu/pilots/library/papers/consensus/RAFTOngaroPhD.pdf)）实现了高可用。

Spanner 启发了很多后来者，但它的 TrueTime 是很难模仿的，后来者也通常使用 TSO 或 HLC 来代替，但这样就很难做到像 Spanner 一样跨越大洲部署仍然能提供合理的延时。

<!--more-->

## Introduction

在 Spanner 之前，Google 的 OLTP 方案有三种，但各自都有明显的缺点：
- 分库分表的 MySQL，维护代价极高，规模上不去。
- BigTable，不提供跨行事务和强一致的 geo-replication。
- Megastore，写吞吐上不去。

Spanner 的设计目标就是解决这些问题，同时作为 F1 的后端替换掉 Ads 的分库分表的 MySQL 集群。

Spanner 的特点是：
- 全球部署。
- 提供 ACID 的分布式事务。
- 数据多版本。
- 支持半结构化数据。

除此之外，它还有以下功能：
- 应用可以细粒度控制数据的 replication：
 - 哪些数据在哪个机房。
 - 数据离用户多远（影响读延时）。
 - replica 之间离多远（影响写延时）。
 - 多少个 replica。

 同时数据还可以动态跨 datacenter 迁移。
- 提供“外部一致性”和全球的 Snapshot Isolation（SI）。

Spanner 能支持全球范围的分布式事务，就是因为它基于 GPS 和分布在全球各个机房的原子钟，通过 TrueTime API 提供了有全局意义的 commit timestamp，且保证可线性化：如果事务 T1 的提交早于 T2 的开始，则 T1 的 commit ts 小于 T2 的 commit ts。Spanner 是第一个能在全球范围内有此保证的系统。

## Implementation

Spanner 的部署单位称为 universe，目前只有三个：test/playground，development/production，production-only。

每个 universe 分为若干个 zone，这些 zone 是实际部署的单位，也是数据 replication 可指定的单位。每个 datacenter 可以有 1 个或多个 zone。

![](/images/2020-12/spanner-01.jpg)

上图可以看到 Spanner 有两层 master。universemaster 和 placement drive 是全局唯一的，前者负责监控各个 zone 的情况，后者负责跨 zone 的数据自动迁移。每个 zone 还有 zone master，负责 zone 内部的数据分发；location proxy 被 client 用于查找数据所在的 spanserver。

### Spanserver Software Stack

![](/images/2020-12/spanner-02.jpg)

Spanner 的数据类似于 BigTable，也有 timestamp，但区别在于 Spanner 自己会赋 timestamp，而不是由用户指定。

每个 Spanserver 服务 1000-2000 个 tablet，每个 tablet 是一张表里的一部分数据（不一定连续），有 WAL 和若干个 B-tree 结构的文件，都保存在 Colossus 上。

每个 Spanserver 有一个 Paxos 的 replicated state machine，不同 zone 的多个 Spanserver 组成一个 Paxos Group。当时（2012 年）的 Spanner 实现是数据会写两遍 log，一遍写 paxos log，一遍写 tablet 的 WAL，只是权宜之计，计划要改掉（不确定做没做）。Paxos 的实现采用了 pipeline 方式（即不等当前写提交就开始下个写的 prepare），这样能在广域网的高延迟下提高吞吐，但写本身还是按顺序提交和应用的。

> - Paxos 的粒度是 Spanserver，而不是 tablet。这样灵活性会差一点，但 RPC 会少非常多，同时也能实现跨 zone 高可用的目的。
> - Spanner 的存储是 Paxos 的 replica 数乘以 Colossus 的 replica 数，即可能达到 5*5，但注意一个 zone 通常只有一个 Paxos 的 replica，相比其它 geo-replication 方式并没有增加存储量。
> - 所有 tablet 的状态共同构成了 Paxos 的 state machine，因此移动一个 tablet 不是 trivial 操作。

写操作必须发给 Paxos Group 的 leader，而读操作可以直接发给任意一个**足够新**的 replica 下面的对应 tablet。

每个 leader 上有一个 lock table，用于两阶段提交（2PC）。用悲观锁的原因是 Spanner 要支持长事务，此时如果用乐观锁会有特别多冲突，性能比较差。但只有需要同步的操作（如 write）需要用到 lock table，其它操作可以绕过它。

每个 leader 还有 transaction manager 来支持分布式事务。只涉及一个 PG 的事务不需要用到 transaction manager，直接使用 lock table。涉及多个 PG 的事务会选其中一个 PG 作为 coordinator，这个 PG 的 paxos 来提供 coordinator 的高可用，如果 leader 挂了 follower 会自动成为 coordinator。

### Directories and Placement

![](/images/2020-12/spanner-03.jpg)

Spanner 的数据分为了若干个 directory，每个 directory 对应一段 key range。directory 也是数据分布的单位，每个 directory 内的数据有着相同的 replication 配置。数据在 PG 间移动实际是在移动 directory（后面提到 directory 可能进一步分成若干个 fragment，fragment 才是移动的单位）。移动 directory 不会阻塞 client 操作。

前面提到的 tablet 与 BigTable 等系统的 tablet 不同，它的数据是不连续的，可能包含若干个 directory。

在 PG 间移动 directory 会通过后台的 movedir 任务来进行，它也负责增加或减少 PG 的 replica（此时 Spanner 还未支持 Paxos 配置修改）（不太清楚 movedir 怎么用来增减 replica）。movedir 过程中会先背景移动历史数据，可能经过若干轮，最后用一个 transaction 移动最后的非常少的增量数据同时更新元数据。

### Data model

Spanner 提供了：
- 基于有 schema 的半关系型表的数据模型。
- 类 SQL 的查询语言。
- 通用的事务。

支持有 schema 的半关系型表和同步的跨 datacenter 的 replication 是来源于 Megastore。类 SQL 的查询语言是来源于 Dremel。通用的事务是为了补上 BigTable 欠缺的功能。引入 Paxos 一方面能实现同步的跨 datacenter 的 replication，一方面也解决了 2PC 的 coordinator 高可用问题。

Spanner 的数据模型只是半关系型的原因在于它需要 primary key 有序且唯一，这点更像一个 key-value store。

Spanner 支持类似于 Megastore 的嵌入表结构，如下图。子表可以用`INTERLEAVE IN`声明自己与 root 表是交替存储的。`ON DELETE CASCADE`意思是 root 表中删一行也会删掉子表的对应行。

![](/images/2020-12/spanner-04.jpg)

## TrueTime

Spanner 最黑科技的功能就是 TrueTime。TrueTime 与正常的 timestamp 的区别在于它的格式为`[earliest, latest]`，是一个范围。两个 TrueTime 只有在时间范围完全不重叠时才能比较大小。

![](/images/2020-12/spanner-05.jpg)

Spanner 通过 GPS 和原子钟两个时钟源来获取时间。每个 datacenter 都有 time master，其中多数会有 GPS 接收器，另一些会有原子钟（paper 里强调原子钟没那么贵:)）。综合两种 time master 就可以得到一个时间点和误差范围，通常是 1ms-7ms，其中 0-6ms 来自原子钟的漂移，1ms 来自机器到 time master 的延时。但一些偶发的故障也会导致误差变大。

## Concurrency Control

Spanner 提供了 Snapshot Isolation，用时间 t 去读 DB，能读到所有早于 t 提交的数据（再次注意 TrueTime 的比较规则）。

### Timestamp Management

![](/images/2020-12/spanner-06.jpg)

Spanner 提供了三种读写操作：
- read-write transaction。
- read-only transaciton。
- snapshot read。

单独的写被实现为 rw 事务；非 snapshot 的单独的读被实现为 ro 事务。

ro 事务中的读会使用系统指定的 timestamp。snapshot 读是完全无锁的，client 可以指定一个 timestamp，也可以指定一个范围，由 Spanner 选择一个合适的 timestamp。无论是 ro 事务还是 snapshot 读都可以由任意足够新的 replica 来服务。

无论是 ro 事务还是 snapshot 读，一旦确定了 timestamp，都会默认 commit。如果 server 出现错误，client 可以换另一台 server，使用上次的 timestamp 和读到的位置继续读。

#### Paxos Leader Leases

Spanner 中 Paxos 的 lease 默认是 10 秒。为了保证两个 leader 的任期不会有重叠，定义前一个 leader 使用的最大的 timestamp 为 S<sub>max</sub>，则新 leader 要等到 TT.after(S<sub>max</sub>) 才能开始工作。

#### Assigning Timestamps to RW transactions

RW 事务的 timestamp 需要是在所有锁都已经拿到后，及在释放任意锁之前。Spanner 选择的是 Paxos 为提交事务的那次 Paxos 写所分配的 timestamp。

Spanner 要保证如下性质：每个 PG 内，Spanner分配给 Paxos 写的 timestamp 是单调增的；leader 只有在自己的 lease 有效时才能分配 timestamp。

Spanner 还要保证如下外部一致性：如果事务 T2 的开始晚于 T1 的提交，则 T2 的 commit ts 大于 T1 的 commit ts。这是通过以下规则保证的：
- Start：事务 Ti 的 coordinator 分配的 commit ts 为 Si，则 Si 不小于 TT.now().latest。
- Commit Wait：coordinator 保证在 TT.after(Si) 为真之前，client 不会看到任何 Ti 提交的数据。

具体证明如下：

![](/images/2020-12/spanner-07.jpg)

其中 e<sup>start</sup>是事务开始事件、e<sup>commit</sup>是事件提交事件、e<sup>server</sup>是事务到达 coordinator 事件。

#### Serving Reads at Timestamp

每个 replica 会记一个 t<sub>safe</sub>，为它能服务的最大的 ts，如果要读的 timestamp 不大于这个值，则这个 replica 就是足够新的。

t<sub>safe</sub>有两个来源。Paxos 的 safe ts 是指最高的已应用的 Paxos 写的 ts。Transaction manager（TM）的 safe ts 是：
- 如果当前没有已 prepare 但未 commit 的事务，则为无穷大。
- 如果有这样的事务，则 TM 的 safe ts 需要取所有这些事务的 prepare ts 的下限。每个 2PC 的参与者会为每个事务的 prepare 分配一个 ts，coordinator 保证最终事务的 commit ts 不会小于这些 prepare ts。

#### Assigning Timestamps to RO Transactions

ro 事务的执行分为两步：先确定 read ts，再用 read ts 发起一次 snapshot read。read ts 最简单的方案是取 TT.now().latest，但可能需要 ro 事务阻塞直到 read ts 小于 t<sub>safe</sub>。为了减少阻塞，Spanner 会分配一个能保证外部一致性的最老的 ts。

### Details

#### Read-Write Transactions

与 BigTable 类似，Spanner 中未提交的写会 buffer 在 client 那里，transaction read 不会读到这部分内容。

rw 事务中的读会发给 coordinator 并申请读锁，用 wound-wait（参考 [这里](https://stackoverflow.com/questions/32794142/what-is-the-difference-between-wait-die-and-wound-wait-deadlock-prevention-a)）来避免死锁。client 端事务持续过程中，client 会定期发 keepalive 以避免 server 端 timeout。

当 client 提交时，它先选择一个 coordinator PG，然后发送 commit 请求给各个参与者（包括 coordinator），其中包含 coordinator 的 id 和 buffer 的写数据。client 来做 prepare 可以避免数据在广域网上跑两遍。

每个参与者会申请写锁，然后选择一个单调增的 prepare ts，再通过 Paxos 把 prepare 信息写进 log，最后把自己的 prepare ts 发给 coordinator。

coordinator 第一步也是申请写锁，但不需要 prepare。它会在所有参与者把 prepare ts 发送过来后，选择一个 commit ts，需要不小于任意 prepare ts，同时大于它收到 commit 信息时的时间。之后 coordinator 通过 Paxos 写入 commit 信息。

在通知各个参与者应用 commit 前，coordinator 会执行 commit wait，等到 TT.after(s)。这个时间段通常与 commit 那次 Paxos 的通信是有重叠的，因此对延时的影响没那么大。

#### Read-Only Transactions

ro 事务需要先知道这个事务中要读的 key 列表，以确定这次事务涉及哪些 PG。如果只涉及一个 PG，则 client 会把事务发给对应 PG 的 leader，leader 会分配 read ts 并执行事务。单 site 的 read ts 是这个 PG 上次提交的写的 ts（LastTS()），从而能看到所有已提交的数据，且比使用 TT.now().latest 阻塞的概率更低。

跨 PG 的 ro 事务的 read ts 有多种选择。最复杂的方案是与所有 PG 的 leader 通信以获得最大的 LastTS()。Spanner 选择了一种更简单的方案，即直接使用 TT.now().latest，这样阻塞概率大一些，但不需要与所有 PG 的 leader 通信。之后读请求就可以发给某个足够新的 replica 了。

#### Schema-Change Transactions

Spanner 可以异步变更数据 schema 而不阻塞 client 操作，相比之下 BigTable 可以在一个 datacenter 内原子更新 schema，但会阻塞 client 操作。

Spanner 中 schema 变更也是一个事务，它先分配一个**未来**的 ts（这样对其它的操作的影响能降到最小）。然后执行 prepare，将新 schema 发给所有 PG。之后所有早于 prepare ts 的读写请求仍然按旧 schema 进行，不受影响；晚于 prepare ts 的读写请求要等到 start_ts.after(prepare_ts) 之后再执行，可能会阻塞一小会。

#### Refinements

前面介绍的 TM 的 safe ts 有个问题，即任何 prepare 了但不 commit 的事务都会导致 t<sub>safe</sub>没办法提升。解法是记录更细粒度的 t<sub>safe</sub>，可以直接记到 lock table 中，因为它就是一个细粒度（key range）的结构。

LastTS() 也有一个类似的问题：如果一个事务刚刚提交，与它无关的 ro 事务的 read ts 会取它的 commit ts，增大了这个 ro 事务被阻塞的概率。解法是类似的，记录更细粒度的信息。

Paxos 的 safe ts 的问题是当没有 Paxos 操作时就不能提升。Spanner 的解法利用了 leader 的任期不重叠的性质，每个 leader 可以在自己的任期内将 t<sub>safe</sub>提升到最小的下次 Paxos 写可能使用的 ts（MinNextTS() - 1）。当 Paxos 空闲时，leader 默认每 8 秒提升一次 MinNextTS()，另外 leader 也可以根据 follower 的请求提升 MinNextTS()。

