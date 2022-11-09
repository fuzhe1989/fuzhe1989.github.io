---
title:      "续：如何装作懂 Snapshot Isolation"
date:       2022-11-01 22:32:15
tags:
---

前同事对[前文](/2022/11/01/how-to-pretend-to-understand-snapshot-isolation/)的评价：

> 反正那篇没完全看懂，愤怒的没点赞
>
> wsi ssi 出来的太生硬了

于是这篇我们继续从直觉出发探索 snapshot isolation，看看能不能让同事给我点赞。

<!--more-->

## global consistent snapshot

前文提到了分布式系统的 snapshot isolation，但却遗漏了最重点的概念：global consistent snapshot。

分布式系统中，每时每刻都可能有多个事务在运行，事务之间可能完全无重合（访问不同节点、不同数据），也可能有重合（访问相同节点、相同数据）。我们要保证每个事务的 snapshot 是符合某种 consistency 的，而不是相互矛盾的。

[ConfluxDB](http://www.vldb.org/2014/program/http://www.vldb.org/pvldb/vol7/p947-chairunnanda.pdf) 使用了以下规则来判断两个事务的 snapshot 是否一致：
1. 如果事务 T<sub>i</sub> 与 T<sub>j</sub> 都访问了某节点，且从这个节点上看到 T<sub>i</sub> start < T<sub>j</sub> commit，则从所有两者交集的节点上都能看到 T<sub>i</sub> start < T<sub>j</sub> commit。
1. 类似地，所有节点看到的 T<sub>i</sub> commit 和 T<sub>j</sub> start 的关系也要一致。
1. 类似地，所有节点看到的 T<sub>i</sub> commit 和 T<sub>j</sub> commit 的关系也要一致。

