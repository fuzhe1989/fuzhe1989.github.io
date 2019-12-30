---
title:      "[笔记] HadoopDB: An Architectural Hybrid of MapReduce and DBMS Technologies for Analytical Workloads"
date:       2020-09-21 16:08:09
tags:
    - 笔记
    - BatchProcessing
    - MapReduce
    - Database
---

> 原文：[HadoopDB: An Architectural Hybrid of MapReduce and DBMS Technologies for Analytical Workloads](https://people.eecs.berkeley.edu/~kubitron/courses/cs262a-F19/handouts/papers/vldb09-861.pdf)

## TL;DR

这篇文章在尝试结合MapReduce（Hadoop）与DBMS（PostgreSQL），取MapReduce的伸缩性与容错性、DBMS的性能。

实现：
- PostgreSQL作为Hadoop的DataNode。
- 将SQL翻译为MR任务，
- 通过兼容于JDBC的InputFormat将具体查询交给PostgreSQL执行。

结论：结合“A comparison of approaches to large-scale data analysis”的性能测试结果，HadoopDB相比Hadoop的分析性能好了一些，仍然达不到并行DBMS的水平，但验证了这个方向是有意义的。

HadoopDB将Hive中对数据源的scan换成了单节点PostgreSQL的查询，但仍然是传输数据而不是传输逻辑，且还是要物化中间结果，这些提高了容错性，但显著降低了性能。

<!--more-->

## Introduction

作者观察到的趋势：
- 大规模分析的市场越来越大。
- shared-nothing架构因为其伸缩性，特别适合实现大型分析系统。
- 典型分析workload的特点（大量scan、多维度聚合、星型join）也非常容易在shared-nothing系统中并行执行。

但现在的并行DBMS的伸缩性并不好，节点数达到百级别就有点困难了，原因是：
- 大规模系统的错误是常态，而并行DBMS的设计假设了错误很罕见。
- 并行DBMS通常假设机器是同构的，而实际的大规模系统很难保证这一点。
- 大规模系统的需求直到最近才多起来，因此大规模的并行DBMS没有得到过充分的测试，难以预料还有什么工程上的障碍。

MapReduce可以支持非常高的规模，但它缺乏许多对结构化数据分析非常有用的特性，MR模型也不适合那种一次加载后多次处理的场景。

HadoopDB就是要结合MapReduce和并行DBMS，用MapReduce作为通信层，将SQL翻译为MapReduce任务，尽可能多地下发查询给DBMS节点执行。

HadoopDB的目标是尽可能地利用已有的开源资源，用Hadoop+PostgreSQL来实现。

## HadoopDB的实现

HadoopDB提供了以下四个组件：
1. DB Connector，允许TaskTracker访问DBMS。
1. Catalog，维护DB的元数据。
1. Data Loader，用于全局和本地的数据分区、加载数据到DBMS。它有global hasher和local hasher，前者用于将数据分为HDFS上的若干个文件，后者用于将HDFS的一个文件分区到本地文件系统中。通过两层分区，HadoopDB保证了每个chunk大小均匀，从而得到更好的负载均衡性。
1. SQL to MapReduce to SQL Planner（SMS），将SQL转为MR任务，再将各个MR子任务转回SQL。SMS扩展自Hive。相比Hive，SMS可以利用collocation、相同partition key等信息下推尽可能多的信息到DBMS中，即Hive的scan加上filter、group by、join等操作，在SMS中可以结合到一个operator中完成。对于跨DBMS的操作，SMS看起来要走和Hive相同的operator，与底下的DBMS就没有关系了。

## 性能指标

在加载阶段，HadoopDB的性能更接近于DBMS-X，相比Hadoop慢了很多。

Grep任务中HadoopDB只比Hadoop略快一点点，原因是这个任务以扫描为主，HadoopDB只是省掉了实时的解析开销。

Selection任务中，HadoopDB默认会将Rankings和UserVisits表一起分区，导致Rankings表被分成了20个只有50MB的chunk，20个map任务的调度开销显著降低了HadoopDB的性能。如果不分区Rankings表，HadoopDB可以利用它在pageRank列上的索引，显著提升了性能（超过了DBMS-X）。

Aggregation任务中，在聚合度较高时，Hive使用了hash聚合，相比原生Hadoop性能提升了非常多；在聚合高较低时，Hive转而使用了基于sort的聚合策略，这就与原生Hadoop相同了，且Hive在保存中间结果时有额外开销，此时它的整体性能还差于原生Hadoop。PostgreSQL在两种场景下都使用了hash聚合，因此HadoopDB的性能超过了Hadoop。

Join任务中，HadoopDB得益于使用索引和内建支持join，性能远高于Hadoop，接近于两种DBMS（差距仍然明显）。

UDF任务中，几个系统的性能差不多。

## 容错性

文章测试了各个系统的正常执行时间，以及单错误节点、单慢节点时的执行时间，HadoopDB的容错性确实比DBMS要好很多。尴尬的是就算在有错误或慢节点时，DBMS的执行时间仍然远低于HadoopDB和Hadoop。
