> 原文：[The snowflake elastic data warehouse](https://dl.acm.org/doi/abs/10.1145/2882903.2903741)

## TL;DR

<!--more-->

## Introduction

云时代来临了，但传统的数仓方案还是在云时代之前的，只能跑在小型的、静态的、指定机器的集群上，而没办法真正利用云设施。

除了平台在改变，数据也在改变。过去数仓要处理的数据都来自相同组织内部，它们的结构、容量、产生速度都是可以预期和规划的。但云数仓的数据来自不容易控制的外部，而且经常是没有schema的半结构化数据。面对这些数据时，传统数仓力不从心，它们严重依赖ETL pipeline和根据已知的数据特点来手动调优。

后续产生的Hadoop、Spark、Stinger Initiative等方案仍然无法满足云时代对数仓的需求，尤其是它们都需要花费大量人力做运维和调优。

Snowflake与这些方案的区别在于它是真正利用云设施的经济、弹性、服务化等优点的数仓产品。它的关键特性如下：
- 纯正的SaaS体验。用户不需要关心机器、运维、调优、扩容，只要数据上传到Snowflake，立即就可以开始分析。
- 支持SQL和ACID的事务。
- 支持JSON和Avro之类的半结构化数据，可以遍历、摊平、嵌套，利益于自动的schema发现和列存格式，操作半结构化数据的效率与结构化数据一样高。
- 弹性的存储与计算资源。
- 高可用。
- 数据可靠性。
- 费用合理，用户只需要为存储（压缩后）和使用的计算资源付费。
- 安全性，所有数据和网络都是端到端加密的，用户还可以基于role来做细粒度访问控制（SQL级别）。

这篇文章中Snowflake使用的是AWS的资源，目前它也已经支持了Azure和GCP。

## Storage Versus Compute

shared-nothing架构在高性能数仓领域占据了主导地位，它的优点是规模与硬件便宜。这种设计很适合星型query，在join一个小的维表（broadcast）和一个大的事实表（partition）时只需要很小的带宽。

但这种shard-nothing架构有个严重缺陷：计算与存储耦合。以下场景会有问题：
- 异构负载。一种系统配置很难同时满足批量导入（高I/O带宽，低运算）和复杂query处理（低I/O带宽，高运算），最终可能两种都不高效。
- 扩缩容时会有大量数据需要搬迁，过程中现存节点也会受影响。
- 在线升级。想要完全不影响服务，理论上可以，但实际很难，尤其是各种资源耦合在一起，又有着同质化假设时。

云环境下这些场景都会出现。

因此Snowflake选择了计算存储分离的架构，其中计算由Snowflake所有的shared-nothing机器提供，存储则使用AWS S3。每台机器上还会在本地盘上缓存数据。如果cache是热的，这种架构的性能可以达到甚至超过shared-nothing系统。

## Architecture

Snowflake的目标是成为企业级的服务，除了易用性和相互操作性之外，还需要有高可用性。整个Snowflake分为三层：
- 存储层是AWS的S3.
- 虚拟数仓层（virtual warehouse）负责在vm集群上执行query。
- 云服务层，包括了管理VW、query、事务的服务，以及管理元数据的服务。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/snowflake-01.jpg)

### Data Storage

Snowflake选用AWS的原因是：AWS是最成熟的云平台；AWS有着最多的潜在客户。

在S3与自建HDFS的选择中，Snowflake发现S3虽然性能不太稳定，但它的易用性、高可用、强数据可靠性都是很难被替代的。因此Snowflake转而将精力花在了VW层的本地cache和弹性处理数据倾斜的技术上了。

S3有以下特点：
- 单次访问延时高，CPU消耗大，尤其是使用HTTPS时；
- 文件只能覆盖写，不能追加；
- GET支持只读部分文件；

这些特点严重影响了Snowflake的文件格式和并发控制策略的设计。表被水平分为若干个大的、不可变的文件，每个文件内部使用PAX风格的结构，分为若干个row group，row group内部各列单独存为block。每个文件有一个header，包含了各列的偏移。query只需要读走header和想要的列就可以了。

S3不止用来保存表文件，还会在本地盘用满时保存query算子产生的临时文件。这种设计还增加了一种client间的互动方式，并且简化了query处理，因为不再需要像传统数据库那样在server端维护query的游标了。

元数据则保存在一个可伸缩的事务性的kv store中，属于云服务层的一部分。

### Virtual Warehouses

### Cloud Services

