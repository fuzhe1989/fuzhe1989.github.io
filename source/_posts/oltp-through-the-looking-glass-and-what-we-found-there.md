---
title:      "[笔记] OLTP Through the Looking Glass, and What We Found There"
date:       2020-09-01 12:23:11
tags:
    - 笔记
    - Database
    - OLTP
    - 探索研究
---

> 原文：[OLTP Through the Looking Glass, and What We Found There](http://pages.cs.wisc.edu/~yxy/cs839-s20/papers/hstore_lookingglass_2008.pdf) 

本文观点：随着硬件的发展和需求的变化，OLTP需要有新的设计；测量传统OLTP架构中各部分对性能的影响。

<!--more-->

## Introduction

自从70年代以来OLTP系统的架构就没有太大的变化，但两个趋势使重新设计OLTP系统变得必要：
1. 硬件的发展。CPU算力的发展使一个事务只需要几ms或更短时间；内存的廉价化使很多DB可以整个放到内存中。
1. 需求的变化。很多应用不需要那么完整的事务支持。

本文测试了以下模块：
- logging，包括undo和redo log。
- locking，指2PL。
- latching，指具体的各种mutex。将事务处理改成单线程就可以去掉很多mutex。
- buffer management，指page和cache管理。

![TPC-C New Order场景指令数分布](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/oltp_f1.jpg)


## Single Threading in OLTP Systems

如果内存足够大，OLTP系统不再需要多线程：
- 不再有disk wait。
- 不再有user wait：事务几乎都使用存储过程。
- OLTP事务足够简单，几百ms就可以完成；长时间的事务主要由OLAP服务。

为了充分利用多核资源，可以使多进程+单线程，每个进程是一个site。

网络可能是个瓶颈，但很多应用可以将事务范围限制在一个site内，从而避免网络影响。

## High Availability vs. Logging

[LM06](http://www.vldb.org/conf/2006/p703-lau.pdf)表明至少在data warehouse中，使用standby足以恢复数据。[SMA+07](http://acm.mementodepot.org/pubs/books/3226595/3226595.3226637/3226595.3226637.pdf)表明OLTP系统也可以这样，通过其它节点而不是redo log恢复数据。

## Transaction Variants

一些应用认为可用性比事务语义更重要，因此更倾向于最终一致性。snapshot isolation的流行也表明用户愿意放宽事务语义来换取性能。

一些研究表明有些限制了用法的事务可以显著简化事务：假如所有事务都先执行完所有读再执行所有写，且保证在读阶段结束后不abort，就不需要undo log了。

## Removing Components

移除logging：
1. 避免执行过程中产生I/O请求：group commit，增大buffer size以保证过程中不flush。
1. 注释掉所有准备和写log record的代码。
1. 避免产生LSN。

移除locking：
1. 修改LockManager，所有调用直接返回。
1. 修改钉住record的函数，在一个目录下查找record，并通过B-tree索引访问。

移除latcing：
1. 修改所有mutex，所有调用直接返回。
1. 避免请求mutex。

移除buffer manager调用：
1. 创建record时直接malloc，而不是通过page allocation。
1. 没有直接移除buffer manager的代码，而是尽量简化了逻辑。

其它优化：
1. 单独优化了B-tree的key为未压缩的整数的场景（对应下图中的“Btree keys”）。
1. 为目录查找增加了一个cache（对应下图中的“dir lookup”）。
1. 增大page size到32KB（对应下图中的“small page”）从而降低B-tree高度。
1. 通过将所有事务放到一个session中，移除了每个事务启动和结束session的开销（对应下图中的“Xactions”）。

## Performance Study

![Payment场景指令数分布](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/oltp_f5.jpg)

![New Order场景指令数分布](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/oltp_f6.jpg)

![New Order场景指令数详细分布](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/oltp_f7.jpg)

![New Order场景指令数与周期数对比](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/oltp_f8.jpg)

两个观察：
1. 单个部分的优化效果并不明显，最明显的也不过30%。
1. 多个优化一起的效果就很明显了。一个单线程处理事务、所有事务都是读写两阶段的、通过其它节点恢复数据的OLTP系统与当前的系统会非常不一样。

## Implications for Future OLTP Engines

1. 并发控制。测试结果表明动态locking对性能影响非常大，但不是所有系统都可以串行执行事务，因此最佳的并发控制方法仍然很有研究价值。
1. 多核支持。多线程导致需要使用latch，实现高效的多线程并发系统需要新的设计。也可以通过虚拟化来实现每个site一个核，单线程。另外还可以考虑单个query内并行执行。
1. 传统的replication是基于log的active-passive架构，它的缺点是：除非使用2PC，否则secondary与primary很难保持事务一致，因此secondary没办法服务需要事务一致的读请求；failover不是立即生效的；logging对性能有影响，占用了20%左右的周期数。基于log的传统设计的考虑是在secondary上恢复log的成本要远低于执行事务。但基于内存的系统的执行事务成本也是很低的，允许我们使用多个active secondary，每个secondary同步执行事务。
1. 弱一致性。最终一致性加上事务一致性。
1. cache友好的B-tree。
