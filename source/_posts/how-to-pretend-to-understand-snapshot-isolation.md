---
title:      "如何装作懂 Snapshot Isolation"
date:       2022-11-01 22:32:15
tags:
---

> 以下大量内容参考自 [Snapshot Isolation综述](https://zhuanlan.zhihu.com/p/54979396)，不一一列举了。

某天，某群，某位老师冒出来一个问题：

> 话说我在想，snapshot isolation 这种的读不会被写阻塞是不是一个伪命题

<!--more-->

抛开问题本身，这引起了我的兴趣：如何正确理解 snapshot isolation。

字面意义上的 snapshot isolation 理解起来并不难：

> In databases, and transaction processing (transaction management), snapshot isolation is a guarantee that all reads made in a transaction will see a consistent snapshot of the database (in practice it reads the last committed values that existed at the time it started), and the transaction itself will successfully commit only if no updates it has made conflict with any concurrent updates made since that snapshot.
>
> [Snapshot isolation](https://en.wikipedia.org/wiki/Snapshot_isolation)

从这个定义上来看，snapshot isolation 就是为每个事务的读准备一个系统的快照（snapshot），这个快照一旦建立就不会再被修改，从而达到了 isolation 的作用。但在事务提交时，如果系统当前状态与它的读快照不符了，这就是经典的 read-modify-write 的冲突，事务本身就要 abort。

我们可以将这句话拆成几部分：
- 生成一个快照
- 修改系统的状态
- 冲突检测

接下来我们采用 read-modify-write 的思路来尝试理解 snapshot isolation。

## 不用 MVCC 可以实现 snapshot isolation 吗

常见的系统都是用 MVCC 来实现 snapshot isolation 的。我们来探讨一下其它方法为什么不行。

### 原地修改

对于原地修改的系统，一个新的修改会破坏一个已有的 snapshot。这样为了不破坏当前事务的运行，我们就只能阻止其它可能冲突的事务运行。

方法大家肯定都会，加锁呗。暴力点的就所有事务串行执行，温柔点的就把锁的粒度变小，把相互冲突的事务给串行化，不冲突的放行。

但通常我们管这个叫 serializable，不叫 snapshot isolation。

### copy-modify-write

如果将系统的状态复制出来，之后本地修改，最后再应用回系统，我们至少保证了事务的执行阶段是相互独立的，不影响并发度。

比如对于一个 LSM store，只要将 MemTable 和 Manifest 完整复制下来，就生成了一个显然正确的快照。说得好，但有点不好的地方：
1. 数据量大的时候复制成本过高。
1. 为了保证系统状态一致，复制阶段需要避免有人修改系统状态，通常这意味着加锁。于是系统的并发又上不去了。地球不欢迎这样的 snapshot isolation。

> 别笑，我甚至参与开发过这样的系统。

我们可以想办法降低复制的粒度，降到刚好是事务可能访问的数据集。但还是有些小问题：
1. 交互式事务不那么容易获得准确的数据集。
1. 大事务的复制成本依然非常高。
1. 依然意味着要加锁（取决于数据集大小）。

另一个方向是降低复制的开销。比如对于上面的 LSM store，我们知道所有 SST file 是 immutable 的，持有 Manifest 就意味着一个不变的 view。而持有 Manifest 的开销是非常低的：只需要复制每个 SST 的 shared_ptr 之类的东西。这样复制开销主要就是复制 MemTable 了。如果我们将 MemTable 实现为 [immutable 结构](/2017/11/07/persistent-data-structure/)，就可以以非常低的开销复制出来一个 MemTable。

对于 client-server 架构，如果要把数据集维护在 client 端，上面的优化就用不上了。如果可以把数据集维护在 server 端，就还是可以获得低开销的数据集复制。如果 server 因为某些原因清除了对应的数据集（如迫于内存/磁盘压力、server 重启等），client 要能正确 abort 事务并重试。

copy-modify-write 接下来会遇到的问题是，如何检测冲突？
1. 逐一对比 snapshot 中的每个值，一方面开销大，另一方面还会有 [ABA 问题](https://en.wikipedia.org/wiki/ABA_problem)：我们怎么区分一个值没变和被修改多次最后回到了初始值呢？
1. 分布式系统中检测本身会发生在多个节点上，为了避免一个节点检测通过之后又有新的写入破坏 snapshot，我们还是要回到加锁上来。

上面第一个问题，如果不能直接检测值本身的变化，一个很自然的想法就是记录一个版本号来保留修改的痕迹。于是我们得到了 MVCC。

## MVCC

实际上，snapshot isolation 暗含了 happen-before 关系，也就是 time。那很自然的想法就是把 time 保存到系统状态中，也就是 MVCC。

> Multiversion concurrency control (MCC or MVCC), is a concurrency control method commonly used by database management systems to provide concurrent access to the database and in programming languages to implement transactional memory.
>
> [Multiversion concurrency control](https://en.wikipedia.org/wiki/Multiversion_concurrency_control)

我们保证系统中的每次状态修改都附带 timestamp，则 timestamp 本身就意味着一个 snapshot。

接下来的问题是，timestamp 从哪来。

## timestamp 分配

> 参考 [分布式事务中的时间戳](https://ericfu.me/timestamp-in-distributed-trans/)

某种角度我们可以将 timestamp 的分配方式分成几类：
1. 全序：
    1. 使用中心节点产生单调增的 timestamp，任意两个 timestamp 之间都可以比较大小。Percolator、TiDB 等使用了这种方式生成 timestamp。
    1. MySQL 的 ReadView 使用了事务序号，虽然不是 time，但它也是全序的。
1. TrueTime：单独将它分为一类是因为……它太特殊了。TrueTime 接近具有全序性，除了两个相互重叠的 timestamp 无法比较之外。Spanner 专用。
1. 偏序：HLC/LC，只有具有 happen-before 关系的两个 timestamp 才可以比较。
1. 有上界的 HLC：通过 NTP 等方式为 HLC 增加一个偏移量的上限，这样就将每个 timestamp 转化成了类似于 TrueTime 的 timestamp range。同样地，两个 timestamp range 如果不重叠就可以比较大小。CRDB 的创新。

timestamp 分配的开销从全序到偏序是逐渐下降的，那为什么有些系统还要使用开销大的全序方案？为了 serializable。

serializable 要求所有事务可以线性排在一个时间轴上，因此所有不具备全序性的 timestamp 都有可能引入异常（anomaly）而破坏 serializable。

> 具体例子可以参考上面的文章。

但为什么上面使用了非全序 timestamp 的 Spanner 和 CRDB 声称提供了 serializable 保证，而使用了全序 timestamp 的 Percolator/TiDB 却声称不保证 serializable 呢？

1. Spanner 通过 read wait/commit wait 保证了任意两个事务的 timestamp 都是可比较的，从而实现了 strict/external serializable，即事务之间不仅仅可串行化，其执行顺序还与外界感知的因果关系相同。
1. CRDB 通过提升 write timestamp 的方式确保了可以正确检测出有因果联系的事务之间的读-写冲突，从而实现了 serializable snapshot isolation（强于 snapshot isolation 但弱于 strict serializable）。但它无法检测出两个没有因果联系的事务之间的潜在冲突。
1. Percolator 不能实现 serializable 的原因不在于 timestamp 分配方式，而是因为原始的 snapshot isolation 的冲突检测有着非常明显的问题。

## snapshot isolation、serializable snapshot isolation、write snapshot isolation

原始的 snapshot isolation 的冲突检测规则是：事务提交时，检测它的 write-set 中是否有元素在 start_ts 到 commit_ts 之间被人修改过（已提交）。

为什么说它有着非常明显的问题？这篇文章一开始，我们很自然地将 snapshot isolation 类比为一次 read-modify-write。rmw 也有冲突检测，但我们都知道它检测的是 read-set 有没有被人修改过，而不是 write-set！

一种朴素的理解：事务的计算（modify）过程完全是基于 snapshot，因此为了保证事务正确提交，我们需要保证的是提交那一刻事务的 snapshot 假设仍然成立，而 snapshot 对应的就是 read-set。

更深刻的理解可以参考以下文章：
- [Weak consistency: a generalized theory and optimistic implementations for distributed transactions](https://www.csd.uoc.gr/~hy460/pdf/adya99weak.pdf)
- [Making Snapshot Isolation Serializable](https://www.cse.iitb.ac.in/infolab/Data/Courses/CS632/2015/2013/2011/Papers/p492-fekete.pdf)
- [Serializable Isolation for Snapshot Databases](https://ses.library.usyd.edu.au/bitstream/handle/2123/5353/michael-cahill-2009-thesis.pdf)
- [Serializable Snapshot Isolation in PostgreSQL](https://arxiv.org/pdf/1208.4179.pdf)
- A Critique of Snapshot Isolation

这些文章最终都得出结论：检测读-写冲突才是 snapshot isolation 正确的打开方式。

其中：
1. serializable snapshot isolation（ssi）提出要在读写事务 A 提交时检测它是否破坏了另一个已提交事务 B 的 snapshot。即事务 A 的 write-set 与事务 B 的 read-set 有重合，且两个事务在存活时间上也有重合。
1. write snapshot isolation（wsi）提出要在读写事务 A 提交时检测自己的 read-set 有没有被另一个已提交事务 B 破坏。即事务 A 的 read-set 与事务 B 的 write-set 有重合，且两个事务在存活时间上也有重合。

wsi 还提出了不需要检测写-写冲突。这也非常容易理解：没有读的 blind write 不会和任何人冲突。

> 考虑以下操作：
> `r1[x] w2[x] w1[x] c1 c2`
> 
> 如果T2没有读x，则不构成写丢失异常，因为可以重排为：
> `r1[x] w1[x] c1 w2[x] c2`

snapshot isolation 一开始为什么要设计成这样，我猜原因可能是检测成本：
1. 数据库中通常读远多于写，snapshot isolation 提出的目的也是为了降低写对只读事务的影响。
1. 写-写冲突没有额外的存储成本：数据总是要写的，写的过程中顺便就完成了冲突检测。且事务提交之后它就变成了数据历史的一部分，不需要额外的空间来保存已提交事务的版本。
1. ssi 需要保存已提交事务的 read-set，开销远大于检测写-写冲突。使用粗粒度的 read-set 则会显著增大 false positive，导致更多事务被错误地 abort。
1. wsi 需要使用中心节点来检测，同样引入了额外开销。

CRDB 的 ssi 在前人的基础之上又做了非常棒的创新：事务执行过程中检测到自己的 read-set 被破坏之后，就提升 snapshot 重试。这种重试是有上界的，保证了事务执行开销可控。这样它既避免了原始 ssi 维护 read-set 的巨大开销，又避免了引入 wsi 中的中心节点。

## 不要破坏我的 snapshot！

我们回到一开始的问题：snapshot isolation 会阻塞只读事务吗？

答案是：可能哦。

想象一下，一个只读事务 A，它在发起 snapshot read 时，遇到一个未提交的事务 B，且 T<sub>s</sub>B < T<sub>s</sub>A，这意味着：
1. 如果最终 T<sub>c</sub>B < T<sub>s</sub>A，即 B 的提交早于 A 开始，则 B 应存在于 A 的 snapshot 中。
1. 否则 B 的提交晚于 A 开始，B 不应存在于 A 的 snapshot 中。

这里的第一种情况可能出现于 B 的提交请求已经发出，但未到达执行。

此时 A 是不知道 B 将用什么 ts 提交的，它只有以下选择：
1. 等，也就意味着阻塞。
1. 不等，或者等一段时间后，直接 abort 自己（wait-die）。
1. 不等，或者等一段时间后，直接 abort 事务 B（wound-wait）。

另一方面，A 也可以选择一个比较老的 start timestamp，这样就能最大化避免被其它事务影响，但代价是读到的数据不新鲜。

无论如何，A 需要保证自己的 snapshot 不被破坏，它要么选择一个不再活跃的 snapshot（更早的 start ts），代价是可能读到过期数据；要么选择一个活跃的、还未定型的 snapshot，代价是需要仔细检查可能与之冲突的其它事务，毕竟是它自己选择了一个『虚假』的 snapshot。

假设 A 是读写事务，它能选择的 start ts 不能早于它的 read-set 中任何数据的已提交版本，否则事务一开始就直接与别人冲突了。

## 不要破坏别人的 snapshot！

考虑到异步执行，事务 A 提交时可能发现：
1. 它的 write-set 与已提交事务 B 的 write-set 有重合，且 T<sub>c</sub>A < T<sub>c</sub>B，此时如果写入会破坏 si。
1. 它的 write-set 与已提交事务 B 的 read-set 有重合，且 T<sub>s</sub>B < T<sub>c</sub>A < T<sub>c</sub>B，此时如果写入会破坏 ssi。

两种情况下，A 要么 abort 自身，要么选择更高的 commit ts 重试。

注意这种情况就是 si 优于 ssi/wsi 的地方了：si 的写-写冲突检测只需要与已提交数据的版本做比较，而 ssi/wsi 就需要想办法保留 B 的 read-set。真实系统，尤其是分布式系统很难承受这种维护代价。wsi 引入了中心节点，但这显著限制了系统的扩展能力。

以下是一些放弃精确性的改进方式：
1. （可能是 CRDB？不记得了）记录粗粒度的 read version，强迫事务 A 选择更高的 commit ts 重试。
1. （FoundationDB）引入多个 resolver 分别维护一部分事务的 read-set 从而进行分布式的冲突检测。每个 resolver 都会记录通过了自己检测的事务的 read-set，但这些事务可能没有成功提交，因此引入了更高的 false positive。另外 FoundationDB 还限制了事务存活时间，极大降低了需要记录的事务集合。

无论哪种改进，鉴于数据库本身的使用特点就是读远多于写，且写本身已经由 MVCC 维护着了，读-写冲突检测的开销因此必然高于写-写冲突。这也是 snapshot isolation 使用如此广泛的一个原因吧。

## 结语

> 你还不算入门呢

感谢这句话作者的激励；感谢提出文首问题的老师帮助我更好地理解 snapshot isolation。
