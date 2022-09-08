---
title:      "[笔记] Order-Preserving Key Compression for In-Memory Search Trees"
date:       2022-08-24 12:42:00
tags:
---

> 原文：[Order-Preserving Key Compression for In-Memory Search Trees](https://dl.acm.org/doi/pdf/10.1145/3318464.3380583)

**TL;DR**

本文提出了一种针对字符串的分段编码框架 HOPE（High-speed Order-Preserving Encoder），在构建初始字典之后，可以流式编码任意字符串。且，重点来了，编码之间仍然保持原有字符串的顺序。这样 HOPE 的适用范围就不仅仅是静态的压缩已有数据了，它还能直接与各种树结构结合，直接用编码后的值作为 key。这样的好处有：
1. 对于 B-tree 等，更短的 key 意味着更大的 fanout。
1. 对于 Trie 等，更短的 key 意味着更低的高度。
1. 节省空间有助于在内存中维护更多数据（如 cache 等）。

<!--more-->