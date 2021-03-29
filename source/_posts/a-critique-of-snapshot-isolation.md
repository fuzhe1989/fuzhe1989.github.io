---
title:      "[笔记] A Critique of Snapshot Isolation"
date:       2021-03-28 13:03:20
tags:
    - 笔记
    - Transaction
    - OLTP
---

> 原文：[A Critique of Snapshot Isolation](https://dl.acm.org/doi/abs/10.1145/2168836.2168853)

Snapshot isolation下存在着

<!--more-->

## Introduction

事务ACID中的I即Isolation，定义了并发事务间的相互作用。最高的isolation级别是serializability（可串行化），这个级别保证了系统的行为如同是将所有事务按某个顺序串行执行的结果。但众所周知的是serializability实现开销巨大，且严重限制了系统的并发能力。

许多实际系统因而采用了另一种可以达到高并发的isolation，snapshot isolation。这种isolation下，每个事务只能看到它开始时的DB状态（一个snapshot）。只读事务不会与其它事务冲突，因而不需要任何锁机制。但两个修改相同元素的写事务之间仍然可能有冲突，称为写写冲突（write-write conflict）。

基于锁的snapshot isolation实现非常直接，每个事务在修改元素前尝试加锁，如果加锁失败则abort（或wait）。但snapshot isolation相比serializability还欠缺一项，即检测出读写冲突（read-write conflict）的能力。然而检测RW冲突会带来明显的开销，因为读事务占了典型workload的大头，且最直接的RW检测还可能导致只读事务abort，限制了系统的并发能力。

一种无锁的snapshot isolation实现（omid前身）[[1]]中每个事务都要将自己的修改列表发给中心节点（status oracle）来检测WW冲突。这种设计同样也可以用来检测RW冲突，只要每个事务将自己读过的row也发给SO即可。

这篇文章分析了WW和RW冲突，得出结论：
- SI的WW冲突超出了serializability的需要，即允许WW冲突也可以实现serializability。
- 只需要检测RW冲突就可以满足serializability。

作者随后提出了write snapshot isolation，只检测RW冲突（不需要处理WW冲突），如果一个事务读过的数据在提交前被其它事务修改了，这个事务就需要abort。

作者预期WSI能达到SI同等的并发能力：
1. WSI和SI都不会abort只读事务，而这就占据了大部分事务。
1. RW冲突和WW冲突都有假阳性（事务冲突但仍然满足serializability）。

（WSI因此具备和SI同等级的性能，且拥有更高的isolation级别，但似乎仍然达不到serializability，还需要gap锁之类的机制来避免conditional update中的phantom read）。

## Snapshot Isolation

Snapshot isolation的机制略。SI的优势在于一个事务的写不会阻塞其它事务的读。

两个事务Ti和Tj发生WW冲突的条件是：
1. 空间上重叠：都写同一行r。
1. 时间上重叠：T<sub>s</sub>(Ti) < T<sub>c</sub>(Tj)且T<sub>s</sub>(Tj) < T<sub>c</sub>(Ti)。

如下图中txn<sub>n</sub>和txn<sub>c</sub>存在WW冲突。

![](/images/2021-03/wsi-01.png)

基于锁的SI实现有[Percolator](/2020/12/11/large-scale-incremental-processing-using-distributed-transactions-and-notifications)。当一个事务要写被锁住的行时，可以有不同实现：wait；自身abort；强行abort掉占有锁的事务（wound-wait）。

作者本身倾向于[[1]]这样的无锁的SI实现。它会依赖于一个中心化的SO。作者认为SO可以达到50K的TPS，不太会成为并发的瓶颈（严重怀疑）。

下面是这种实现的commit过程。

![](/images/2021-03/wsi-02.png)

这里关键是如何管理每行的commit ts。最直接的想法是由SO本身管理，但这样扩展性不好。也可以写回DB[[1]]或由client来维护[[2]]（更详细的分析可以看omid[[3]][[4]][[5]]）。

## Serializability

Q1：**避免WW冲突是serializability的充分条件吗？**

显然不是，避免WW冲突也没办法避免write skew：

```
H 1. r1[x] r2[y] w1[y] w2[x] c1 c2
```

H1中两个事务分别读了对方要写的行，显然没有WW冲突，但不满足serializability。

```
H 2. r1[x] r1[y] r2[x] r2[y] w1[x] w2[y] c1 c2
```

H2中两个事务都读了x和y，想要保证x + y > 0，假设初始x = y = 1，T1将x改为-1，T2将y改为-1，两个事务在SI下都会成功，不满足serializability。

Q2：**避免WW冲突是serializability的必要条件吗？**

SI能避免ANSI SQL标准中定义的以下异常：
- 脏读（dirty read），读到了未提交的值。
- 不可重复读（fuzzy read），读到了已被其它并发事务删除的值。
- 幻读（phantom），满足一组条件的数据集被并发事务修改且被读到。

另外它还能避免写丢失（lost update)异常：

```
H 3. r1[x] r2[x] w2[x] w1[x] c1 c2
```

但注意如果T2没有读x，则不构成写丢失异常：

```
H 4. r1[x] w2[x] w1[x] c1 c2
```

这是因为在事务重排序后，H4等效于：

```
H 5. r1[x] w1[x] c1 w2[x] c2
```

显然H5是满足serializability的，H4因此也满足。

H4的例子就说明WW冲突可能会阻止一些合法的、满足serializability的场景。

结论就是：避免WW冲突既不是serializability的充分条件，也非必要条件，我们需要重新审视SI要避免WW这一观点。

## Read-Write vs. Write-Write






[1]: https://ieeexplore.ieee.org/abstract/document/5958809/
[2]: https://www.sigops.org/s/conferences/sosp/2011/posters/summaries/sosp11-final12.pdf
[3]: https://ieeexplore.ieee.org/abstract/document/6816691/
[4]: https://www.usenix.org/conference/fast17/technical-sessions/presentation/shacham
[5]: https://dl.acm.org/doi/abs/10.14778/3229863.3229868
