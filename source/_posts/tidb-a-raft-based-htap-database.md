> 原文：[TiDB: a Raft-based HTAP database](https://dl.acm.org/doi/abs/10.14778/3415478.3415535)

## TL;DR

<!--more-->

## Introduction

（前面阐述HTAP的意义）HTAP除了像NewSQL系统一样要实现可伸缩、高可用、事务一致性，还有两个重要属性：新鲜度（freshness）和隔离性（isolation）。

新鲜度指OLAP能看到多新的数据。基于ETL的HTAP方案保证不了实时新鲜度，甚至经常要延迟几小时以上。将ETL换成流计算能极大改善新鲜度，但这种方案涉及多个模块（OLTP、流计算、OLAP），维护代价高。

隔离性指OLTP和OLAP的请求互不影响。一些内存数据库（如HyPer）允许一个server内的AP请求访问到最新的TP数据，但这就存在相互影响，没办法让TP和AP吞吐都达到最高（实际上有相当明显的影响）。

让TP和AP使用不同的硬件资源（如不同server）能解决隔离问题，但问题在于如何保证用于AP的replica有着和用于TP的replica一样新的数据。注意到保证replica数据一致与高可用一样，都需要用到共识协议，如Paxos和Raft。

TiDB的idea就来自这里，它使用了Raft来同步replica，其中AP replica身份为learner，只接受数据，不参与投票，且在收到数据后将行存格式转为列存格式。通过Raft保证了AP replica能看到最新的数据；通过引入learner，保证了AP replica对TP replica只有最小的影响（每多一个AP replica，多一次RPC的吞吐，但基本不增加延时）。

## Raft-based HTAP

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/tidb-01.png)

从high level来看，数据保存在若干个Raft group中，其中每个leader和follower的数据是行存的，用来服务TP请求，learner数据是列存的，用来服务AP请求。query优化器会考虑两种引擎来生成query plan。

以下是TiDB解决的几个工程问题：
1. 如何构建一个可伸缩的支持高并发读写的Raft存储系统？如果数据量超过了Raft各节点的可用空间，需要有办法将数据分片分散到各个server上。另外在基本的Raft协议中，请求是串行执行的，在回复client前必须要得到多数Raft节点的确认。这个过程引入了网络和磁盘操作，会导致leader成为系统的瓶颈。
1. 如何以低延时将数据同步给leaner？正在执行的事务可能产生很大的log，这些log需要快速在learner上重放、持久化。将log转换到列存的过程中可能因schema不匹配而出错，导致log同步延迟。
1. 如何同时高效处理TP和AP query且保证性能？大的TP query需要读写位于多个server上的大量数据，而AP query也会消耗大量资源，不应该影响到TP请求。需要能综合考虑行存和列存store来生成最优化的plan。

## Architecture

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/tidb-02.png)