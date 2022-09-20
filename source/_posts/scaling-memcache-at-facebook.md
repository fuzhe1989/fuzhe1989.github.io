---
title:      "[笔记] Scaling Memcache at Facebook"
date:       2022-09-19 09:00:39
tags:
---

> 原文：[Scaling Memcache at Facebook](https://www.usenix.org/system/files/conference/nsdi13/nsdi13-final170_update.pdf)

**TL;DR**

在 cache coherence 方面经常被人引用的 paper。

<!--more-->

# Overview

为什么要用单独的 cache service：
1. 读请求数量远多于写（高一个数量级）
1. 多种数据源（MySQL，HDFS，以及其它后端服务）

![](/images/2022-09/memcached-01.png)

Facebook 用 cache service 的方式：
1. 作为 query cache 减轻 DB 的负担，见图 1。具体用法是按需填充（demand-filled）的 look-aside cache，即读请求先请求 cache，未命中再请求 DB，然后填充 cache；写请求直接写 DB，成功后再发请求给 cache 令对应的 key 失效（删除而未更新是为了保证幂等）。
    > [Is Memcached look-aside cache?](https://www.quora.com/Is-Memcached-look-aside-cache)
    >
    > The distinction between look-aside and look-through caches is not whether data is fetched from the cache and memory in serial or in parallel. The distinction is whether the fetch to memory on a cache miss originates from the caller or the cache. If the fetch to memory originates from the caller on cache miss, then you’re using a look-aside cache. If the fetch to memory originates from the cache on cache miss, then you’re using a look-through cache.
    >
    > 应用自己处理 cache miss 之后的 fill 就是 look-aside，cache 自己处理 fill 就是 look-through，学废了没有？
1. 作为更通用的 key-value 存储（字面理解是不再绑定到具体的后端上了？）

memcached 本身只是单机的，Facebook 将其修改为可以支持 cluster。这样，他们就多了两个需要处理的问题（在原有问题的基础上，笑）：
1. 如何协调多个 memcached 节点。
1. 如何维护 cache 与 DB 的一致性。

这也是本文的两个重点：
1. 优化
1. 一致性

# In a Cluster: Latency and Load

## Reducing Latency

通常一个业务请求会对应很多次 cache 访问，比如作者的一份数据是平均 521 次 get（P95 是 1740 次）。在使用 memcached 集群后，这些 get 会根据某些 sharding 规则（实际是一致性 hashing）分散到不同的 memcached 节点。当集群规模增大后，这种 all-to-all 的通信模式会造成严重的拥塞。replication 可以缓解单点热点，但会降低内存的使用率。

Facebook 的优化思路是从 client 入手：
1. 将数据间的依赖关系梳理为 DAG，从而能并发 batch fetch 相互不依赖的数据（平均一个请求可以 fetch 24 个 item）。
1. 完全由 client 端维护 router，从而避免 server 间的通信。
1. 

