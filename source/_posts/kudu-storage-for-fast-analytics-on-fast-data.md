> 原文：[Kudu: Storage for fast analytics on fast data](https://kudu.apache.org/kudu.pdf)

## TL;DR

<!--more-->

## Introduction

很多Hadoop的用户会使用HDFS上的Parquet或Avro文件做OLAP，使用HBase做OLTP，但没办法结合起来。前者顺序写很厉害，但不支持实时更新，后者随机写很厉害，但分析性能差。一种折衷的方案是数据和修改流式写入HBase，再定期导出为HDFS上的Parquet文件。但这样的架构会有以下问题：
- 应用端要写复杂的代码维护两套系统。
- 要跨系统维护一致性的备份、安全策略、监控。
- 数据可见性差：更新进入HBase到最终能被查询到的延时可能很久。
- 实际场景中经常有要修改已经持久化到HDFS的文件的需求（如迟到的数据、订正等），需要做高开销的文件重写，还可能要人工介入。

Kudu是一种可以弥补HDFS与HBase间的功能gap的存储系统。它同时提供了行级别的insert、delete、update，和类似Parquet的列存数据扫描。

## Kudu at a high level

### Tables and schemas

Kudu中数据组织为表，每张表都要有schema，其中primary key是有序且唯一的。用户可以使用`ALTER TABLE`来增加或移除某些列（除了primary key）。

Kudu与HBase的一个区别在于它的列是有类型的，好处有两点：
1. 允许Kudu做类型特定的压缩，如int可以用bitpacking。
1. 方便与BI系统交互。

Kudu目前还不支持二级索引，或非primary key的列的唯一性保证。

### Write operations

Kudu本身只暴露了行级别的insert、update、delete接口，必须要指定完整的primary key。条件更新需要通过更上层的Impala等系统来实现。

目前Kudu也不支持多行事务，但保证单行写一定是原子的。

### Read operations

Kudu只支持scan这一种读操作，scan时用户可以传入若干个谓词，目前支持两种谓词：比较一列和一个常量，或primary key range。此外用户可以指定需要的列（projection）。

### Other APIs

Hadoop生态的很多系统（如MapReduce、Spark、Impala）都可以利用数据局部性，因此Kudu允许用户指定某个key range的数据存在特定server上。

### Consistency Model

Kudu提供了两种一致性，snapshot（默认）与read-your-write。

默认情况下Kudu不提供外部一致性，即client A写了一笔，外部通知client B，B可能看不到这笔写。解决方案是client A可以要求前面一笔写返回一个timestamp，将它传给B，B用这个timestamp去读。

另外Kudu还支持类似Spanner的commit-wait，但如果没有专门的硬件的话，commit-wait可能引入非常高的延时（默认的NTP配置下100-1000ms）。

### Timestamps

## Architecture

### Cluster roles

### Partitioning

### Replication

### The Kudu Master

## Tablet storage

### Overview

### RowSets

### MemRowSet Implementation

### DiskRowSet Implementation                                                                                       

### Delta Flushes

### INSERT path

### Read path

### Lazy Materialization

### Delta Compaction

### RowSet Compaction

### Scheduling maintenance
