---
title:      "[笔记] An Integrated Approach to Recovery and High Availability in an Updatable, Distributed Data Warehouse"
date:       2020-12-14 12:22:39
tags:
    - 笔记
    - OLTP
    - Google
    - Paxos
    - 2PC
---

> 原文：[An Integrated Approach to Recovery and High Availability in an Updatable, Distributed Data Warehouse](https://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.481.3000&rep=rep1&type=pdf)

## TL;DR

这篇文章介绍了HARBOR，一种针对高可用数仓的新的恢复与高可用机制。相比传统的基于ARIES的系统，HARBOR不使用log，而是利用冗余replica，在恢复时query其它replica来恢复数据。

<!--more-->

ARIES的瓶颈在于必须同步写的log
-> 宕机怎么处理：可以query其它active的replica来恢复数据。
-> 怎么知道要恢复哪些数据：通过monotonic timestamp。
-> 如何减少需要恢复的数据量：定期做checkpoint。

不同replica之间只通过SQL query传递数据，这种松耦合的关系允许不同replica有不同的数据组织方式，如行/列格式、列排序、压缩编码、视图等都不需要相同（受到了C-Store的启发）。这样每个query可以由最适合的replica服务，进一步提升了性能。

HARBOR也使用了MVCC，每个replica会维护一个时间T，保证所有活跃的transaction都不早于T，这样读T之前数据的query（historical query）就不需要锁机制保护了。这种机制也是源自C-Store。

具体到实现上，每个tuple有insertion和deletion两个timestamp，T时间可见的tuple需要满足insertion ts <= T < deletion ts。

HARBOR的容错机制也源自C-Store，称为K-safety，即最多允许K个replica同时宕机，最小需要K+1台机器。K-safety的前提是至少有一个replica是完整的，这就对恢复速度有要求。如果上一轮宕机还没恢复完，其余replica又宕机了，或者有超过K个replica宕机，则此时需要依赖checkpoint或冷备数据来恢复。

值得一提的是，HARBOR假设所有错误都是fail-stop错误，不处理拜占庭问题、网络分区（network partition）、数据损坏（HARBOR的恢复机制处理不了部分损坏）、磁盘页不完整等。（容错机制制约了HARBOR的实用性）

恢复过程：
1. 每个checkpoint对应一个safe timestamp T，小于等于它的所有commited修改都保证已落盘。实现上是在做snapshot的时候会强制刷dirty page。
1. 使用T去其它active replica上执行historical query，因为historical query不需要加锁，这个操作不会影响被读的active replica上的事务执行。
1. 从其它active replica上获得最新数据，需要加锁，但因为数据量不多，影响会比较小。在这一阶段coordinator会重新将请求发往正在恢复的节点，包括它宕机期间coordinator上缓存的请求（要求coordinator高可用，有难度/开销）。

coordinator会有一个内存队列，里面是所有未执行完的transaction。coordinator负责为transaction分配insertion和deletion timestamp，因此HARBOR需要各节点间有某种时间同步机制（此时HLC还未问世）。另外HARBOR中会有一个coordinator定期提升所有节点的epoch，同时也提升了historical query可读的范围。

HARBOR中未提交的transaction不会落盘或生成timestamp，worker会在内存中hold这些未提交的transaction的修改，同时作为undo log。如果buffer pool支持STEAL（dirty page不需要等到所有相关的transaction都提交就可以刷下去），则这些修改的insertion ts会是一个特殊值，在恢复时会被忽略掉。


