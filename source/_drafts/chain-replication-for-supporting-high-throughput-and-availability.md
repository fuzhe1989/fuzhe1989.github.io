---
title:      "Chain Replication for Supporting High Throughput and Availability"
date:       2022-06-28 19:18:58
tags:
---

**TL;DR**

Chain replication 是一种分布式共识算法，它具备高吞吐、强一致、容错能力强、读写负载分离等优点，但同时也有写入延时高的缺点。这些特点令 CR 特别适合用于跨 data center 的 geo replication。

> 原文：[Chain Replication for Supporting High Throughput and Availability](https://www.usenix.org/legacy/events/osdi04/tech/full_papers/renesse/renesse.pdf)

<!--more-->

Chain replication 的目标是维护数据多个副本的一致性。它的原理非常简单：
- 将数据的 K 个副本链式排列，从 head 到 tail。
- 写请求由 head 开始处理，顺着链以 FIFO 的方式直到写 tail 成功。
- 读请求只由 tail 处理。

![](/images/2022-06/chain-replication-01.png)

## References

- https://medium.com/coinmonks/chain-replication-how-to-build-an-effective-kv-storage-part-1-2-b0ce10d5afc3
- https://medium.com/swlh/replication-and-linearizability-in-distributed-systems-cd9036ea7b40
- http://nil.lcs.mit.edu/6.824/2020/papers/craq-faq.txt