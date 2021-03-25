---
title:      "[笔记] Weaving Relations for Cache Performance"
date:       2021-03-25 22:19:01
tags:
    - 笔记
    - Database
    - Cache
---

> 原文：[Weaving Relations for Cache Performance](http://www.vldb.org/conf/2001/P169.pdf)

这篇文章提出了一种行列混合的布局方式PAX，可以在不改变operator的前提下提升cache利用率。现在这种布局几乎成为了各种OLAP系统的标配。虽然这篇文章是针对OLTP的，但OLTP中使用PAX的似乎不是很多。

<!--more-->

## 传统布局：NSM与DSM

传统上数据库的数据有两种存储布局方式，一种是按行存到每个page中，称为N-ary Storage Model（NSM）。另一种是将列拆开，每列的数据连续存到各个page中，称为Decompposition Storage Model（DSM）。

NSM的最大优点就是实现简单：每条记录（行）连续，插入只需要寻找有足够空闲空间的page。但它的缺点也很明显：大多数query只需要读部分列，而NSM的读取单位是page，里面包含了所有列，造成了I/O浪费。

DSM就是针对这点，将数据按列拆开，这样读磁盘的时候只需要加载列对应的page，节省I/O。但DSM的缺点是重新组装记录的过程很复杂。DSM中每列的每个值都要额外再记一个row-id（或offset等）来join相同记录的不同列，这样既造成了存储的浪费，join过程也需要比较复杂的流程。join一行数据可能需要加载多个列对应的page（多次I/O），而NSM则只需要一次I/O。

随着CPU速度的提升，CPU与内存的速度差成为了新的性能瓶颈，如何利用好cache成为了性能提升的关键（[DBMSs On A Modern Processor: Where Does Time Go?](/2021/03/07/dbmss-on-a-modern-processor-where-does-time-go)）。

![](/images/2021-03/weaving-01.png)

上面的图中可以看到NSM对cache并不友好，当我们执行`SELECT name FROM R WHERE age < 40`这样的query时，不需要的列也会被载入cache：
1. 浪费内存带宽；
2. 容易把有用的数据挤出cache；
3. 需要的数据分散在不同的cache block中（比如我们只需要age），scan时每迭代一次都造成一次cache miss。

![](/images/2021-03/weaving-02.png)

## PAX：结合NSM与DSM

DSM则天生对cache友好，因为相同列的数据连续排布，也会一起载入cache。它的问题还是join行的成本太高，一旦涉及的列太多，性能急剧下降。

本文综合了这两种布局，提出了Partition Attributes Across（PAX）布局，在page以上它与NSM相同，仍然是按行切分的，但在page里面它将数据按列分成若干个mini-page，mini-page中相同列的数据连续排布。PAX因此有以下优点：
1. 兼容NSM的operator，只需要修改page内部的迭代器即可。
1. cache友好，只有需要的列的数据会被载入cache。
1. 一行的数据仍然分布在一个page内，访问一行的I/O次数与NSM相同，join行的成本低。且PAX中不需要记录row-id。

![](/images/2021-03/weaving-03.png)

## 设计与实现

![](/images/2021-03/weaving-04.png)

PAX中每个page有一个header，除此之外就是若干个mini-page。mini-page分为固定大小（F）和可变大小（V）两种，前者的尾部有一个bitmap标识每个值是否存在，后者的尾部则是每个值的offset（2B）。

在插入记录的时候，NSM和DSM比较简单，分别按记录/值追加到page中即可。PAX则需要处理mini-page满但page不满的情况。

PAX中每个可变大小的列都会记录一个平均大小，利用这个大小我们可以估计出一个page中可以存放多少条记录，每个mini-page的大小。每当插入一条记录时各列的平均大小随之变化。如果遇到mini-page满了，如果此时page仍然能容纳这条记录，PAX会调整每个mini-page的大小（需要重写这个page），否则就分配新page。

当一个page写完后，它的各个mini-page的大小会成为新page的初始值，这样就能迅速回应值占用空间大小的变化。

mini-page的重组开销还是很大的，这里有种优化是为重组时page可用空间设置一个阈值，低于这个阈值则直接开始写新page。下图是几种情况下的重组次数与开销占比。

![](/images/2021-03/weaving-05.png)

当更新一条记录时，NSM与PAX的策略相同：原地更新，如果超出大小则重组page。这里PAX的优势是重组时平均只需要搬运半个mini-page的数据，而NSM则需要搬运半个page的数据。

## 评测

直接放结论：几乎所有场景中PAX相比NSM和DSM都有着明显的性能提升。