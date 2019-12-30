---
title:      "[笔记] Cassandra: a decentralized structured storage system"
date:       2020-09-23 16:54:37
tags:
    - 笔记
    - Cassandra
    - NoSQL
    - KV
    - 开源系统
---

> 原文：[Cassandra: a decentralized structured storage system](http://www.cl.cam.ac.uk/~ey204/teaching/ACS/R212_2014_2015/papers/lakshman_ladis_2009.pdf)
>
> 可以参考[Cassandra: structured storage system on a P2P network](https://static.last.fm/johan/nosql-20090611/cassandra_nosql.pdf)加深理解。

<!--more-->

## Introduction

Cassandra的目标是：
- 伸缩性，集群能承载的吞吐随着规模线性增加。
- 容错性，大规模集群出错是常态。
- 可用性，即使出现网络分区，也要能提供服务（类似于Dynamo）。
- 跨机房数据复制。
- 高写入吞吐。

Cassandra的目标应用是Facebook的Inbox搜索，具有写入压力大、跨地域、不需要强一致等特点。

## Data Model

Cassandra的数据模型类似于BigTable，也是`<rowkey, column, timestamp>`，ColumnFamily和SuperColumn的叫法不同，但其实是一样的。

> 后续版本中SuperColumn已经没有了，叫法不同，没有根本变化。

## System Architecture

Cassandra在节点间的机制上类似于Dynamo，同样去中心化、使用一致性hash、使用gossip协议来传播成员列表、读写使用quorum协议。在节点内的存储上类似于BigTable，同样使用了WAL、MemTable、Compaction。

### Partitioning

Cassandra使用了保序的一致性hash算法。

为了解决原始的一致性hash分布不均匀的问题，Dynamo为每个node分配了多个virtual node，而Cassandra则是根据负载情况微调（参考Chord），原因是简单。

> 后续版本中Cassandra也使用了virtual node。

### Replication

Cassandra中每个key会有一个对应的coordinator node来管理，除此之外还需要选择N-1个node用于replication。选择的策略有很多种，可以考虑rack或datacenter。Cassandra会使用ZooKeeper选出一个leader来管理这些信息。

### Membership

Cassandra使用了[The ϕ Accrual Failure Detector](https://dspace.jaist.ac.jp/dspace/bitstream/10119/4784/1/IS-RR-2004-010.pdf)方法来检测哪些node有问题。它也是基于gossip协议的，每T秒每个node会选择另一个node通信，然后更新本地的统计数据，并通过gossip协议与其它node交换。

Accrual Failure Detector的特别之处在于它是基于概率的，ϕ为1表示判断出错概率为10%，2表示出错概率为1%，3表示出错概率为0.1%。当阈值设置为5时，Cassandra可以在10-15秒内检测到错误。

### Bootstrapping

node首次启动时，会在环上随机找个位置，并持久化到本地和ZooKeeper上，随后通过gossip将自身的存在传播到整个集群。一个集群最初会有若干个contact points，称为seeds，保存在配置文件中或ZooKeeper上，这是node首选的位置。

### Scaling the Cluster

新node会分走已有node负责的部分数据，这部分数据会通过kernel-kernel复制，未来可能考虑同时从多个replica并行传输以提高速度。

### Local Persistence

这部分Cassandra与BigTable非常类似，Cassandra会在内存中维护一些索引以加速查询。

### Implementation Details

Cassandra在读写上使用quorum协议，写入可以选择同步写或异步写。写失败和读的时候都可能触发repair。
