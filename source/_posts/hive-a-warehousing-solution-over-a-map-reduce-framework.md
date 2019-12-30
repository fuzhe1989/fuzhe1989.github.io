---
title:      "[笔记] Hive: a warehousing solution over a map-reduce framework"
date:       2020-09-29 21:30:09
tags:
    - 笔记
    - DataWarehouse
    - MapReduce
    - Hive
    - OLAP
    - Facebook
---

> 原文：[Hive: a warehousing solution over a map-reduce framework](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.303.2572&rep=rep1&type=pdf)

## TL;DR

Hive在MapReduce模型上实现了支持类SQL查询的数据仓库，它的改进点主要为：
- 自己维护了元数据，从而允许了其它的所有优化。
- 数据在物理上切分为partition和bucket，避免了扫描全量数据。
- 实现了各种operator，从而支持了大多数关系型操作。

<!--more-->

## Data Model

Hive中数据被组织为：
- Table：逻辑上为record序列，用户可以自定义序列化和反序列化方法。每个表对应HDFS上的一个目录。Hive也支持外部表。
- Partition：按若干列的值将表分成多个子目录，如`/wh/T`按列ds和ctry切分，其中一个子目录为`/wh/T/ds=20090101/ctry=US`。
- Bucket：每个partition的数据可以进一步按某一列的hash值分为若干个bucket，每个bucket为一个文件。

Hive支持基本类型以及array和map。

## Query Language

Hive使用HiveQL，支持`load`和`insert`，不支持`update`和`delete`。HiveQL支持一次select出来的结果给后续多个语句使用。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/hive-01.jpg)

## Architecture

### Metastore

Hive的Metastore需要随机读写的能力，因此不用HDFS存储，而是用其它DB或文件系统。

Metastore支持为每个partition设置序列化反序列化方法，这样可以平滑升级data schema。

### Compiler

Hive的compile流程：
- Parser将QL转换为parse tree。
- Semantic Analyzer将parse tree转换为基于block的内部query形式。它会读取metastore获取表的元数据，展开`select *`，检查列名、类型、进行隐式类型转换等。
- Logical Plan Generator将内部query形式转换为logic operator tree。
- Optimizer会按以下方式重写logic plan：
    - 将多个有相同join key的join合并为一个实现多路join的mr任务。
    - 增加repartition operator用于join、groupby、自定义的mr操作，它也是map和reduce的分隔线。
    - 尽早移除不需要的列，将predicate前推到离扫描更近的地方。
    - 去掉不需要的partition和bucket。

    用户可以用以下方式帮助Optimizer：
    - 针对数量特别多的groupby，提供部分聚合operator。
    - 针对有倾斜的groupby，提供repartition operator。
    - 在map阶段而不是reduce阶段执行join。
- Physical Plan Generator将logic plan转换为physical plan。它先基于分隔operator（repartition和union all）将logic plan分为若干部分，再将每部分转为map或reduce任务。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/hive-02.jpg)
