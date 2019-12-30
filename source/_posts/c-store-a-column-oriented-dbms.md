---
title:      "[笔记] C-store: a column-oriented DBMS"
date:       2020-08-13 12:12:32
tags:
    - 笔记
    - Database
    - Columnar
---

> 原文：[C-store: a column-oriented DBMS](https://dl.acm.org/doi/abs/10.1145/3226595.3226638)

## TL;DR

本文idea：面向ad-hoc读优化的分布式DBMS。

论证过程：
1. 面向ad-hoc读优化需要：
   1. 列存，以提高压缩和扫描效率；
   1. 数据直接存成多种排序形式，从而应对ad-hoc查询；
   1. 只读事务避免加锁，需要读写分离以及snapshot isolation。
1. 列存需要仔细地编码数据，因此需要区分不同情况编码。
1. 数据存成projection需要考虑：
   1. 不同replica可以用不同的projection，这样既满足了安全性，又有机会提高性能；
   1. anchor table物理上不存在，就需要通过storage key和join index组合多个projection来还原一行数据；
1. 读写分离要考虑：
   1. 分成WS和RS，这样两者可以用不同的存储结构，分别在写和读上达到最优；
   1. 任意时间的MVCC开销太大，可以把时间分成epoch，一段一段推进；
   1. 需要有一个将WS合并进RS的操作。
1. 考虑到C-Store的主要场景是写特别少读特别多，可以：
   1. 不压缩WS中的数据；
   1. 使用简化的2PC而不需要考虑吞吐问题。

感想：
1. 学术界的paper画风和工业界的确实不太一样，到处都是idea，但细推敲又觉得缺点什么。
1. 列存的核心问题就是编码。
1. 不同replica用不同projection确实是非常棒的idea，但这样要求DB和FS必须耦合在一起，很多情况下不现实。
1. 关于读写分离的几个idea很棒，MVCC是可以不用那么细粒度的。

<!--more-->

## Abstract

C-Store是面向读优化的RDBMS，与当前面向写优化的RDBMS截然不同：
- 列存 vs 行存
- 内存中的数据与磁盘上的数据一样仔细紧凑地编码
- 数据保存为若干个互有重叠的列存的projection，而不是传统的table和index
- 保证了高可用性和对只读事务的snapshot isolation的非传统的事务实现
- 大量结合使用B-tree和bitmap

## Introduction

传统的OLTP系统按行存，单次写就可以把一行所有字段写到磁盘上，面向写优化。

ad-hoc查询需要面向读优化的系统。典型如data warehouse，运行模式是批量导入数据，之后长时间的ad-hoc查询。这类系统中使用列存会更高效。C-Store就是这样的系统。

列存的优势在于，ad-hoc查询通常只需要少量列，使用列存可以只访问少量数据。但读优化的系统与写优化的系统在架构上还有其它区别。

当前的DBMS通常将数据按字或字节对齐，这样对CPU，但CPU越来越快了，可以按两种方法用CPU换磁盘带宽：
- 紧凑编码数据，如用6bit来表示州，而不是2Byte。
- 紧凑排列数据，如一列的若干个值连续排列。

DBMS最好能直接在压缩后的数据上查询，只在需要时解压数据。

传统RDBMS使用B-tree存储table和index中的数据，这样对OLTP够用了，但在OLAP上不够高效。我们可以使用更高效的针对只读场景的存储结构。

C-Store将数据保存为若干个projection，每个projection包含一行的部分列，不同的projection可以有相同列，但排序不同。因为每个projection都是排序的，压缩效率会比较高，这样数据保存多份的存储开销不会太大。

C-Store使用若干台grid机器组成shard-nothing架构，数据分为多个partition保存在不同node上。

C-Store一份数据会有若干个replica，但目的是高可用，缩短恢复时间，且不同replica可以是不同的projection（只要所有列都被覆盖到了）。这样同时兼顾了读优化和高可用。C-Store可以配置允许最多K个node出问题（目前还只支持K=1）。

一个趋势是data warehouse也需要支持在线写入。但兼顾高效的在线写与ad-hoc查询是非常困难的。C-Store的作法是将存储分为Writeable Store（WS）与Read-optimized Store（RS），通过tuple mover将WS转为RS。

Insert会发给WS，而Delete会同时发给WS和RS（要在RS中记录）。Update实现为一次Delete加Insert。Tuple mover过程借鉴了LSM的merge，将较小的WS数据与巨大的RS数据作merge生成新的RS（似乎过程中会复制所有数据）。

为了支持事务而不影响读性能，C-Store对只读事务使用了snapshot isolation，只能读到历史数据。

最后，面向列存的优化器设计与传统的行存系统也是非常不同的（但我不懂这个领域）。

C-Store的创新点：
- WS加RS的混合结构，同时支持高效写与读。
- 不同replica使用不同projection，查询时可以选择最适合的replica。
- 高压缩的列存格式。
- 面向列的优化器与执行器。
- 足够多的projection可以保证K-safety下的高可用性和高性能。
- 使用snapshot isolation以避免2PC和读锁。

## Data Model

C-Store中table只在逻辑上存在，实际存储的只有projection。所有projection都有相同的行数。每个projection都按sort key range分为若干个segment。

为了能还原table，C-Store使用了两种数据：
- Storage Keys：一个segment内同一行数据的不同列都有着相同的storage key。Storage key实际是这行在这个RS内的编号，因此不需要真的存下来，通过这行的位置就可以计算得到。对应的WS中的storage key保证大于所有RS segment中的storage key。
- Join Indexes：Join index实际是一组index，帮助组合一系列projection的数据，最终得到完整的一行。Join index是segment级别的，格式为`{next_projection, sid, storage_key}`。

> 后面大篇幅在讲如何选择一条路径来最小化join得到一行的开销。

因为join index的维护代价比较大，C-Store推荐在实践中选择若干个projection包含全部列。

为了能确定projection、segment、sort key、join index等该如何分配才能同时满足K-safety和性能，可以先训练C-Store集群，比如记录所有请求并回放。

## RS

RS中按列分别保存数据，每列中数据都按sort key的顺序排列。

### Encoding Schemes

每列的数据格式按两个维度（是否是sort key、有多少distinct value）分为四种情况：
1. 是sort key，只有很少distinct value：每个值表示为`(v, f, n)`，其中v是值，f是v第一次出现的位置，n是出现次数。之后用B-tree索引这些值。
1. 不是sort key，只有很少distinct value：每个值表示为`(v, b)`，其中b是bitmap，记录每个值出现的位置，如001101001，预期这个bitmap是稀疏的，可以用RLE编码。为了高效实现查找第i个值的操作，还需要一个B-tree index。
1. 是sort key，很多distinct value：每列差值编码，之后用B-tree索引这些值。
1. 不是sort key，很多distinct value：直接保存原始值，用B-tree索引。

### Join Indexes

join index就是一系列`(sid, storage_key)`，和其它存储没什么区别。

## WS

为了避免维护两套optimizer，WS也是列存格式，也有projection和join index。WS中storage key是显式分配的（RS中是推算出来的），初始值大于任意一个RS中最大的storage key。

WS与RS的segment是一一对应的，因此`(sid, storage_key)`既可能指向RS也可能指向WS。

WS通常比较小，因此数据不考虑压缩，每个projection直接用B-tree保存数据。

## Storage Management

C-Store使用storage allocator分配segment给各个node，规则是：
- 同一个projection的同一个segment的不同列应该分配在一起。
- join index segment应该与sender segment分配在一起。
- 对应的WS和RS segment应该分配在一起。

值比较大的列（若干MB）会单独保存。

## Updates and Transaction

一次insert会在每个projection的每列新增一个值，所有这些值有着相同的storage key，这个key是在收到insert的node上分配的。每个node使用unique_id+local_counter来生成全局唯一的storage key。所有node的local counter初始值都大于任意RS的storage key。

WS目前使用了BerkeleyDB，未来考虑使用大的内存池来降低实际写磁盘的分摊开销。

C-Store中只读事务使用snapshot isolation，需要指定在low water mark（LWM）到high water mark（HWM）之间的一个effective time。这样只读事务就不需要加锁了。

Update事务使用传统的2PL。

### Providing Snapshot Isolation

对于一个只读事务选择的effective time（ET），一条记录当且仅当它的插入时间小于ET且删除时间大于ET时可见。为了减少空间开销，C-Store使用了epoch，每个epoch是一个固定长度的时间段，每个WS会用insertion vector（IV）保存一个epoch中的所有insert。Tuple mover能保证不会有记录在LWM之后插入RS，所以RS不需要有IV。类似地还有deleted record vector（DRV），每个projection中每个值都对应DRV中的一个值1或0，很明显DRV也可以用RLE编码。DRV也保存在WS中。

#### Maintaining the High Water Mark

C-Store定期向每个WS发送epoch+1，之后等所有WS的上一个epoch的请求都处理完，再向每个segment发送HWM=epoch。

C-Store会定期回收epoch以避免溢出，方法类似于TCP等协议。

### Locking-based Concurrency Control

C-Store中只有undo log，且实现了一种不严格的2PC（没有prepare），通过timeout实现了死锁检测。

#### Distributed COMMIT Processing

C-Store中事务通过一个master来分配给各个worker，但没有prepare阶段，当需要commit时，它会等所有worker完成当前任务，然后要求worker commit或abort这个事务。一旦node完成了这个事务，就可以释放锁，删除undo log了。如果有node在需要commit时还没记下来undo log就挂了，它会在recovery阶段通过其它projection恢复状态。

#### Transaction Rollback

C-Store记录logical log（statement），这样undo log的体积会远小于phisical log。在abort时它会从尾部开始扫描undo log，然后恢复状态。

### Recovery

如果node只是挂了，没有数据丢失，它只需要追上所有update就可以了。

如果node的WS和RS都没了，它需要通过其它projection和join index来恢复数据，其中WS还需要能从其它projection获取IV和DRV。

如果node的WS没了，但RS还在，只需要恢复WS。

#### Efficiently Recovering the WS

需要恢复的WS要先选择一组覆盖它所有列的projection，这些projection的last tuple move时间早于要恢复的WS（保证IV和DRV完整），然后通过一组SQL获取数据。

如果没有这样的projection（很罕见），WS要的数据就在其它RS中，可以要求tuple mover在执行时记录log，之后从log中恢复WS数据。

## Tuple Mover

> 与LSM类似，略。

## C-Store Query Execution

> 略

## Performance Comparison

> 略

## Related Work

> 略

## Conclusions

> 略
