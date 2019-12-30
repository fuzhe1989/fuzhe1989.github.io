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

## TL;DR

Spanner是划时代的OLTP系统，它的创新点是：
1. 用TrueTime实现了广域的物理timestamp，这样不引入全局唯一的TSO就提供了基于2PC的分布式事务与Snapshot Isolation。
1. 将数据分为了若干个PaxosGroup，使用MultiPaxos（但后来[透露他们的实现更接近于当时还未提出的Raft](http://wcl.cs.rpi.edu/pilots/library/papers/consensus/RAFTOngaroPhD.pdf)）实现了高可用。

Spanner启发了很多后来者，但它的TrueTime是很难模仿的，后来者也通常使用TSO或HLC来代替，但这样就很难做到像Spanner一样跨越大洲部署仍然能提供合理的延时。

<!--more-->

## Introduction

在Spanner之前，Google的OLTP方案有三种，但各自都有明显的缺点：
- 分库分表的MySQL，维护代价极高，规模上不去。
- BigTable，不提供跨行事务和强一致的geo-replication。
- Megastore，写吞吐上不去。

Spanner的设计目标就是解决这些问题，同时作为F1的后端替换掉Ads的分库分表的MySQL集群。

Spanner的特点是：
- 全球部署。
- 提供ACID的分布式事务。
- 数据多版本。
- 支持半结构化数据。

除此之外，它还有以下功能：
- 应用可以细粒度控制数据的replication：
    - 哪些数据在哪个机房。
    - 数据离用户多远（影响读延时）。
    - replica之间离多远（影响写延时）。
    - 多少个replica。

    同时数据还可以动态跨datacenter迁移。
- 提供“外部一致性”和全球的Snapshot Isolation（SI）。

Spanner能支持全球范围的分布式事务，就是因为它基于GPS和分布在全球各个机房的原子钟，通过TrueTime API提供了有全局意义的commit timestamp，且保证可线性化：如果事务T1的提交早于T2的开始，则T1的commit ts小于T2的commit ts。Spanner是第一个能在全球范围内有此保证的系统。

## Implementation

Spanner的部署单位称为universe，目前只有三个：test/playground，development/production，production-only。

每个universe分为若干个zone，这些zone是实际部署的单位，也是数据replication可指定的单位。每个datacenter可以有1个或多个zone。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-01.jpg)

上图可以看到Spanner有两层master。universemaster和placement drive是全局唯一的，前者负责监控各个zone的情况，后者负责跨zone的数据自动迁移。每个zone还有zone master，负责zone内部的数据分发；location proxy被client用于查找数据所在的spanserver。

### Spanserver Software Stack

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-02.jpg)

Spanner的数据类似于BigTable，也有timestamp，但区别在于Spanner自己会赋timestamp，而不是由用户指定。

每个Spanserver服务1000-2000个tablet，每个tablet是一张表里的一部分数据（不一定连续），有WAL和若干个B-tree结构的文件，都保存在Colossus上。

每个Spanserver有一个Paxos的replicated state machine，不同zone的多个Spanserver组成一个Paxos Group。当时（2012年）的Spanner实现是数据会写两遍log，一遍写paxos log，一遍写tablet的WAL，只是权宜之计，计划要改掉（不确定做没做）。Paxos的实现采用了pipeline方式（即不等当前写提交就开始下个写的prepare），这样能在广域网的高延迟下提高吞吐，但写本身还是按顺序提交和应用的。

> - Paxos的粒度是Spanserver，而不是tablet。这样灵活性会差一点，但RPC会少非常多，同时也能实现跨zone高可用的目的。
> - Spanner的存储是Paxos的replica数乘以Colossus的replica数，即可能达到5*5，但注意一个zone通常只有一个Paxos的replica，相比其它geo-replication方式并没有增加存储量。
> - 所有tablet的状态共同构成了Paxos的state machine，因此移动一个tablet不是trivial操作。

写操作必须发给Paxos Group的leader，而读操作可以直接发给任意一个**足够新**的replica下面的对应tablet。

每个leader上有一个lock table，用于两阶段提交（2PC）。用悲观锁的原因是Spanner要支持长事务，此时如果用乐观锁会有特别多冲突，性能比较差。但只有需要同步的操作（如transaction read）需要用到lock table，其它操作可以绕过它。

每个leader还有transaction manager来支持分布式事务。只涉及一个PG的事务不需要用到transaction manager，直接使用lock table。涉及多个PG的事务会选其中一个PG作为coordinator，这个PG的paxos来提供coordinator的高可用，如果leader挂了follower会自动成为coordinator。

### Directories and Placement

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-03.jpg)

Spanner的数据分为了若干个directory，每个directory对应一段key range。directory也是数据分布的单位，每个directory内的数据有着相同的replication配置。数据在PG间移动实际是在移动directory（后面提到directory可能进一步分成若干个fragment，fragment才是移动的单位）。移动directory不会阻塞client操作。

前面提到的tablet与BigTable等系统的tablet不同，它的数据是不连续的，可能包含若干个directory。

在PG间移动directory会通过后台的movedir任务来进行，它也负责增加或减少PG的replica（此时Spanner还未支持Paxos配置修改）（不太清楚movedir怎么用来增减replica）。movedir过程中会先背景移动历史数据，可能经过若干轮，最后用一个transaction移动最后的非常少的增量数据同时更新元数据。

### Data model

Spanner提供了：
- 基于有schema的半关系型表的数据模型。
- 类SQL的查询语言。
- 通用的事务。

支持有schema的半关系型表和同步的跨datacenter的replication是来源于Megastore。类SQL的查询语言是来源于Dremel。通用的事务是为了补上BigTable欠缺的功能。引入Paxos一方面能实现同步的跨datacenter的replication，一方面也解决了2PC的coordinator高可用问题。

Spanner的数据模型只是半关系型的原因在于它需要primary key有序且唯一，这点更像一个key-value store。

Spanner支持类似于Megastore的嵌入表结构，如下图。子表可以用`INTERLEAVE IN`声明自己与root表是交替存储的。`ON DELETE CASCADE`意思是root表中删一行也会删掉子表的对应行。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-04.jpg)

## TrueTime

Spanner最黑科技的功能就是TrueTime。TrueTime与正常的timestamp的区别在于它的格式为`[earliest, latest]`，是一个范围。两个TrueTime只有在时间范围完全不重叠时才能比较大小。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-05.jpg)

Spanner通过GPS和原子钟两个时钟源来获取时间。每个datacenter都有time master，其中多数会有GPS接收器，另一些会有原子钟（paper里强调原子钟没那么贵:)）。综合两种time master就可以得到一个时间点和误差范围，通常是1ms-7ms，其中0-6ms来自原子钟的漂移，1ms来自机器到time master的延时。但一些偶发的故障也会导致误差变大。

## Concurrency Control

Spanner提供了Snapshot Isolation，用时间t去读DB，能读到所有早于t提交的数据（再次注意TrueTime的比较规则）。

### Timestamp Management

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-06.jpg)

Spanner提供了三种读写操作：
- read-write transaction。
- read-only transaciton。
- snapshot read。

单独的写被实现为rw事务；非snapshot的单独的读被实现为ro事务。

ro事务中的读会使用系统指定的timestamp。snapshot读是完全无锁的，client可以指定一个timestamp，也可以指定一个范围，由Spanner选择一个合适的timestamp。无论是ro事务还是snapshot读都可以由任意足够新的replica来服务。

无论是ro事务还是snapshot读，一旦确定了timestamp，都会默认commit。如果server出现错误，client可以换另一台server，使用上次的timestamp和读到的位置继续读。

#### Paxos Leader Leases

Spanner中Paxos的lease默认是10秒。为了保证两个leader的任期不会有重叠，定义前一个leader使用的最大的timestamp为S<sub>max</sub>，则新leader要等到TT.after(S<sub>max</sub>)才能开始工作。

#### Assigning Timestamps to RW transactions

RW事务的timestamp需要是在所有锁都已经拿到后，及在释放任意锁之前。Spanner选择的是Paxos为提交事务的那次Paxos写所分配的timestamp。

Spanner要保证如下性质：每个PG内，Spanner分配给Paxos写的timestamp是单调增的；leader只有在自己的lease有效时才能分配timestamp。

Spanner还要保证如下外部一致性：如果事务T2的开始晚于T1的提交，则T2的commit ts大于T1的commit ts。这是通过以下规则保证的：
- Start：事务Ti的coordinator分配的commit ts为Si，则Si不小于TT.now().latest。
- Commit Wait：coordinator保证在TT.after(Si)为真之前，client不会看到任何Ti提交的数据。

具体证明如下：

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/spanner-07.jpg)

其中e<sup>start</sup>是事务开始事件、e<sup>commit</sup>是事件提交事件、e<sup>server</sup>是事务到达coordinator事件。

#### Serving Reads at Timestamp

每个replica会记一个t<sub>safe</sub>，为它能服务的最大的ts，如果要读的timestamp不大于这个值，则这个replica就是足够新的。

t<sub>safe</sub>有两个来源。Paxos的safe ts是指最高的已应用的Paxos写的ts。Transaction manager（TM）的safe ts是：
- 如果当前没有已prepare但未commit的事务，则为无穷大。
- 如果有这样的事务，则TM的safe ts需要取所有这些事务的prepare ts的下限。每个2PC的参与者会为每个事务的prepare分配一个ts，coordinator保证最终事务的commit ts不会小于这些prepare ts。

#### Assigning Timestamps to RO Transactions

ro事务的执行分为两步：先确定read ts，再用read ts发起一次snapshot read。read ts最简单的方案是取TT.now().latest，但可能需要ro事务阻塞直到read ts小于t<sub>safe</sub>。为了减少阻塞，Spanner会分配一个能保证外部一致性的最老的ts。

### Details

#### Read-Write Transactions

与BigTable类似，Spanner中未提交的写会buffer在client那里，transaction read不会读到这部分内容。

rw事务中的读会发给coordinator并申请读锁，用wound-wait（参考[这里](https://stackoverflow.com/questions/32794142/what-is-the-difference-between-wait-die-and-wound-wait-deadlock-prevention-a)）来避免死锁。client端事务持续过程中，client会定期发keepalive以避免server端timeout。

当client提交时，它先选择一个coordinator PG，然后发送commit请求给各个参与者（包括coordinator），其中包含coordinator的id和buffer的写数据。client来做prepare可以避免数据在广域网上跑两遍。

每个参与者会申请写锁，然后选择一个单调增的prepare ts，再通过Paxos把prepare信息写进log，最后把自己的prepare ts发给coordinator。

coordinator第一步也是申请写锁，但不需要prepare。它会在所有参与者把prepare ts发送过来后，选择一个commit ts，需要不小于任意prepare ts，同时大于它收到commit信息时的时间。之后coordinator通过Paxos写入commit信息。

在通知各个参与者应用commit前，coordinator会执行commit wait，等到TT.after(s)。这个时间段通常与commit那次Paxos的通信是有重叠的，因此对延时的影响没那么大。

#### Read-Only Transactions

ro事务需要先知道这个事务中要读的key列表，以确定这次事务涉及哪些PG。如果只涉及一个PG，则client会把事务发给对应PG的leader，leader会分配read ts并执行事务。单site的read ts是这个PG上次提交的写的ts（LastTS()），从而能看到所有已提交的数据，且比使用TT.now().latest阻塞的概率更低。

跨PG的ro事务的read ts有多种选择。最复杂的方案是与所有PG的leader通信以获得最大的LastTS()。Spanner选择了一种更简单的方案，即直接使用TT.now().latest，这样阻塞概率大一些，但不需要与所有PG的leader通信。之后读请求就可以发给某个足够新的replica了。

#### Schema-Change Transactions

Spanner可以异步变更数据schema而不阻塞client操作，相比之下BigTable可以在一个datacenter内原子更新schema，但会阻塞client操作。

Spanner中schema变更也是一个事务，它先分配一个**未来**的ts（这样对其它的操作的影响能降到最小）。然后执行prepare，将新schema发给所有PG。之后所有早于prepare ts的读写请求仍然按旧schema进行，不受影响；晚于prepare ts的读写请求要等到start_ts.after(prepare_ts)之后再执行，可能会阻塞一小会。

#### Refinements

前面介绍的TM的safe ts有个问题，即任何prepare了但不commit的事务都会导致t<sub>safe</sub>没办法提升。解法是记录更细粒度的t<sub>safe</sub>，可以直接记到lock table中，因为它就是一个细粒度（key range）的结构。

LastTS()也有一个类似的问题：如果一个事务刚刚提交，与它无关的ro事务的read ts会取它的commit ts，增大了这个ro事务被阻塞的概率。解法是类似的，记录更细粒度的信息。

Paxos的safe ts的问题是当没有Paxos操作时就不能提升。Spanner的解法利用了leader的任期不重叠的性质，每个leader可以在自己的任期内将t<sub>safe</sub>提升到最小的下次Paxos写可能使用的ts（MinNextTS() - 1）。当Paxos空闲时，leader默认每8秒提升一次MinNextTS()，另外leader也可以根据follower的请求提升MinNextTS()。
