---
title:      "[笔记] MapReduce: A Major step backwards"
date:       2020-11-08 21:22:24
tags:
    - 笔记
    - Database
    - MapReduce
---

> 原文：[MapReduce: A Major step backwards](https://pdfs.semanticscholar.org/08d1/2e771d811bcd0d4bc81fa3993563efbaeadb.pdf)

**TL;DR**

这篇文章是两位大佬（DeWitt和Stonebraker）对“MapReduce可以革掉传统的并行RDBMS的命”的论点的回击。

MapReduce对于DB社区而言：
1. 在大规模数据密集型应用的编程范式角度是一种倒退。
1. 并非最优化的实现，因其使用暴力搜索而不是索引。
1. 一点也不新奇 —— 它就是25年前广为人知的技术的又一次实现。
1. 缺少当今DBMS必备的多种功能。
1. 与DBMS用户依赖的工具不兼容。

（因此之后若干年大家用高度的热情将这两者又统一起来了）

<!--more-->

## MapReduce是一种倒退

DB社区40年来达成的共识：
* 要有schema。
* schema要与应用分离。
* 要用高阶语言。

有了schema，DB才可以确保输入都是合法的。而MapReduce没办法保证这点，应用可能被非预期的数据搞挂掉。

schema要与应用分离，这样新应用的开发者直接看一下schema就可以，而不需要先检查一遍数据，或找到旧应用的代码 —— 而MapReduce的用户天天就在做着这样的事情。

70年代的时候DB社区有过所谓的“Great Debate”，争论关系模型和CODASYL模型（一种网络模型）哪个更好，访问DB的代码该写成什么样：
* 表达你想要什么（关系模型），或
* 表达你用于访问数据的算法（CODASYL）。

结论就是关系模型更易于写、维护、理解。CODASYL被批评“是访问DBMS的汇编语言”。而MapReduce就类似于CODASYL，是一种低阶语言，表达做什么，而不是要什么。

MapReduce的拥护者可能质疑说MapReduce就是针对那些没有schema的数据，但实际上map和reduce就在依赖数据中有你要的字段。

基于BigTable或HBase的MapReduce程序也有这些问题，每行数据都可能有不同的schema，而且它们还没有logical view，不能解耦应用schema与数据schema。

## MapReduce是糟糕的实现

现代的DBMS都有比如B-tree实现的索引，可以用来减少搜索空间（经常可以减少两个数量级以上）。但MapReduce没有索引，只能暴力搜索。

有种观点是MapReduce的价值在于它的并行执行，但从80年代以来已经有了很多的并行DBMS，如Gamma、Bubba、Grace、Teradata。

MapReduce的其它缺点还包括数据倾斜与数据交换的效率低下。

并行DBMS社区针对数据倾斜问题做了很多研究，已经有了一些方案，而MapReduce目前还没有采用。

MapReduce在M个map任务和N个reduce任务之间要做M*N次数据交换，M和N很大时会带来非常多的磁盘访问（对网络带宽的要求也很高）。这也是并行DBMS不经常物化中间结果的原因。MapReduce依赖这些物化的中间结果来实现良好的容错性，因此MapReduce后续能否改成推/拉模型还不明朗。

MapReduce的数据分区和自定义函数也不新奇：前者早有研究，后者在80年代的Postgres就已经支持了。

## MapReduce缺少特性

以下是现代DBMS必备，但MapReduce缺少的特性：
* 批量载入（Bulk loader）。
* 索引。
* 更新。
* 事务。
* 完整性约束（Integrity constraints）。
* 引用约束（Referential constraint）。
* 视图。

## MapReduce不兼容于DBMS的工具

现代DBMS依赖的工具：
* 报告生成工具。
* BI工具。
* 数据挖掘工具。
* 复制工具。
* DB设计工具。

（但现在都有了）
