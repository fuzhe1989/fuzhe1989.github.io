> 原文：[F1: A distributed SQL database that scales](https://research.google/pubs/pub41344/)

## TL;DR

F1是Google用于替换分库分表的MySQL的RDBMS系统。它在[Spanner](/2020/12/14/spanner-googles-globally-distributed-database/)之上建立了一套关系模型，拥有Spanner的跨datacenter的同步replication能力。F1还开发了新的ORM模型，从而将同步geo-replication导致的延时隐藏了起来，保证了E2E延时在替换后没有上升。

<!--more-->

## Introduction

F1的设计目标：
- 可伸缩性，要能把扩容变得平凡、业务无感知。
- 可用性，无论是datacenter中断，还是正常维护、schema变更，都不能停止服务。
- 一致性，必须提供ACID的事务，保证数据不出错。
- 易用性，要满足用户对于一个RDBMS的预期，要支持完整的SQL查询、二级索引、ad-hoc查询。

F1建立在Spanner之上，在继承Spanner原有功能（极致规模、同步replication、强一致、有序）之外，F1还增加了以下特性：
- 分布式SQL查询，包括与外部数据源做join。
- 事务一致的二级索引。
- 异步schema变更，包括db重新组织（TODO）。
- 乐观事务。
- 自动记录和发布表变更历史。

F1的设计选择导致了常规读写延时上升，因此F1使用了以下技术来隐藏掉延时，从而提供了与之前MySQL方案差不多的E2E延时：
- 使用分级的表结构和嵌套数据类型，显式将数据聚焦起来，提高了数据局部性，减少了RPC数量。
- 重度使用batch、并行、异步读，并通过新的ORM来体现这些特点。

## Basic Architecture

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/f1-01.jpg)

为了降低延时，F1 client和load balancer会优先选择离得最近的F1 server。

F1 server通常与对应的Spanner部署在相同datacenter中。但F1 server也可以访问其它datacenter的Spanner。Spanner的数据在CFS（Colossus File System）上，CFS是单datacenter的，因此Spanner不会访问其它datacenter的CFS。

F1 server通常是无状态的，除了client要执行悲观事务，此时F1 server会持有锁，client需要在事务期间保证与对应的F1 server的连接。

F1集群还会有slave pool来执行复杂的分布式query，这些机器由F1 master负责管理。

F1也支持将大规模的数据处理offload给MapReduce来执行，MapReduce会直接与Spanner通信获取数据，不需要再走一次F1。

因为Spanner的同步replication，一次commit的延时通常在50ms-150ms。

### Spanner

Spanner是与F1一同开发的，负责底层的数据存储，包括一致性、cache、replication、容错、数据分区与移动、事务等。

Spanner中数据被分为若干个directory，每个directory至少有一个fragment。每个paxos group包含若干个fragment。

（细节略，见[Spanner笔记](/2020/12/14/spanner-googles-globally-distributed-database/)）

Spanner提供了一个global safe timestamp，并保证不会有正在执行的事务的timestamp比它更小。gst通常落后当前时间5-10秒。小于gst的读可以由任意replica服务，也不会被正在执行的事务阻塞。

## Data Model

### Hierarchical Schema

逻辑上F1的表schema可以组织为树型结构。物理上F1会将子表与父表的行交织在一起，要求子表有一个foreign key是父表primary key的前缀（类似于Megastore）。比如Customer表的primary key是`(CustomerId)`，它的一个子表Campaign的primary key是`(CustomerId, CampaignId)`，再下层子表AdGroup的primary key是`(CustomerId, CampaignId, AdGroupId)`。root表的行称为root row，所有从属于某个root row的子表的行都与root row存储在Spanner的一个directory中。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/f1-02.jpg)

这种层级结构的好处：
- 将读某个root前缀下多个entity的操作由多次point query转为了一次range query。
- 可以用sorted join来处理两张子表间的join（相同primary key顺序），且不需要shuffle（都在一个directory中）。

这些性质能帮助F1缓解延时的上涨。

层级结构尤其对update有用：减少了涉及的paxos group数量，2PC数量大大降低，甚至可以避免2PC。

F1并不强制这种层级结构，平坦结构也是可以的。对于Ads而言，它的模式就是读写通常都以CustomerId为界限，这种层级结构有着非常明显的好处。

### Protocol Buffers

F1支持数据格式为Protocol Buffers（保存为一个blob），且经常在每行父表数据对应的子表数据不多时，将子表替换为父表中的一个repeated字段，从而避免了多张表的管理开销与潜在的join开销。此外使用repeated字段常常要比用子表语义上更自然。
