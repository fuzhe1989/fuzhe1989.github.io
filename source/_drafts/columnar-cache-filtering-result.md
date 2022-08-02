---
title:      "一种缓存 filter 结果的方案"
date:       2022-06-28 19:18:58
tags:
---

**TL;DR**

<!--more-->

> 我之前有想法是在 tiflash storage 里面维护一个 cache
>
> key 是某个幂等的 filter 表达式，比如 eq(plus(cast(a as int), 10), 100) 这种
>
> 然后用一个 bloomfilter 记录哪些 stable 的 page **完全不包含对应的数据**
>
> 这样下次这个表达式过来的时候，先正常用 min/max 来过滤，再用这个 filter cache 过滤掉不要的 page
>
> filter cache 单位是 page
>
> 哦我说那个 filter 可以简单设置一个容量，按 lru 淘汰就行了。expr -> bloomfilter，后者的大小正比于 page 数量
> 
> 针对的是 stable page，所以只在 compaction 时需要更新。甚至可以 lazy 更新，如果能确保每个 page 有唯一标识的话。
>
> https://pages.databricks.com/rs/094-YMS-629/images/Fine-Grained.pdf
> 
> 它是想通过 filter 的值来让 skip index 效果最大化。比如同样是有 10 条有效的数据，如果分布在 10 个 block 中，即使有 sparse index，我们还是要把所有 block 读上来。如果重新分布数据，让这 10 条数据在一个 block 中，其它 9 个 block 自然就被过滤掉了，不需要读。
> 
> 是的，而且针对 adhoc query 没办法有一个稳定的分布，于是我就想能不能 cache filtering result，这样也能解决 sparse index 无法用在包含子表达式的 filter 的问题