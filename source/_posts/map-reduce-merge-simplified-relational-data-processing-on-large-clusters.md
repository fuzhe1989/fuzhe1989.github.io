---
title:      "[笔记] Map-reduce-merge: simplified relational data processing on large clusters"
date:       2020-09-27 22:46:41
tags:
    - 笔记
    - MapReduce
---

> 原文：[Map-reduce-merge: simplified relational data processing on large clusters](http://www.cs.duke.edu/courses/cps399.28/current/papers/sigmod07-YangDasdanEtAl-map_reduce_merge.pdf)

## TL;DR

这篇文章为MapReduce模型增加了一步Merge，增强了Join的能力。原理如图。

![](/images/2010-09/map-reduce-merge-01.jpg)

<!--more-->

## Merge Impementation

merge动作会在所有reducer结束后启动。MapReduceMerge允许定制以下行为：
- Partition Selector。它的输入是左右两个数据源的reducer编号列表，输出是这个merger处理哪些reducer。

    ![](/images/2010-09/map-reduce-merge-02.jpg)
- Processor。它是在左右数据集上执行merge，可以用来实现hash-join等。它与Merger互斥。
- Merger。它是逐条执行merge，输入是左右各一条记录，输出是join后的结果。

    ![](/images/2010-09/map-reduce-merge-03.jpg)
- Iterator。它可以定义在迭代过程中左右数据集上的iterator如何前进，从而实现不同的join。
    
    ![](/images/2010-09/map-reduce-merge-04.jpg)
    ![](/images/2010-09/map-reduce-merge-05.jpg)
    ![](/images/2010-09/map-reduce-merge-06.jpg)

## Map-Reduce-Merge实现关系运算

- Projection：只通过mapper即可。
- Aggregation：只通过reducer即可。
- Generalized Selection：mapper、reducer、merger都可以参与实现。如果selection只涉及源数据，只需要mapper；如果涉及aggregation后的结果，需要reducer；如果跨数据源，需要merger。
- Join：通过merger实现。
- Set Union：reducer负责去重，merger负责将来自两个源的有序数据组合起来。
- Set Difference：类似于set union。
- Cartesian Product：每个merger处理一个左源的reducer和所有右源的reducer。

## Map-Reduce-Merge实现Join

### Sort-Merge Join

- Map：根据range切分源数据，交给reducer。
- Reduce：排序。
- Merge：从左右选取覆盖相同key range的reducer进行一次遍历。

### Hash Join

- Map：根据hash value切分源数据。
- Reduce：将数据根据hase value聚合起来。
- Merger：使用Processor，本地构建一个hash table。

### Block Nested-Loop Join

- Map：与hash join相同。
- Reduce：与hash join相同。
- Merge：与hash join相同，但已默认实现。
