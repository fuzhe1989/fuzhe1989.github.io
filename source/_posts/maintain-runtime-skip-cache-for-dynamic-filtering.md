---
title:      "一种通过 skip cache 加速重复查询的方法"
date:       2022-08-03 21:33:18
tags:
---

**TL;DR**

AP 系统中缓存算子结果是一种很有效的针对重复查询的优化手段。但这种方法严重依赖于结果的不变性，因此并不适用于频繁更新的场景（如 TiFlash）。本文提出一种通过维护运行期的 skip cache，尽可能跳过无效 page 的优化方法，**应该**对这种场景有效。

> 有 paper 已经讲过这种优化的话求告知。

<!--more-->

## 背景

任何缓存结果的方法，其核心都是要寻找到某种不变的东西。传统的离线数仓系统，其数据更新频率极低，相同 query plan 通常扫过的数据集是不变的。如果 plan 中不存在非幂等的函数/算子（通常是这样），则每个算子的结果也是不变的。这是缓存算子结果的基础。

但在 TiFlash 中，我们需要考虑到用户可能频繁在更新数据，同时 query plan 也很难保证稳定。因此直接缓存算子结果可能并不合适。

> 虽然但是，有机会还是得搞，见文末。

materialize 这样的实时物化是另一种方案，但计算开销较大。而且人家属于另一个赛道了。我们还是从简单的优化入手。

## 从 skip index 到 skip cache

Skip index 是 AP 系统非常关键的模块，它的作用是记录每个 page（或者叫 block）的一些 metadata（如 min/max 等），在 TableScan 时提前过滤那些不可能包含所需数据的 page，从而节省大量 IO 与计算资源。

> 为什么 AP 系统通常不使用 secondary index 来加速查询？
> 1. 维护 secondary index 的代价是非常高的。
> 1. 查询 secondary index 时通常会引入大量随机 I/O（无论是读 index 还是回表时）。而 AP 系统可以通过 skip index 跳过大量无效的 page，而对剩余部分进行顺序扫描的效率是非常高的（I/O 和 cache 友好）。

skip index 的缺点是它通常只能包含静态数据，一旦遇到复杂一点的表达式就无能为力了（比如 `year(a) = 2022` 或者 `concat(a, b) like '%PingCAP%'`)。对于这种复合表达式的 filter，很多系统（如现阶段的 TiFlash）只能无脑扫全表了。

一种很直接的想法就是，如果我能知道一个表达式是否命中了一个 page，就可以在下次遇到这个表达式时提前排除掉对应的 page，重新让 skip index 生效。显然这种信息是非常动态的，不适合持久化，只能放到内存中。

方案一：在内存中缓存每个表达式扫描未命中的 page list。

注意，这个方案中，page 指的是 stable file 中的 page。如前所述，我们要缓存结果，就要找到某种不变的东西。TiFlash 是按 delta 和 stable 来划分数据的，前者变化频繁，后者变化较少，显然我们只能针对 stable 来缓存。恰好，TiFlash 的 skip index 也是只存在于 stable 部分的。

stable 只是变化不那么频繁，不代表它永远不变。方案一要保证不能跳过新生成的 page，就要记录未命中的 page list，将所有新 page 视为可能命中。这对方案的实现提出了要求：
1. 能识别哪些 page 是在缓存更新之后生成的。通常记录某种单调增的 version 即可。
1. 每个 page 要有稳定的唯一标识，即新 page 不能重用老 page 的标识。这个可以用 fileid+pageid 来实现。

skip cache 可以用 LRU 或 LFU 等策略管理。

## 降低内存占用

方案一的缺点是当 page 很多时，内存占用较高。我们需要有办法将 page list 占用的空间降下来。我们可以将 page list 替换为 bloomfilter，后者通过引入适量的 false positive 来降低空间占用。

> 另一种类似的结构是 cuckoo filter。

但换用 bloomfilter 之后，我们就要将方案一中记录未命中的 page list 改为记录命中的 page list 了，否则 false positive 会导致有 page 被错误地跳过。

方案二：在内存中缓存每个表达式扫描命中的 page list 对应的 bloomfilter。

同样地，方案二也要求显式处理新生成的 page。

## 在分布式 plan 中寻找不变量

上面的方案只是针对单个 TableScan 算子。接下来我想做点不成熟的探讨。

我们知道分布式 plan 对各种结果 cache 都不太友好：
1. 数据可能更新，而 planner 很难及时知道这点。
1. 数据分布可能变化，同上。
1. 参与计算的节点可能变化。

但总还是有些不变量存在的，我们要做的就是充分地将其挖掘出来。

对于 TiFlash 而言：
1. 首先 planner 要能知道 query 是否存在缓存。如果做得好的话，可以针对 subplan 设置缓存。
1. 接下来，planner 是知道这次需要扫描的 region list 的，它需要知道从上次请求到当前 tso，这期间哪些 region 数据没有发生变化。
1. 接下来，针对这些没有变化过的 region，自底向上计算每个算子的输入是否可能发生变化。
1. 接下来，计算每个 task 命中缓存的收益，从而决定要不要生成相同的 task 且分发给相同的 node。

即使如此，如果算子产生的数据过多的话，需要将其物化才能重复利用，开销一下子就上去了。

从上述内容可以看出，想用上算子的缓存还是不太容易的。
