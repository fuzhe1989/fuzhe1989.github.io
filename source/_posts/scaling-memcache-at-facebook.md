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

![](/images/2022-09/memcached-02.png)

# In a Cluster: Latency and Load

## Reducing Latency

通常一个业务请求会对应很多次 cache 访问，比如作者的一份数据是平均 521 次 get（P95 是 1740 次）。在使用 memcached 集群后，这些 get 会根据某些 sharding 规则（实际是一致性 hashing）分散到不同的 memcached 节点。当集群规模增大后，这种 all-to-all 的通信模式会造成严重的拥塞。replication 可以缓解单点热点，但会降低内存的使用率。

Facebook 的优化思路是从 client 入手：
1. 将数据间的依赖关系梳理为 DAG，从而能并发 batch fetch 相互不依赖的数据（平均一个请求可以 fetch 24 个 item）。
1. 完全由 client 端维护 router，从而避免 server 间的通信。
1. client 通过 UDP 发送 get 请求，从而降低延时。数据显示在高峰期 0.25% 丢包率。set 和 remove 仍然走 TCP。

    ![](/images/2022-09/memcached-03.png)
1. client 端使用滑动窗口来控制发出的请求数量，避免 response 把机架或交换机的网络打满。滑动窗口的拥塞策略类似于 TCP，即快下降+慢上升。与 TCP 不同的是，滑动窗口针对进程的所有请求生效，而 TCP 则是针对某个 stream 生效。如下图可见，窗口太小，请求并发上不去；窗口太大，容易触发网络拥塞。

    ![](/images/2022-09/memcached-04.png)

## Reducing Load

### Leases

作者引入 lease 是为了解决两类问题：失效的 set；惊群。前者发生在多个 client 并发乱序 update 一个 key 时。后者发生在一个很热的 key 不停被写，因此不停被 invalidate，读请求不得不反复请求 DB。

每个 memcached 实例可以为某个 key 生成一个 token 返回给 client，client 后续就可以带着这个 token 去更新对应的 key，从而避免并发更新。memcached 会在收到 invalidation 请求后令对应 key 的 token 失效。

为了顺便解决惊群问题，作者对 lease 机制加了个小小的限制：每个 key 最多每 10s 生成一个 token。如果时间没到就有 client 在 cache miss 下请求 token，memcached 会返回一个特定的错误，让 client 等一会重试。通常这个时间内 lease owner 就有机会重新填充 cache，其它 client 下次请求时就会命中 cache 了。

另一个优化是 memcached 在收到 invalidation 请求后，不立即删除对应的数据，而是暂存起来一小会。这期间一些对一致性要求不那么高的 client 可以使用 stale get 获取数据，而不至于直接把寒气传递给 DB（笑）。

### Memcache Pools

单独的 cache service 就要能承载不同的业务请求，但很多时间业务之间相互会打架，导致某些业务的 cache 命中率受影响。作者因此将整个 memcached 集群分成了若干个 pool，除了一个 default pool，其它的 pool 分别用于不同的 workload。比如可以有一个很小的 pool 用于那些访问频繁但 cache miss 代价不高的 key，另一个大的 pool 用于 cache miss 代价很高的 key。

作者给的一个例子是更新频繁（high-churn）的 key 可能会挤走更新不频繁（low-churn）的 key，但后者可能仍然很有价值。分开到两个 pool 之后就能避免这种负面的相互影响。

### Replication Within Pools

