---
title:      "一种分布式文件系统的存储层设计"
date:       2022-06-08 23:08:27
tags:
---

**TL;DR**

<!--more-->

## 场景

需要支持：
1. overwrite。
1. 多个 client 并发写。
1. RDMA read/write。
1. 高并发读取一个文件的不同区域。

workload 特点：
1. 真正的 overwrite 极少。
1. 读远多于写。
1. 遇到写失败时，server 可以放宽一致性要求，由 client 兜底。

## Overview

### 文件组织

每个文件对应若干个定长的 chunk，每个 chunk 可以有 K 个 replica，分布在不同的 target 上。chunk 是调度单位。

为了支持“高并发读取一个文件的不同区域”，每个文件会先按地址空间水平切分为若干个定长的 segment，每个 segment 内部再垂直切分为固定数量个 chunk。

![](../images/2022-06/storage-design-01.png)

上面这个例子中，文件先按 16MB 切分为若干个 segment，每个 segment 内再分成 4 个 chunk，每个 chunk 管理 8 个长度为 512KB 的 subrange。这样做的好处：
1. 通过 offset 可以唯一确定 chunk。
1. client 需要读大块数据时，可以根据 subrange 切分，从不同 target 并发读取，充分利用多台机器的带宽。

实际设计中，我们会选择使用更大的 chunk 与 segment，如：
1. 每个 subrange 仍然是 512KB。
1. 每个 chunk 管理 128 个 subrange（64MB）。
1. 每个 segment 包含 16 个 chunk（1GB）。

优点：
1. 更大的 chunk 意味着更少的 metadata，减轻 metadata 的管理负担。
1. 更大的 segment 意味着更高的读取并发，如 16 个 chunk 意味着可以 16 并发读取 8MB 的连续数据。

缺点：
1. 更大的 chunk 意味着更高的 replica 复制成本。
1. 更大的 segment 意味着更高的初始分配成本，尤其小文件较多时。

### 系统架构

系统中有若干个 storage server，每个 storage server 的每块盘为一个 target。每个 target 管理若干个 chunk。meta server 会指定其中一个 target 为 primary，其它 targets 为 secondary。

每个 target 的存储分为两类：
1. 每个 chunk 对应一个 local file。
1. 每个 target 维护一个小的 local kv engine（如 rocksdb），用于持久化 target 的各种 meta。

每个 storage server 与 management node 之间有心跳，相互之间不维护心跳。

### chunk metadata 管理

> 注意：segment 只是虚拟单位，不对应真正的 metadata。

每个 chunk 对应两类 metadata：
1. inodeid + index -> chunk_id。
1. chunk_id -> chunk_info，后者包含：replica -> target，每个 replica 的状态（online/offline），以及谁是 primary。

> 另一种思路是 chunk 不要有自己的 id，直接用 inodeid+index，但这样 truncate 就会比较复杂。

另外，每个 target 也要记录 target -> chunk_id。

> 我们的 meta server 是无状态的，没有 leader 概念，所以需要持久化 target -> chunk_id 的映射以加速 failover 时 offline replica 的操作。

## Details

### 一致性保证

1. client 如果写成功，则后续一定能读到此次写。
1. 多个 replica 中的相同区域如果初始状态一致，经过若干次成功的写入后，结束状态也一致。
1. 多个 client 并发写文件同一区域，不保证相互顺序。
1. 有 client 正在写文件某一区域时，对这一区域的读不保证结果的原子性。
1. 如果有 replica 写失败，不保证写入区域的状态（即不保证原子性）。

### 写入流程

整体写入流程：
1. （针对 append）client 与 meta server 通信，将文件 truncate 到想要的 size（可能触发分配 chunk）。
1. client 根据要写的范围获得对应的 chunk list。
1. 对每个 chunk：
    1. client 通过 RDMA read/write 将数据传送给三个 target，其中包含此次写的 token。
    1. client 向 primary 发送 commit rpc，其中包含此次写的 token。
    1. primary 为此次写分配一个 commit version，与 token 一起发送给每个 secondary。
    1. secondary 写成功之后返回给 primary。
    1. primary 收集每个 secondary（包括自己）写的结果，返回给 client。
    1. 如果所有写都成功，client 继续下面的操作；否则进入容错流程。

每个 replica 上的写入流程：
1. target 需要根据 commit version 顺序处理同一 subrange 上的写入。
1. target 没有 wal，每次写直接写到对应的 chunk_file 上。
1. target 更新对应 subrange 的 meta：chunkid+subrange_index -> commit_version。

### 读取流程

整体读取流程：
1. client 与 meta server 通信（或读取本地缓存），获得文件对应的 inode。
1. client 与 meta server 通信（或读取本地缓存），获得要读的范围对应的 chunk list。
1. 对每个 chunk：
    1. client 挑选某个 target，通过 RDMA read/write 获得需要的数据。

每个 replica 上读取某个 chunk 时，**不需要**加任何锁：并发读写时读不保证原子性。

### 容错流程

#### 基本原则

1. primary 在遇到 secondary 失败时会尽量重试。如果重试仍不成功则将结果返回给 client。**但 primary 本身不会直接 offline secondary**。
1. primary 写入时会跳过 offline 的 secondary，但不会跳过 online 的 secondary（即使上轮写入失败）。
1. offline 的 secondary 可以通过 sync 重新回到 online。sync 过程中 primary 会暂停写入。
1. meta server 会将 chunk_info 的变更通知给 primary。
1. 只有 management node 可以

1. 每个 replica 会重试本地的写入。
1. primary 会重试 secondary 的写失败（主要是 timeout）。
1. management node 发现 storage server 心跳断开后，如果超过指定时间 storage server 仍未恢复，则通知 meta server 将这个 storage server 对应的所有 replica 设置为 offline。
1. meta server 在 offline replica 时，可以根据配置决定要不要增加 replica。replica 状态的修改也会同步给 primary，primary 不再尝试写 offline 的 secondary。

storage server 在重新建立心跳后，需要先确认自身所有 replica 状态。对于已经 offline 的 replica，再从对应的 primary 同步数据。整个同步期间 primary 会暂停写入以简化状态管理。chunk 同步时，每个 subrange 可以通过 commit version 来判断自身是否过期，从而减小需要同步的数据量。

> 存在一种情况，不同 replica 上相同的 commit version 对应的数据不同。这是因为修改 chunk_file 与持久化 commit version 不是原子操作。但此时 client 一定没有收到写成功，因此不同 replica 的状态不一致是可以接受的。

新增 replica 也会走 recovery 流程，即先 offline，同步好之后再切换到 online。

在 offline replica 之前，对应 chunk 的写操作均会失败。此时如果 client 可以容忍短时间降级写，则可以自行 offline replica。

如果 primary 本身被标记为 offline，meta server 会立即从 online 的 secondary replica 中随机挑一个升级为 primary。我们的写入流程保证了这个 replica 一定拥有所有被 client 确认写成功的数据。

存在一种情况：client a 写操作失败，标记 replica r1 为 offline，之后继续写数据；r1 所在的 storage server 心跳未断；client b 读 r1，未意识到 r1 已经 offline。此时 client b 可能无法读到 client a 的后续写入。

此时我们可以：
1. 强制只读 primary。
1. secondary 每次读都与 primary 通信，确认自己没有被 offline。
1. 不保证多个 client 并发读写的一致性。
