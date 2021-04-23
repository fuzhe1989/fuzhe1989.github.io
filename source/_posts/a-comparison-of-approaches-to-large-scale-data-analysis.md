---
title:      "[笔记] A comparison of approaches to large-scale data analysis"
date:       2020-09-17 21:40:22
tags:
    - 笔记
    - Database
    - MapReduce
    - 探索研究
---

> 原文：[A comparison of approaches to large-scale data analysis](https://people.eecs.berkeley.edu/~kubitron/cs262/handouts/papers/p165-pavlo.pdf)

**TL;DR**

本文的背景是随着MapReduce的兴起，越来越多的人抛弃了传统的DBMS，直接使用MapReduce来做数据分析。本文对比了MapReduce（Hadoop）、Vertica（列存的并行DBMS）、DBMS-X（某个商用的行存的并行DBMS）的性能，指出并行RDBMS仍然有它的优势，不会被MapReduce取代。

并行DBMS的优势：
- 速度远快于MapReduce（热数据、有组织、强大的优化器）。
- 更少的代码（SQL的表达能力优势）。

劣势：
- 冷数据的加载时间长。
- 容错能力弱（任意子query失败都会导致 整个query失败）。
- 灵活性差，难以处理非关系型数据。

但本文指出，并行DBMS可以用更少的节点完成同样的任务，因此它不一定需要MapReduce那么强的容错能力。

<!--more-->

## 并行DBMS

并行DBMS可以并行处理的两个关键点：
1. 大多数表分片由多个节点服务。
1. SQL优化器可以生成跨节点的查询计划。

如果filter涉及了partition key，或者两个表join的key恰好是各自的partition key，则查询计划涉及的节点数会大大减少。

join中的一张非常小的表可以冗余分布在多个节点上。如果join双方都比较大，则可以将其中一张表的数据hash后加载到另一张表涉及的节点上。

## 架构

- schema：MapReduce看似不需要schema，很多人认为这是它的优点。但当MapReduce的输出会被多个应用共享时，它仍然需要一个schema，这个schema要放到一个大家都能看得到的地方，与数据分离。而且不同于DBMS，每个应用要自己操心如何管理维护schema。
- index：简单的MapReduce任务不需要index，但在需要的时候，每个开发者都要自己写一遍。
- 编程模型：SQL模型比MR更高级，不需要开发者手写很低级的逻辑，因此MR社区又在MR模型之上引入了一些更高级的模型。
- 数据分发：大规模DB的经验是将计算发送到数据端，而不是反过来。并行DBMS可以利用已有的统计数据，生成最优化的查询计划，最小化需要传输的数据量。而MR需要手写来实现。
- 执行策略：MR的一大隐患是Map和Reduce任务间的数据流是M*N的，一旦任务数非常多的时候，会生成非常多的小文件。并行DBMS不会物化split文件，且用push而不是pull模式来传输数据。
- 灵活性：MR显然是更灵活的，但DBMS通过UDF、存储过程、自定义聚合函数等特性也弥补了不少的差距。
- 容错性：MR可以重试出错的子任务，而通常并行DBMS在某个子query失败时会令整个query失败。

## 性能

文章关注了两个阶段的性能：数据加载、执行。

无论是哪类任务，数据加载阶段MapReduce有着非常明显的优势，符合预期。

执行阶段，两种DBMS都明显比MapReduce快。

Hadoop在启动阶段耗费了10-25秒，严重影响了小数据集场景的性能。随着任务数增多，调度器的延时也在增加。

Grep任务中，MR一半的时间花在了合并各个reducer的输出上了。当数据量增大时，Vertica相比DBMS-X的优势体现出来了，这主要归功于Vertica激进的压缩策略。

Selection任务中，两种DBMS仍然明显快于MR，除了上述因素，新的差异在于DBMS有index，而MR没有。Vertica在这个场景中性能超过了DBMS-X一个数量级。但Vertica在节点增多后性能下降得非常厉害，原因是大量节点间的通信打爆了整个系统。这可能是因为Vertica使用了可靠的通信协议，带来了额外的开销。

Aggregation任务中，两种DBMS仍然明显快于MR。Vertica在group非常多时性能相比DBMS-X没有优势，但在group较少时需要传输的数据量变少，它在扫描上的性能优势就体现出来了。

Join任务中，两种DBMS的性能比MR好上两个数量级，有两方面原因：
1. MR需要扫描所有数据，而DBMS可以利用index只读部分数据。另外MR读出数据还要再解析，而DBMS在加载时已经做完了这部分工作。
1. DBMS可以利用join key就是join双方的partition key的特点，直接在各个节点本地join。

UDF聚合任务中，三种系统的性能终于差不多了。DBMS需要per row调用UDF，UDF还会访问DB以外的文件，这显著增加了开销。

## 其它讨论

- DBMS明显比MR更难部署。
- 相比于DBMS，Hadoop的任务都是冷启动的，但这不是MR模型本身要求的。例如，如果允许重用JVM的话上述测试结果可以提升10%-15%。
- HDFS支持块级别和记录级别的压缩，但测试结果显示压缩没有提升，甚至可能降低了性能。Vertica的压缩很激进，当I/O为瓶颈时用一部分CPU换取I/O是值得的，也有比较好的效果。
- Hadoop相比DBMS更耗费CPU，原因是它需要解析和反序列化数据。
- DBMS在生成查询计划后，就只在必要时传输数据了，也不需要传输控制消息，这两点都可以明显降低开销。
- MR的上手难度非常低，但对于长期的任务，它的隐式成本就会体现出来（MR模型的表达能力、schema的管理等）。
