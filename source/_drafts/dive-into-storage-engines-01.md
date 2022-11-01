为什么要有 Write Ahead Log (WAL)

> In computer science, write-ahead logging (WAL) is a family of techniques for providing atomicity and durability (two of the ACID properties) in database systems. [[Write-ahead logging]]

假设我们现在有一个 in-memory 数据结构，比如一个 hashmap。我们希望它能变成一个持久化的结构，时刻保持与磁盘数据一致，该怎么实现？

方案 1：每笔写都原地覆盖整个 hashmap

我们可以为 hashmap 实现一个序列化方法，每笔写都调用一次这种方法，将整个 hashmap 序列化并写到磁盘的一个固定文件上。

这样我们就得到了一个持久化的 hashmap。显然，它是非常低效的。

而且它还有一个正确性问题。对于稍大一点的 hashmap 而言，显然文件系统无法一次原子地将整个 hashmap 写下去，这样就存在把原本完好的文件写坏的可能。

这个时候重试可以部分解决问题，但如果重试成功之前进程 crash 或者机器宕机了，我们就可能永远无法恢复原本的 hashmap 状态了。

方案 2：使用 [shadow copy]

[Write-ahead logging]: https://en.wikipedia.org/wiki/Write-ahead_logging