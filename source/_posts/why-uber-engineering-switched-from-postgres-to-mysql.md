---
title:      "Why Uber Engineering Switched from Postgres to MySQL"
date:      2021-04-22 20:51:05
tags:
    - Engineering
    - Database
    - MySQL
    - PostgreSQL
---

> 原文：[Why Uber Engineering Switched from Postgres to MySQL](https://eng.uber.com/postgres-to-mysql-migration/)

Uber之前一直用Postgres作为DBMS（最终版本是9.2），后来遇到了很多困难，最终迁移到了MySQL/InnoDB上。

<!--more-->

导致问题的是以下Postgres的核心设计：
1. row（tuple）是不可变的，每次更新会写入新的tuple，产生新的ctid。
1. primary和secondary index引用的是ctid。
1. 主备之间replication传输物理log（WAL）而不是逻辑log。
1. 依赖于OS的page cache，自身只维护非常小的内部cache。
1. 多进程（per-connection）而不是多线程。

这里面引起问题最多的就是物理replication协议。后续PG增加了pglogical，但文章中表示pglogical还没有合入主线，只能是辅助使用。

## 严重问题

**写放大**

P1和P2导致每次写除了要写heap file外，还需要更新1个primary index和N个secondary index（ctid变了），无论这次更新的列是否与索引有关。

MySQL没有heap file，数据直接存在primary index中，每次修改只产生delta，secondary index只索引primary key。这些设计都显著降低了写放大。

**Replication放大**

P3（传输WAL）可以时刻保持主备文件级别一致，有问题了甚至可以直接`rsync`修复。但它太过于底层，包含很多不必要的数据，导致了网络流量放大。在跨datacenter的replication中这就成问题了。

另外每笔写入都导致索引跟着更新，都在WAL里，也增大了replication的流量。

MySQL支持三种replication模式：
- 语句级别：`UPDATE users SET birth_year=770 WHERE id = 4`。
- 行级别。
- 两者混合。

语句级别最精简，但replica还需要重新执行，开销较大；行级别数据量较大，但更精确，重要的是只需要传输主表的逻辑修改，replica自己会应用到index上。这就显著降低了replication的流量。

**数据损坏**

Postgres 9.2有个bug，replica可能会漏掉一些WAL。因为replication传输的是物理修改，漏掉一条log会导致整个B-tree都有错位的风险，可能整个DB会挂掉。而这样的检测又非常困难。

MySQL的replication传输逻辑修改就没有这种风险了，顶多是某行出错，不会有不相关数据跟着出错的风险。

**Replica不支持MVCC**

Postgres的replica不真的支持MVCC，当replica上有query执行时，为了避免后续修改破坏它的snapshot，它会持有相关的行，不让其被修改。这就导致replication可能被阻塞（WAL线程会等到相关事务结束）。为了避免无限等待，Postgres会在WAL被阻塞太长时间后kill掉对应的事务。

这种设计导致两个问题：
1. replica随时可能落后于master相当长（分钟级别）的时间。
1. 用户端写代码变困难，因为事务不知道什么时候就会被kill。

**升级困难**

Postgres的replication导致在开启了主备之后，跨版本升级变得特别困难：主备如果版本不同，物理格式可能不兼容。

作者总结的升级步骤：
- 停掉master。
- 在master机器上运行`pg_upgrade`升级数据格式，可能需要好几个小时，过程中master不能服务。
- 启动master。
- 创建一个master的snapshot，这一步需要物理复制master的所有文件，也可能需要好几个小时。
- 停掉并清空每个replica的文件，再将master的snapshot复制过去。
- 加回每个replica，等待追上master的进度。

一看这个步骤就太吓人了，Uber做了一次9.1到9.2的升级，之后就再也没动了Postgres的版本了。

而MySQL的逻辑replication的设计使它可以几乎无停服地升级master和所有replica。

## 其它问题

**无法利用大内存**

Postgres自身管理的cache通常比较小，因此在大内存机器上它依赖于OS的page cache来提升性能。

但访问page cache的开销远大于访问进程内部的cache，原因是要经过系统调用，且无法实现自定义的换出策略。

MySQL则会主动管理大cache，这样做的缺点是可能增加TLB miss，但可以通过使用huge page缓解。

**多进程**

Postgres会为每个connection创建一个新进程，但相比多线程，这种设计有以下缺点：
1. 无法支持高连接数。
1. 进程间通信开销远大于进程内通信。