---
title:      "[笔记] Anti-Caching: A New Approach to Database Management System Architecture"
date:       2022-08-25 12:04:25
tags:
---

> 原文：[Anti-Caching: A New Approach to Database Management System Architecture](https://dl.acm.org/doi/pdf/10.14778/2556549.2556575)

**TL;DR**

Anti-Caching 是基于 H-Store 实现的 in-memory db。作者认为传统的 RDBMS 是以磁盘为主存，内存为缓存（cache），而 in-memory db 则应该反过来，内存才是主存，磁盘只是用于 swap，数据不会同时存在于两个地方，可以认为“没有 cache”，因此称其为 “anti-caching”。

<!--more-->

事务仍然继承了 H-Store “只支持存储过程”的设计，每个 partition 严格按事务 ID 顺序串行执行，因此不需要死锁检测。

每个 partition 使用一个 LRU list 记录所有内存中的 tuple。为了降低开销，作者引入了抽样，只有被抽样的事务才会真正去修改 LRU list。

不在内存中的 tuple 会被记录在 evicted table 中，其中每个 tuple 会记录所属的 blockid 与 offset。

Block 的 metadata 常驻内存，它的 value 部分也是尽量贴近内存中的数据格式（比如可能没有压缩？）。

事务执行过程中，如果遇到 evicted tuples，就会进入 pre-pass 状态。这个状态下，事务会继续执行完，但在结束时会 rollback 自己的所有改动，然后开始加载遇到的所有 evicted tuples，事务自身重新 enqueue，等待第二次执行。某些情况下这个过程会重复多次（比如依赖 evicted value 做过滤），但作者表示很罕见，可以接受。

Anti-caching 保证事务重新被运行之前它需要的这些 tuples 都会被加载好，且会 pin 在内存中（所以事务间存在内存容量的争抢）。

Evicted tuples 的加载是以 block 为粒度的，当加载好之后，它会被当作一个事务 enqueue，这样确保更改 tuple table 的时候不会有事务在执行（回忆 H-Store 事务是串行执行的）。这里有两种 merge 策略：
- block-merge，将整个 block 的所有 tuple 都 merge 进 tuple table，其中被需要的 tuple 放到 LRU 的尾，其它顺带加载的放到 LRU 头。这种策略的问题在于开销比较大（尤其是如果只有非常少量 tuple 需要读）。另外它对 tuple table 的冲击也比较大，可能导致不停的换入换出。
- tuple-merge，只 merge 需要的 tuple。这样 tuple 就可能同时存在于内存和磁盘中（但作者表示问题不大）。另一个可能的问题是磁盘上的 block 就相当于存在空洞，因此 anti-caching 有一种 lazy-compaction，当 block 的空洞足够大的时候，直接将这个 block 的剩余部分加载上来。

分布式事务：依赖于单个 partition pin 住内存，等到所有参与的 partition 都加载好再一起执行。（聊胜于无）

Snapshot & Recovery：略。