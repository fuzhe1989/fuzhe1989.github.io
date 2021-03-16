---
title:      "[笔记] FlumeJava: Easy, Efficient Data-Parallel Pipelines"
date:       2020-10-16 20:00:43
tags:
    - 笔记
    - MapReduce
    - BigData
    - Google
---

> 原文：[FlumeJava: Easy, Efficient Data-Parallel Pipelines](https://research.google/pubs/pub35650.pdf)

## TL;DR

FlumeJava是基于MapReduce的计算框架。它的特点：
- 提供了表达能力很强的Java库。
- 惰性求值。
- 结合了本地运算与远端MapReduce作业。
- 可以生成优化的MapReduce程序。

（FlumeJava这套框架目前仍在被大量使用，但底下应该已经不是MapReduce了。）

<!--more-->

## The FlumeJava Library

FlumeJava提供了几个核心类：
- `PCollection<T>`：一个不可变序列。
- `PTable<K, V>`：一个不可变的multi-map。
- `PObject<T>`：单个Object。

这几个类都是惰性求值的。

基础转换关系：
- `PCollection.<T>parallelDo(fn<T, U>, collectionOf(...))` -> `PCollection<U>`。
- `PCollection.<T>parallelDo(fn<T, Pair<K, V>>, tableOf(...))` -> `PTable<K, V>`。
- `PTable.<K, V>groupByKey()` -> `PTable<K, Collection<V>>`。
- `PTable.<K, Collection<V>>combineValues(fn<V, U>)` -> `PTable.<K, U>`。
- `flatten(Collection<PCollection<T>>)` -> `PCollection<T>`，新序列的元素直接来自各个输入，不会发生拷贝。
- `PCollection.<T>asSequentialCollection()` -> `PObject<Collection<T>>`。
- `PTable.<K, V>asSequentialCollection()` -> `PObject<Collection<Pair<K, V>>>`。
- `PCollection.<T>combine(fn<T, U>)` -> `PObject<U>`。
- `operate(PObject<T>, fn<T, U>)` -> `PObject<U>`。

衍生出`join()`：
- `parallelDo`：`PTable<K, Vi>` -> `PTable<K, TaggeeUnion2<V1, V2>>`。
- `flatten`。
- `groupByKey`：`PTable<K, TaggeeUnion2<V1, V2>>` -> `PTable<K, Collection<TaggeeUnion2<V1, V2>>>`。
- `parallelDo`：`PTable<K, Collection<TaggeeUnion2<V1, V2>>>` -> `PTable<K, Tuple2<Collection<V1>, Collection<V2>>>`。

top()：
- `parallelDo`。
- `groupByKey`。
- `combineValues`。

## Optimizer

### 合并`parallelDo`

合并后的`parallelDo`可以有多个输出。

可以合并连续的`parallelDo`，以及同一个输入的不同下游`parallelDo`。

`combineValues`是另一种形式的`parallelDo`，因此也可以一起合并掉。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/flumejava-01.jpg)

### MapShuffleCombineReduce（MSCR）

可以将`parallelDo`、`groupByKey`、`combineValues`、`flatten`合并为一个MapReduce作业，称为MapShuffleCombineReduce（MSCR）。

一个MSCR有M个输入（每个对应一种map操作）和R个输出（每个可以包含shuffle、combine、reduce阶段）。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/flumejava-02.jpg)

注意shuffle发生在`groupByKey`之前。

### 合并MSCR。

如果多个`groupByKey`操作有相同的上游（可以是合并后的一个`parallelDo`的不同输出），称它们相关。

多个相关的`groupByKey`可以合并为一个MSCR，这些`groupByKey`上游的`parallelDo`合并进MSCR，对应一个输入，每个`groupByKey`对应一个输出。如果MSCR的某个输出只被`comineValues`或`parallelDo`消费，这个操作也可以合并进来（`parallelDo`也可以合并进下个MSCR的输入）。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/flumejava-03.jpg)

最后孤立的`parallelDo`会被转换为平凡的MSCR，这样最终执行计划只剩下MSCR、`fatten`、`operate`。

### 整体优化

整体优化的顺序：
1. 下移`flatten`，将它下面的`parallelDo`复制到每个输入上，即将`parallelDo(flatten(A, B))`转为`flatten(parallelDo(A), parallelDo(B))`，这样能创造更多的合并`parallelDo`的机会。
1. 如果`groupByKey`后面跟着`combineValues`，可以将`combineValues`视为普通的`parallelDo`，从而参与`parallelDo`的合并。
1. 如果两个`groupByKey`中间是一个或多个`parallelDo`，我们需要找一个边界，将它们切成两个MSCR。优化器会沿着这条路径估计每步产生的`PCollection`的大小，从最小的那个位置切开。
1. 合并`parallelDo`。
1. 合并与转换MSCR。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/flumejava-04.jpg)

### 限制

目前FlumeJava没有做：
- 静态分析用户代码。
- 修改用户代码。
- 公共表达式消除。
- 移除不必要的`groupByKey`。

## Executor

FlumeJava可以根据数据大小决定是本地运行，还是启动远端的MapReduce作业。

## Lumberjack的教训

FlumeJava之前，Google还开发过一个类似的语言Lumberjack：
- 适合表达并行pipeline。
- 隐式并行。
- 函数式。
- 精巧的多态类型系统。
- 局部类型推导。
- 轻量tuple和record类型。
- 匿名函数作为一等公民。
- 拥有强大的静态优化器，包含了内联、数据流分析等常规优化，以及合并并行循环等非常规优化。
- 编译为中间代码，最终可以生成Java字节码或机器码。

它的问题在于：
- 隐式并行违反了用户的习惯。相比之下，FlumeJava代码写起来更多，但更好理解，它有一个`PCollection`到`Collection`和`parallelDo`到`iterator`的明显的边界。FlumeJava的“命令式为主”的接口加上不可变结构也更符合用户的习惯。
- Lumberjack的静态优化器没办法根据运行期数据进行优化。FlumeJava的动态优化器带来的开销是非常有限的，但运行期优化却有着非常大的优势。
- 基于Lumberjack开发高效完整的系统比基于FlumeJava更难，更花时间。
- 新颖阻碍着被接受。FlumeJava是Java的一个库，极大降低了用户接入的成本。
