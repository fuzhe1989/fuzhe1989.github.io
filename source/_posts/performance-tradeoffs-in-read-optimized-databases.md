---
title:      "[笔记] Performance Tradeoffs in Read-Optimized Databases"
date:       2021-01-10 23:16:30
tags:
    - 笔记
    - Columnar
    - 探索研究
    - Compression
---

> 原文：[Performance Tradeoffs in Read-Optimized Databases](http://nms.csail.mit.edu/~stavros/pubs/vldb2006.pdf)

## TL;DR

> 有点旧，有些结论可能不适合现在的列存数据库了。

这篇文章讲的是，在面向读优化的数据库中，行存和列存的性能差异在什么地方，分别适合什么场景。

大概结论就是projection的列比例越低、selectivity越低、CPU相比于I/O的速度优势越大，列存的性能越有优势。

<!--more-->

需要注意的几点：
1. 这里讲的列存是指纯粹的列存，每列对应不同文件，而不是PAX风格的行列混合格式。
1. 面向读优化，因此不考虑插入、更新等操作（实现一个单纯的read store）。
1. 场景以scan为主。
1. 为了方便比较，所有字段都是定长的；行存和列存有相同的query plan；压缩只使用字典、bit packing和FOR/FOR-delta，而不使用更适合行存的RLE、bitmap等。
1. 使用了向量化，但没有用代码生成、惰性物化等现在常用的技术。

文章指到性能瓶颈有三个地方：I/O带宽、内存带宽、CPU，其中列存在前两项中有优势，而在CPU上消耗要比行存大（使用向量化和代码生成后会有不同结论），因此在projection比例低、cpdb（cycles per disk byte）高的时候适合用列存。当projection比例达到100%时，理论上行存与列存会访问相同的数据，性能应该差不多，但实际上列存的I/O次数会更多（每列一个文件），性能会更差一点。

在selectivity比较少时，列存有机会跳过一些列，优势会更大一些。在selectivity接近100%时，列存算子的pipeline方式需要物化中间结果，CPU消耗高于行存。

前面说的列存的I/O次数更多的问题，可以通过prefetch来解决，文章中尝试了prefetch 48个I/O单元（每个128KB），明显提升了性能。有了prefetch后，列存相当于在用并发I/O，从而将seek延时掩盖住了。

最后的结论是大部分场景中仔细优化过的列存要比仔细优化过的行存性能更好，行存只有在记录非常小（小于20B）、CPU是瓶颈（cpdb小）的场景中有优势。

![](/images/2021-01/perf-read-optimized-01.png)
