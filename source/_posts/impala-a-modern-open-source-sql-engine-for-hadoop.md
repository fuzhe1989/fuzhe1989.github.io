---
title:      "[笔记] Impala: A Modern, Open-Source SQL Engine for Hadoop"
date:       2020-12-23 11:52:41
tags:
    - 笔记
    - OLAP
    - Codegen
    - 开源系统
---

> 原文：[Impala: A Modern, Open-Source SQL Engine for Hadoop](https://2013.berlinbuzzwords.de/sites/2013.berlinbuzzwords.de/files/slides/Impala%20tech%20talk.pdf)

**TL;DR**

Impala是建立在Hadoop生态之上的MPP query engine，它有以下特点：
- MPP，所有节点都可以参与query执行。
- 代码生成。
- 基于pub-sub的元数据同步。

<!--more-->

## User View of Impala

Impala是建立在Hadoop生态之上的MPP query engine，用到了多种Hadoop标准模块（Metastore/HDFS/HBase/YARN/Sentry）并提供了类似于RDBMS的体验。

Impala瞄准的是标准BI场景，用户可以通过ODBC/JDBC访问Impala。

### Physical Schema Design

用户可以在建表时提供一组partition列：

```sql
CREATE TABLE T (...) PARTITIONED BY (day int, month int) LOCATION '<hdfs-path>' STORED AS PARQUET;
```

类似于Hive，未分区的表的数据文件直接存放在表的根目录下，有分区的表的数据文件则存放在分区对应的子目录下，如`<root>/day=17/month=2/`。

Impala支持多种文件格式，如文本文件（压缩/未压缩）、sequence file、RCFile、Avro、Parquet。用户可以在`CREATE TABLE`或`ALTER TABLE`时指定文件格式，还可以指定具体受影响的分区，如`ALTER TABLE PARTITION（day=17, month=2) SET FILEFORMAT PARQUET`。

### SQL Support

Impala支持：
- 大多数SQL-92标准的SELECT语句；
- SQL-03标准的聚合函数；
- 大多数标准标量类型；
- Java/C++写的UDF和C++写的UDA。

在写入方面，受限于HDFS，Impala只支持批量插入（`INSERT INTO ... SELECT ...`），但不支持`UPDATE`和`DELETE`。但与传统RDBMS不同的是，用户还可以直接把数据文件拷贝进HDFS目录，或者通过`LOAD DATA`语句导入数据。`ALTER TABLE DROP PARTITION`还可以直接删除某个分区的数据。

在表数据发生比较大变化之后，用户可以执行`COMPUTE STATS <table>`，让Impala重新收集统计信息。

## Architecture

Impala在每台机器上有一个impalad进程，负责接收请求以及将请求分解发给整个集群执行。接受请求的impalad会作为coordinator。所有impalad是平等的，可以完成所有任务。

Impala在分发请求时会考虑数据的局部性，优先让impalad读取它所在的datanode存储的数据。

Statestore服务（statestored）是Impala的元数据pub-sub服务，负责将整个集群的元数据传播给所有Impala进程。

Catalog服务（catalogd）负责管理Impala的元数据，数据的修改会通过statestore传播出去。

![](/images/2020-12/impala-01.jpg)

### State distribution

节点数量增加时，整个集群元数据同步与协调成本也会上升。Impala的MPP架构要求所有节点都有服务能力，就需要每个节点都能看到正确的集群状态，如节点列表等。

Impala的一个设计信条是避免在关键路径上有同步RPC，因此没有选择用一个单独的元数据服务的方案。相反，Impala使用了statestore来把元数据的修改推送给感兴趣的节点。

statestore会维护若干个topic，每个topic的数据格式为`(key, value, version)`。statestore不感知这里具体的数据类型，也不会持久化这些数据。节点在初次订阅某个topic时，statestore会把全量数据推送过去，之后会定期推送两类数据：更新和keepalive。

如果statestore检测到某个订阅者宕机（连续的keepalive失败），它会停止发送更新。有些topic entry会被标记为“transient”，它们会有从属的订阅者，如果订阅者宕机了，这些entry也会跟着被移除。

statestore只提供弱语义，不同的订阅者收到更新的速率可能不同，看到的topic状态也可能不同。但Impala的节点只使用topic中的元数据做本地决策，而不会与其它节点协同决策。比如impalad会使用catalog的元数据生成query plan，一旦plan生成好了，执行它所需的所有信息也会直接发给执行节点，因此不要求执行节点一定要看到与coordinator相同的topic状态。

单个statestore节点足够支撑中等大小的集群了。statestore不持久化数据，当它启动时会在与订阅者的初次通信过程中恢复状态。

### Catalog service

catalog service负责管理表的元数据，并通过statestore发布。它在启动时只会加载表的元数据的一个大概情况，后续如果还有访问的话再异步加载完整的元数据。

## Frontend

Impala的frontend负责解析SQL并生成query plan。它有一个基于cost的优化器。除了基本的SQL特性（select/project/join/group by/order by/limit）外，Impala还支持inline view、非关联和关联的subquery、各种outer join、left/right/semi join、anti-join、分析窗口函数等。

query生成过程分为query解析、语义分析、生成plan、优化几个阶段。

执行plan生成需要先生成单节点的plan，再做分解和并行化。

单节点plan树包含以下节点：
- HDFS/HBase scan
- hash join
- cross join
- union
- hash聚合
- sort
- top-n
- 分析求值

这一阶段需要尽可能下推谓词、基于等价类做谓词推断、裁剪partition、设置limit/offset、执行projection、执行一些基于cost的优化（如排序合并分析窗口函数、重排列join）。开销预测是基于table/partition的基数与每列的不同值数量（使用HyperLogLog），目前还没有统计直方图。Impala使用了启发式方法，避免在整个join空间内枚举和计算。

第二阶段会将单节点plan作为输入，生成分布式执行plan，原则是最小化数据移动，最大化扫描的数据局部度。向plan的节点中间插入exchange节点以使plan分布化，插入非exchange节点（如部分聚合节点）以最小化数据移动。这阶段所有join的策略都会被确定下来，目前支持broadcast和partitioned join（两边都hash分区）。

所有聚合都会先在各个节点上预聚合，跟着一个最终的聚合。sort和top-n也使用类似的策略。

最后plan树会在exchange处切分为若干个fragment，作为backend的执行单位。

![](/images/2020-12/impala-02.jpg)

## Backend

Impala的backend是C++写的（frontend是Java），使用了代码生成（codegen）。它的执行方式是传统的Volcano风格，但每次`GetNext()`会返回一批record，而不是一个。除了sort等要hold数据的操作，其它操作都是完全流水线化的，最小化内存占用。在内存中处理期间，数据是按行组织的（提前组装）。

在需要的时候，算子也可以把过多的数据暂存到磁盘上。Impala在hash join的时候会把内存中的hash table分为若干个partition，当内存吃紧的时候会把一些partition暂存到磁盘上。

在构建hash table过程中，Impala也会生成bloom filter，这样能避免探测表传输太多数据。

### Runtime Code Generation

Impala使用了LLVM来编译生成有JIT的代码，在一些典型场景中性能提升能达到5倍以上。代码生成的好处是可以去掉那些考虑到通用性的分支、虚函数、变量、循环、各种指针等，在编译过程中可以充分inline。Impala会对那些运行在内层循环中的函数使用codegen，比如将数据解析为内存格式的函数会per record调用，对它使用codegen是收益非常大的。

![](/images/2020-12/impala-03.jpg)

![](/images/2020-12/impala-04.jpg)

### I/O Management

Impala会使用HDFS的short-circuit本地读取技术，绕过datanode的协议，直接读本地盘，从而充分利用磁盘带宽。HDFS的cache也帮助Impala减少了磁盘和cpu消耗。

为了能匹配不同盘的能力，Impala会为每块HDD配一个线程，每块SSD配8个线程。

### Storage Formats

Impala推荐使用Parquet作为文件格式（细节略，参见[Dremel笔记](/2020/09/22/dremel-interactive-analysis-of-web-scale-datasets)）。

## Resource/Workload Management

略
