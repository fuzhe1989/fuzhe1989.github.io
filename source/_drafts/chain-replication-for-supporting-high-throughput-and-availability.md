---
title:      "Chain Replication for Supporting High Throughput and Availability"
date:       2022-06-28 19:18:58
tags:
---

**TL;DR**

Chain replication 是一种分布式共识算法，它具备高吞吐、强一致、容错能力强、读写负载分离等优点，但同时也有写入延时高的缺点。这些特点令 CR 特别适合用于跨 data center 的 geo replication。

> 原文：[Chain Replication for Supporting High Throughput and Availability](https://www.usenix.org/legacy/events/osdi04/tech/full_papers/renesse/renesse.pdf)

<!--more-->

## 背景

- Chain replication 的目标是维护数据多个副本的一致性。
- CR 自身无法处理配置变更（如维护 membership），需要依赖外部服务。
- CR 的一个基本观点是分布式环境下 client 无法区分丢失的请求与被 server 丢弃的请求，因此 server 可以任意丢弃未回复 client 的请求 ———— 只要保证系统状态正确。

## 基本原理

CR 的原理非常简单：
- 将数据的 K 个副本链式排列，从 head 到 tail。
- 写请求由 head 开始处理，顺着链以 FIFO 的方式直到写 tail 成功。
- 读请求只由 tail 处理。

![](/images/2022-06/chain-replication-01.png)

通过这种简单的策略，CR 保证了以下性质：
- 每个副本收到的写请求序列都是前一副本的前缀，即如果副本 i 收到了某个写请求 r，则 i 之前的每个副本也都收到了 r。
- 只有所有副本都写成功的修改才会被读请求看到。

可见，CR 通过以上性质保证了线性一致性。但这还不够，我们还没有处理出错场景。

## 容错

CR 假设 server 满足 fail-stop：
- server 遇到错误时会退出，而不是停留在某种错误状态。
- server 的退出是可被外界检测到的。

我们需要保证的是：
- 对于 K 个副本的 chain，在最多 K-1 个副本出错时，不会丢失已经提交的数据（对 client 可见）。
- 所有副本最终可以达到一致的状态。

CR 将出错情况分为了三种：
1. head 出错。
2. tail 出错。
3. 中间副本出错。

### head 出错

当 head 出错时，我们只要简单地将 head 副本移除，令 head 的后继（head+1）成为新的 head 即可。

由上面的性质可知：
1. 未被 head+1 收到的修改，一定还未到达 tail，因此对 client 不可见，可以放心丢弃。
2. head+1 的状态是它所有后继副本的超集，因此只要它持续转发后继缺失的修改，最终所有副本都可以到达一致的状态。

### tail 出错

当 tail 出错时，我们只要简单地将 tail 副本移除，令 tail 的前驱（tail-1）成为新的 tail 即可。

由上面的性质可知：
1. tail-1 包含 tail 的所有状态，因此之前对 client 可见的修改现在仍然可见。
2. tail-1 可能包含 tail 还未收到的修改，此时这些修改相当于提前执行完成，也不会影响正确性。

### 中间副本出错

当我们要从 chain 的中间移除副本时（假设编号为 i），可能会有一些写请求已经被 i-1 发给了 i，但还未被发给 i+1。这就可能导致后续 i-1 与 i+1 状态不一致。这就需要每个副本有能力

## References

- https://medium.com/coinmonks/chain-replication-how-to-build-an-effective-kv-storage-part-1-2-b0ce10d5afc3
- https://medium.com/swlh/replication-and-linearizability-in-distributed-systems-cd9036ea7b40
- http://nil.lcs.mit.edu/6.824/2020/papers/craq-faq.txt
- https://engineering.fb.com/2022/05/04/data-infrastructure/delta/