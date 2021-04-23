---
title:      "[笔记] Dremel: interactive analysis of web-scale datasets"
date:       2020-09-22 18:46:25
tags:
    - 笔记
    - Columnar
    - Dremel
    - Google
---

> 原文：[Dremel: interactive analysis of web-scale datasets](https://research.google/pubs/pub36632.pdf)

**TL;DR**

这篇文章讲了两方面：
- 支持嵌套结构（Protocol Buffers）的列式存储（Google内部叫ColumnIO，可以参考开源的[Parquet](https://parquet.apache.org/)）。
- 并行执行架构。

能列式存储复杂的嵌套结构，极大地扩展了列存的应用范围。相比而言Dremel的并行执行架构不是很新颖。

<!--more-->

## 列式存储

### Repetion Level和Definition Level

以下面这个proto为例。

```
message Document {
    required int64 DocId;
    optional group Links {
        repeated int64 Backward;
        repeated int64 Forward;
    }
    repeated group Name {
        repeated group Language {
            required string Code;
            optional string Country;
        }
        optional string Url;
    }
}
```

Dremel会存储以下列：
- `DocId`
- `Links.Backward`
- `Links.Forward`
- `Name.Language.Code`
- `Name.Language.Country`
- `Name.Url`

不同记录的相同列会存储在一起。

问题是如何用这些列来还原这些记录呢？考虑到`optional`和`repeated`字段，我们需要能分辨：
- 前后两个`Links.Forward`属于一条记录还是两条记录。
- 前后两个`Name.Language.Country`属于一条记录的一个`Name`还是两个`Name`还是两条记录。

光靠列值自身是没办法做到的。

Dremel引入了两个概念：Definition Level（DL）和Repetition Level（RL）。

DL指，一个路径上有多少个`optional`或`repeated`字段被定义了，如`DocId`的DL为0，`Links.Backward`的DL为1，`Name.Language.Country`的DL为3。

RL指，一个值是在它的路径的第几个`repeated`字段上在重复。这里的重复的范围是一条记录内，每条记录的这列的第一个值的RL总是0。以`Name.Language.Country`为例：
- 如果RL为0，表示它与上一个值是跨记录的。如果上一个值属于当前记录，则要新开始一个记录。
- 如果RL为1，表示它与上一个值是跨`Name`的。
- 如果RL为2，表示它与上一个值是跨`Name.Language`的。

如果某一层级下，一个`optional`或`repeated`字段未定义，但有和它平级的其它字段定义了，为了避免混淆，我们要插入一个`NULL`。这个`NULL`也是有DL和RL的。

实际存储中：
- 不需要显式存储`NULL`，只要DL小于字段的深度，就是`NULL`。
- DL总为0的字段不存储DL。
- RL也只在需要时存储，如DL为0隐含RL为0，则RL也不需要保存。
- DL和RL按实际使用的位数保存。

同一列非`NULL`的值的DL都是相同的，但`NULL`的DL值可能不同，由此我们可以知道这个`NULL`来自哪一层。

### 反序列化

Dremel会根据查询条件构建一个有限状态机（FSM），每个查询需要的列是一个状态，根据RL转换状态。

## 查询

Dremel有一个RootServer来接收client请求，读取对应表的元数据，再将查询下发给中间节点，直到叶子节点执行。叶子节点会去扫描保存为tablet的列存数据。Dremel允许client指定只扫描完一定比例的tablet就返回，如98%，经常可以显著降低延时。

Dremel的一些算子（如Top-K或Count-Distinct）使用了近似算法。

Dremel有QueryDispatcher来分配节点，它还可以检测慢节点并重新分配交给慢节点执行的查询。

## 性能

本地磁盘：
- 只需要读很少的列的时候，列存的读取性能高于行存一个数量级。
- 组装记录和解析非常耗费性能，分别导致处理时间加倍。（和C-Store的延迟物化结论一样）
- 通常在读取数十列以上时列存的处理时间和行存差不多了，但不同数据集差异非常大，且列存不一定需要组装记录。

MR和Dremel：
- MR将行存换成列存，性能提升了接近一个数量级。
- 将MR换成Dremel，性能提升了两个数量级。

树状执行结构：
- 大量聚合场景多层节点的优势非常明显（每一层做部分聚合，避免串行处理太多下游节点的响应）。


