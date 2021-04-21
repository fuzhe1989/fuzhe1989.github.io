---
title:      "[笔记] Omid: Lock-free transactional support for distributed data stores"
date:      2021-04-17 23:14:39
tags:
    - 笔记
    - OLTP
    - Snapshot Isolation
    - 2PC
---

> 原文：[Omid: Lock-free transactional support for distributed data stores](https://ieeexplore.ieee.org/abstract/document/6816691/)
>
> Omid三部曲：
> 1. Omid: Lock-free transactional support for distributed data stores
> 2. [Omid, Reloaded: Scalable and Highly-Available Transaction Processing](/2021/04/20/omid-reloaded-scalable-and-highly-available-transaction-processing)
> 3. [Taking Omid to the Clouds: Fast, Scalable Transactions for Real-Time Cloud Analytics](/2021/04/21/taking-omid-to-the-clouds-fast-scalable-transactions-for-real-time-cloud-analytics)

Omid的目标是为支持MVCC的分布式KV增加乐观事务（2PC）功能，实现snapshot isolation。

它的特点：
- 乐观锁（它称为lock-free）。
- 有中心化的Status Oracle。
- 依赖于client进行SI需要的判断。

这是Omid的第一篇paper（后面还有2篇），从后续paper来看，这篇paper的实现在规模上还是存在问题，但它确实解决了Percolator的延迟高（尤其是有事务失败时）的问题。

<!--more-->

在Omid之前，Google的[Percolator](/2020/12/11/large-scale-incremental-processing-using-distributed-transactions-and-notifications)也实现了类似的功能，但[[1]]表示Percolator有以下问题：
- 数据与锁meta存在一起的做法导致了数据server负担过重，容易产生高延迟。
- 去中心化的设计导致一旦事务失败（如client宕机），一段时间内锁不能被及时清理，阻碍后续事务进行。

Omid使用了中心化的Status Oracle（SO），所有锁的meta由SO管理，与data分离，解决了问题一；中心化节点去除了分布式事务的必要性，大大降低了实现复杂度，也解决了问题二。

但中心化使得：
- 容量受限，可以通过裁剪meta数据，只保留最近的事务信息[[2]][[3]][[4]]来缓解，但引入了违背一致性的风险。
- 单个server的处理能力有限。

Omid解决（内存）容量问题的方式仍然是裁剪meta，但保留了SI需要的信息，因而保证了一致性。而在处理能力方面，Omid会将一个事务需要的meta信息轻量化地复制给client，允许client本地做SI需要的判断，从而分担了SO的处理压力。

这里面的关键点就是只读事务不需要它的start_ts之后开始的其它事务的信息，SO每次在回复client的get_timestamp请求时，都会带上距离上次请求之间的meta修改，这样client就可以在开始事务后本地构建出它需要的meta视图。

我们知道SI中判断两个事务冲突的规则是：
1. 空间上重叠：都写同一行r。
1. 时间上重叠：T<sub>s</sub>(Ti) < T<sub>c</sub>(Tj)且T<sub>s</sub>(Tj) < T<sub>c</sub>(Ti)。

那么为了做判断，需要的信息有T<sub>s</sub>（开始时间）、T<sub>c</sub>（提交时间）和writes（修改过的行）。

假设有和Percolator一样的TSO来提供全局唯一单调增的timestamp作为事务id，则SO不需要记录T<sub>s</sub>，只需要记录每行对应的T<sub>c</sub>。算法如下：

![](/images/2021-03/wsi-02.png)

注意第6行，我们需要维护每个事务的T<sub>s</sub>与T<sub>c</sub>的映射，这样后续的事务T<sub>r</sub>才能判断它读的行是否在它开始之前提交了（在它的read snapshot中）：

![](/images/2021-04/omid-14-01.png)

Omid底层使用了HBase来存储数据，数据的版本是对应事务的T<sub>s</sub>（注意不是T<sub>c</sub>），因此client在完成一次读取时需要通过`inSnapshot`来判断哪些版本是需要过滤掉的。一种naive的实现就是向SO发送对应的请求：

![](/images/2021-04/omid-14-02.png)

下面开始讲Omid的设计。

为了限制内存使用，Omid只会保留最近NR个事务的meta，以及T<sub>max</sub>（不在内存中的最大的T<sub>s</sub>），如下图。

![](/images/2021-04/omid-14-03.png)

当行r对应的`lastCommit(r)`不在内存中时，我们知道它也一定小于T<sub>max</sub>，因此有T<sub>max</sub> < T<sub>s</sub>(txn<sub>i</sub>)意味着`lastCommit(r)` < T<sub>s</sub>(txn<sub>i</sub>)。

由此我们得到算法3：

![](/images/2021-04/omid-14-04.png)

注意行2会导致一些合法的事务被abort，SO内存保留的事务越多，这种false positive概率越小。

接下来，我们需要让`inSnapshot`能在裁剪事务列表后仍然能正确工作。

Omid中维护了aborted和uncommitted两个事务列表，不在这两个列表的事务就默认是committed（得益于Omid的中心化设计，SO有所有事务的start信息）。这样所有事务就分成了四类：
- 大于T<sub>max</sub>的已提交事务，有commit_ts。
- 不大于T<sub>max</sub>的已提交事务，没有commit_ts。
- 位于aborted队列的事务。
- 位于uncommitted队列的事务。

为了控制两个队列的长度，Omid会：
- 在T<sub>max</sub>提升时将uncommitted队列中小于T<sub>max</sub>的事务abort掉（移到aborted队列中）。这意味着持续时间过长的事务会被提前中止掉。
- 在aborted事务对应的数据被清理后移除将事务从aborted队列中去掉。这种清理是由client主动发起的，并在完成后通知SO。

这样我们有了新的`inSnapshot`算法：

![](/images/2021-04/omid-14-05.png)

注意当前事务（txn<sub>r</sub>）的start_ts不可能小于T<sub>max</sub>（会被abort）。

接下来，我们来解决一下naive方案中依赖SO做`inSnapshot`判断的问题。

Percolator的做法是将commit_ts再写回到数据存储中（图4），但这样SO的工作量就非常大。Omid的做法是在处理`getTimestamp`时将事务meta返回给client，让client有足够的信息本地做`inSnapshot`的判断（图5）。

![](/images/2021-04/omid-14-06.png)

![](/images/2021-04/omid-14-07.png)

这种设计得益于以下信息：
- 大于start_ts的commit信息可以忽略。
- 数据不走SO，因此SO属于CPU密集应用，网络带宽不成问题。
- `getTimestamp`返回的数据量通常非常小，再加点数据（压缩后）也能装进一个packet里，因此这种设计几乎没有额外开销（有点怀疑）。
- 假设有N个client，每个client在每个事务过程中请求一次`getTimestamp`，则每次返回的事务meta平均下来不会大于N。

对于每个事务，client只需要维护以下信息：
- T<sub>max</sub>。
- 介于T<sub>max</sub>与T<sub>s</sub>间的已提交事务的start_ts和commit_ts。
- aborted列表。

注意client不需要维护`lastCommit`数据，而这个是最占SO内存的部分。

由此我们得到了优化后的Omid事务全过程：

![](/images/2021-04/omid-14-08.png)

（从2017年的paper可以看到SO还需要维护一个WAL，但这篇没有提）

[1]: https://ieeexplore.ieee.org/abstract/document/6816691/ 
[2]: https://dl.acm.org/doi/abs/10.1145/568271.223787
[3]: https://ieeexplore.ieee.org/abstract/document/5767897/
[4]: https://ieeexplore.ieee.org/abstract/document/1541186/