---
title:      "[笔记] LocoFS: A Loosely-Coupled Metadata Service for Distributed File Systems"
date:       2022-09-07 11:22:53
tags:
---

> 原文：[LocoFS: A Loosely-Coupled Metadata Service for Distributed File Systems](https://dl.acm.org/doi/pdf/10.1145/3126908.3126928)

**TL;DR**

LocoFS 的出发点：
1. 降低 metadata 操作的网络延时。
1. 充分发挥 kv-store 的性能。

LocoFS 做了以下两种优化：
1. 将 dir entry 与 child 的 inode 放到一起管理，更有利于将整个 namespace 的树型结构平坦化为 key-value 结构。
1. 将 file metadata 分为 access 与 content 两类，降低单次操作的粒度。

LocoFS 整体上来看对系统限制还是比较强（限制只有一个 DMS），但思路还不错。

<!--more-->

## Motivation

![](/images/2022-09/locofs-01.png)

作者发现越来越多的 fs 开始用 kv-store 来保存 metadata，但远没能发挥出 kv-store 的性能，原因：
1. 根本矛盾是将树型结构映射到平坦的 key-value 结构上导致单个操作需要经过多个节点，此时性能瓶颈是过高的网络延时（trip 过多）。
1. kv 的序列化/反序列化成本过高。如果 fs 自己维护一个 cache（如 IndexFS）就与 kv-store 的 cache 重复，反倒浪费了内存带宽（cache 两次）。

## Design and Implementation

## Loosely-Coupled Architecture

![](/images/2022-09/locofs-02.png)

LocoFS metadata 架构包括：
1. 单个 DMS（Directory Metadata Server）
1. 若干个 FMS（File Metadata Server）

其中 DMS 必须是一个的原因应该是更容易实现 rename。DMS 会在 kv-store 中维护 path -> d-inode，意味着 rename 需要修改整个 subtree 的所有节点。单个 DMS 可以很高效地完成这项工作，而多个 DMS 管理成本就会非常高。

DMS 中 path -> inode 维护为了一棵 B+ 树，这样 rename 本身只涉及其中一个中间节点的移动：

![](/images/2022-09/locofs-05.png)

> 这个设计很赞。另外它也是 LocoFS 的树的平坦化的核心，即将树结构藏到 DMS 中，才能让别的部分保持平坦 kv 结构。但这种架构下 DMS 很容易成为瓶颈，很难支持很高的规模。

DMS 维护的映射：
- path -> d-inode
- dir_uuid -> [dir-entry, dir-entry, ...] （所有 children 的 dir-entry 拼成一个 value）

f-inode 会根据 dir_uuid + file_name 一致性 hash 到某个 FMS 上。

FMS 维护的映射：
- (dir_uuid + file_name) -> f-inode
- 某个 FMS 上所有 file 的 dir-entry 会拼在一起（是吧？）

![](/images/2022-09/locofs-03.png)

> 这里没看懂 DMS 和 FMS 拼接 dir-entry 的好处是什么。
> 
> 原文：In the DMS, all the subdirs in a dir have their dirents concatenated as one value, which is indexed by the dir_uuid key. All the files that are mapped to the same FMS have their dirents concatenated and indexed.

此外每个 d-inode 和 f-inode 中也会保存指向 parent 的 dir-entry，这样类似于倒排索引，结合 path 映射，可以有效分担 parent 的压力。

这套架构上 DMS 的压力会比较大，所以 client 一侧也会积极 cache d-inode。为了保持一致性，所有 client 会保持与 DMS 的 lease。

## Decoupled File Metadata

![](/images/2022-09/locofs-04.png)

LocoFS 根据用途将 file metadata 分成了 access 与 content 两部分，大多数操作只需要访问其中一部分，这样明显降低了 kv-store 的访问压力。

另外，为了降低序列化/反序列化开销，LocoFS 将 file metadata 组织为所有字段都定长的 struct，直接将 struct 本身作为 value 写进去而不需要序列化；读某个字段时也不需要反序列化。

> 1. 缺点是牺牲了一些压缩的可能性（但对 metadata 压缩意义可能不大）
> 1. 有些类似于使用 flatbuffers。
> 1. 另外这里看起来很适合 PAX style 的 store：schema 固定；每列的数据高度相似；经常只需要更新少量字段。

此外，LocoFS 也没有保存 file 的 block 映射表，而是直接用 file_uuid + block_num 作为 block 的 key 去访问对象存储。

> 缺点是 truncate 需要是同步的（甚至需要持久化），否则会读到已失效的 block。
