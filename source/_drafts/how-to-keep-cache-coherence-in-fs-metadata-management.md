---
title:      "如何实现文件系统元数据的缓存一致性"
date:       2022-05-29 22:01:19
tags:
---

**TL;DR**

Posix-like 文件系统的一个性能瓶颈是元数据访问。常见的分布式 cache + DB 的实现中，一大挑战就是如何既保证性能又保证读写的强一致性（如 rename、remove 等）。这实际上是分布式一致性问题。本文介绍了如何使用分布式共识算法来解决此问题，并比较了 follower read 与 lease 两种方法的优缺点。

<!--more-->

## 背景

Posix-like 文件系统中，元数据操作可以占据多达 50% 的负载，经常成为性能瓶颈[^1]。我们经常会使用已有的 Database 来保存元数据，从而享受到这些系统提供的 ACID。但如前所述，令成百上千的 client 直接访问 DB 在性能上是不可接受的。

实际系统中，读请求通常远多于写，最直接的优化手段就是为 DB 增加一层 cache layer。通常为了足够的读性能提升，这层 cache 不止一个节点，因此会是分布式cache。难点就在于如何在增加了分布式 cache layer 之后仍然保证读写的强一致性。

DB 本身的一致性不在本文范围。我们要探讨的是，当加上一层 cache 后，如何在提升性能的同时，仍然保证 serializability，即：任意 client 的写操作返回成功之后，其它所有 client 的读操作保证读到最新的值。

这是一个非常经典的 cache invalidation 问题。

## Cache invalidation

> There are only two hard things in Computer Science: cache invalidation and naming things. [^2]

常见的 cache 更新策略有两种：
1. look-aside cache：读请求未命中 cache 时，由 client 直接去读 DB，再将结果写回 cache。
1. look-through cache：读请求未命中 cache 时，由 cache 自己去读 DB，本地更新之后再返回给 client。

> 以上两种策略假设写操作只更新 DB，不更新 cache。

而 cache 写入策略也有两种：
1. write-through：写操作同时更新 cache 与 DB。
1. write-back：写操作只同步更新 cache，cache 随后异步更新 DB。

假设我们要实现的是一个分布式的 cache：
1. write-back 无法保证写操作一定能持久化到 DB 中，首先被排除。
1. look-through 无法保证 DB 的更新同步到所有 cache 节点，也要排除。
1. look-aside 当一个 key 被多次更新时，无法保证后续更新被写回所有 cache 节点，也要排除。

这样只剩下一种 cache 策略：write-through。

write-through 的难点在于：
1. 同一个 key 的多次更新，如何保证 cache 中的更新顺序与 DB 的顺序相同。
1. 如何保证 DB 的修改一定同步给所有 cache。

可以看出这实际是一个分布式一致性问题，需要引入某种共识算法（如 Paxos/Raft）。对此我们有两种实现：
1. cache 节点之间组成若干个 consensus group，在写 DB 成功之后，通过 consensus 协议将最新值同步给所有成员。
1. 

## References

[^1]: Roselli, Drew, Jacob R. Lorch, and Thomas E. Anderson. "A comparison of file system workloads." 2000 USENIX Annual Technical Conference (USENIX ATC 00). 2000.
[^2]: [Martin Folwer: TwoHardTings](https://martinfowler.com/bliki/TwoHardThings.html)
