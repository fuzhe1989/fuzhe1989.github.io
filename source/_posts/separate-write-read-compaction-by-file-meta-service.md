---
title:      "解耦NoSQL系统的写、读、Compaction"
date:       2020-12-05 13:50:58
tags:
    - 脑洞
    - NoSQL
    - Raft
---

## TL;DR

NoSQL系统中的一个常见问题就是写、读、compaction三类操作的资源争抢导致服务质量受到影响。这里脑洞一个方案，通过单独的FileMeta Service来管理文件的元数据与生命期，从而解耦前面三类操作，实现资源的隔离，提升服务质量。

进一步地，分离三类操作后，我们甚至可以针对不同操作使用不同的分区策略，从而进一步解耦三类操作，释放出更高的能力。

> idea来源于我在阿里云Tablestore时的一次讨论，参考了Google的Procella和F1-lightning的实现方式。

<!--more-->

## 架构

> TODO：补架构图

系统中数据按表组织，每张表按key range分为若干个逻辑partition。每个逻辑partition实际对应三种物理partition：
- 1个primary partition，服务写请求和要求完整数据的读请求，管理WAL。
- N个read-only partition，服务snapshot读请求。
- 1个compaction partition，定期compact数据文件。

系统中有三类角色：
- 1个Master，服务DDL请求（CREATE、DROP、ALTER），负责调度物理partition。
- M个FileMeta Server，管理所有逻辑partition的数据文件的元数据和生命期。
- N个Data Server，加载三类物理partition，服务用户的读写请求。

整个系统采用计算存储分离架构，所有节点都是计算节点。数据本身保存在分布式文件系统中（如Pangu、HDFS等DFS），文件一旦写完之后就不可更改。

## 设计

### FileMeta Service

FileMeta Service负责管理整个系统中所有的数据文件（不包括Master的）。FileMeta Service是整个系统的核心，要能比Data Server有更高的可用性。这里我们假设它是一个极高可用的黑盒服务。

它保存的数据格式为`<table_id, part_id, file_id> -> file_meta`。可以用非常简单的KeyValue Store来作为它的底层存储。

其中file meta的结构中需要包含以下信息：
- is_link_file，是否是链接文件。
- ref_count，被引用了多少次（初始是1），只对非链接文件有作用。
- path，对应文件系统中的路径。

链接文件是在分区的分裂合并过程中产生的，即分裂与合并操作的目标分区中的文件都是源分区的链接文件。所有链接文件都会指向非链接文件，即如果原本链接文件B指向非链接文件A，此时创建C到B的链接会产生一个C到A的链接。

链接文件因为不对应真实文件，在清理时只需要删除meta即可。非链接文件会在其ref_count变为0后异步清理掉。

FileMeta Service可以由一个或多个Server组成，只要按table_id进行sharding，满足同一张表内的文件操作保证是原子操作即可，另外最好能保证所有file meta都常驻内存。

FileMeta Server需要支持以下针对单个分区的操作：
- 追加新文件，参数为新增文件列表，只由primary partition调用。
- 变更文件列表，参数为新增文件列表与删除文件列表，只由compaction partition调用。
- 获取文件列表，三类partition都会调用。primary partition可以通过这个列表得知整个partition的checkpoint。

每个分区需要串行处理这三种操作。分区间可以并行。

每个分区可以有一个version语义，每次文件列表变化都会产生一个新version，这样获取文件列表操作还可以返回一个diff（可选）。

另外还需要支持以下针对多个分区的操作：
- 分裂一个分区，参数为一个源分区和若干个目标分区，操作结果是源分区对应的所有file meta会被链接到每个目标分区。
- 合并若干个连续的分区，参数为若干个源分区和一个目标分区，操作结果是所有源分区对应的file meta会被链接到目标分区。
- 删除链接文件，参数为一个链接文件，操作结果是该链接文件被删除，其指向的非链接文件的ref_count减1。这项操作只由前面的变更文件列表操作间接触发。

每张表需要串行处理这两种操作。表之间可以并行。

### Primary Partition

Primary partition就是正常的一个LSM引擎，有完整的文件列表，可以服务所有读写请求。

Primary partition在加载时会从FileMeta Service中获得当前partition的完整文件列表，通过文件列表得知WAL的checkpoint。

数据进入primary partition后先写WAL，再写memory table，之后定期（如1分钟）flush到DFS上。primary partition会调用FileMeta Service的接口将新产生的文件添加进去，与此同时primary partition就可以使用这个文件来服务读请求了。如果添加失败的话primary partition会反复重试，直到成功。这个过程不会阻碍primary partition服务写请求和产生新文件，只是后续产生的新文件对read-only partition和compaction partition不可见。

如果在追加成功之前primary partition发生了failover，因为WAL的checkpoint并没有提升，数据也不会有丢失的风险。但如果长时间追加没有成功，此时primary partition可以考虑停写或主动failover，以避免影响数据可见性。

如果在flush过程中primary partition发生了failover，只会留下临时文件，可以考虑由primary partition自己回收这些临时文件，也可以由FileMeta Service异步回收。

每次追加成功后primary partition会向每个read-only partition和compaction partition发送一个notification。这里有两种实现方式：
- 将这两类partition视为listener，像Raft一样记录每个listener的lastIndex，保证不漏掉通知，如果通知失败会定期重试。优点是可以增量通知，不需要另两类partition经常去访问FileMeta Service，缺点是有一定复杂性。
- 单纯的通知，失败了不重试，依赖于另两类partition定期从FileMeta Service那里更新文件列表。优点是简单，缺点是增加了FileMeta Service的负担（但本来也不大），且数据可见延迟会受到影响。

primary partition还会接收到compaction partition的文件列表变化的通知。

Primary partition的读请求处理与正常的NoSQL系统相同，就不介绍了。

### Read-only Partition

Read-only partition不管理WAL，也不写数据，是一个单纯的面向一组不可变文件的查询引擎。

Read-only partition在加载时也会去获得完整文件列表。之后文件列表有两种更新方式：
- 接收到primary partition或compaction partition的通知时更新，根据实现方式不同可以直接从通知中获得增量内容，或是接着访问FileMeta Service。
- 定期（如5分钟）从FileMeta Service处拉取文件列表，可以增量也可以全量。

Read-only partition接收的读请求会带有一个timestamp，此时有几种可能：
- timestamp不大于当前文件列表的timestamp，可以直接服务。
- timestamp大于当前文件列表的timestamp，可以直接拒绝服务，也可以触发一次文件列表的拉取（但要控制好频率）。

文件列表更新失败不会影响read-only partition的服务，但如果长时间没有更新，read-only partition可以将自己标记为stale或主动failover。primary partition可能需要保证即使没有写入也要定期更新FileMeta Server中记录的文件列表的version，帮助read-only partition更好地确认自身状态。

Read-only partition是stateless的，可以任意伸缩或failover。

### Compaction Partition

Compaction partition会接到primary partition的通知，也会在每一轮compaction成功后调用FileMeta Service的接口变更文件列表，并通知另两类partition。

Compaction partition可以单线程或多线程做compaction，对于特别复杂的长时间的compaction，可以考虑交给一个单独的worker pool来做（类似F1 Query或ADB等系统），此时compaction partition只负责异步跟踪状态。

独立出compaction partition的好处是可以分摊热点分区的压力，也可以放心做非常复杂的编码、压缩、构建索引结构等操作，而不用担心影响读写服务。

Compaction在扫描文件时不需要将文件内容载入cache，但可以考虑将compaction partition与一个read-only partition放到同一台data server上，这样有机会借用read-only partition的cache来加速compaction。

Compaction过程中如果发生了failover只会留下一些临时文件，不会影响数据安全性。清理方式与flush相同。

Compaction策略和文件存储格式与此方案无关，就不介绍了。

### Split and Merge

这个架构下分区的分裂合并大部分只需要操作元数据，加上非常短的服务暂停（可以做成毫秒级）。分裂和合并过程都由master负责协调。

分裂过程：
- 计算分裂点：
    - 可以由外部进程从FileMeta Service那里获得文件列表后离线计算得到基于全量数据的分裂点。
    - 或由primary partition处得到基于增量数据的分裂点。
- Master产生若干个新的part_id，调用FileMeta Service的分裂接口。
- 触发primary partition flush，停写后再次触发flush。
- 再次调用FileMeta Service的分裂接口。
- Master持久化新partition的meta，之后卸载旧partition，调度新partition。

这个设计中旧partition的WAL不会对新partition可见，因此需要一次停写后flush来保证所有数据都持久化下去。如果我们在某个地方记录这种关系的话可以将WAL也设计为可链接的方式，从而不需要停写后flush，而是由新partition自己读旧partition的WAL回放数据，这样服务暂停时间更短。

进一步地，分裂产生的新partition可以直接调度到旧partition所在的data server，实现内存数据交接，这样甚至可以无缝分裂。

合并过程与分裂过程类似，主要考虑点在于要不要让新partition回放多个旧partition的WAL。

合并分裂操作在Master持久化新meta后就不可逆了。如果在此之前失败，则只会在FileMeta Service中留下垃圾，可以后续异步清理掉。

## 延伸讨论

### 分区策略

我们假设分区策略一定是基于key range的，那么可以让三种partition有不同的key range吗？

考虑两个primary partition分别服务`[A, B)`和`[B, C)`，read-only partition服务`[A, C)`，compaction partition可以服务以上任意key range。这样需要修改前面的两处设计：
- 通知机制。
- FileMeta Service的存储和文件的ref_count需要区分不同类型的partition。

这么做的好处是可以非常灵活地适应不同的workload：如果写压力大则分裂写；如果读压力大则分裂读；根据compaction策略可以让compaction partition随着其中一种partition变化。

理论上我们是可以实现三种partition按M:N:K的方式对应，但这样会使整个设计非常复杂，通知和文件列表都要考虑到所有有交集的情况。而如果保证三种partition是1:N或N:1（compaction取1或N都行），则整张表的结构是树型的，并不会很复杂，只要位于N的一方带着自己的key range去过滤文件列表就可以了。

### Read-only partition可以服务current读

如果要让read-only partition可以服务current读，实际上意味着要把它从listener变成follower，接收数据，参与投票，但不需要写WAL和flush。

数据进入follower partition后会同时维护在内存中的FIFO buffer和memory table中，每当它收到文件列表更改的通知时，将已经checkpoint过的数据从buffer中淘汰掉，再重新构建memory table。当leader挂掉发生选举时，新leader会先获取文件列表，将FIFO buffer转为memory table，再重放WAL。

当follower接收到current读请求或大于其当前timestamp的snapshot读请求时，它会向leader发送一个readCurrent请求，leader会将它缺少的数据返回回来。

这种设计需要引入部分Raft算法，不算太复杂，但也会对读写性能有一定影响。
