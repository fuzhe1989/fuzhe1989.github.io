---
title:      "[笔记] F1 Lightning: HTAP as a Service"
date:       2020-11-30 12:38:12
tags:
    - 笔记
    - OLAP
    - HTAP
    - Google
    - Columnar
    - LSM
---

> 原文：[F1 Lightning: HTAP as a Service](https://dl.acm.org/doi/abs/10.14778/3415478.3415553)

## TL;DR

F1 Lightning是一种提供HTAP服务的系统，本身并不是一个完整的HTAP系统。它的切入点是为已有的OLTP（F1、Spanner）用户提供列式存储，供其它的OLAP系统（[F1 Query](/2020/11/21/f1-query-declarative-querying-at-scale)）使用。这样用户不需要迁移转换已有的数据就能享受到更快捷的分析能力。

F1 Lightning依赖于OLTP系统的ChangeDataCapture（CDC）接口，从而将OLTP的修改应用到Lightning列存上；另一端，它依赖于OLAP系统下推部分算子，从而充分利用列式存储的扫描优势。

HTAP现在已经不那么新颖了，但F1 Lightning的切入点很独特，也是因为Google内部系统间合作比较容易，且已有数据量非常大，这种不需要迁移，与已有系统充分结合的方案才有用武之地。

<!--more-->

## 设计目标

近年来很多新的系统可以同时服务TP和AP两种场景，称为HTAP。但作者认为Google需要一种并非全新的、能与已有系统结合的HTAP方案。在Google中有多种OLTP系统服务于不同的场景，有大量历史数据，且OLAP系统（如F1 Query）可以在不同系统上实现federated query，只是效率不高。因此F1 Lightning的目标是在已有的OLTP和OLAP系统间增加HTAP能力。

用户只要标记它们的表为“Lightning table”，F1 Lightning就会自动将这些表从行存转换为列存供F1 Query使用，用户甚至不需要修改SQL，所有工作都由F1 Query和F1 Lightning来完成。

## 相关工作

在[Hybrid transactional/analytical processing: A survey](https://dl.acm.org/doi/abs/10.1145/3035918.3054784)中将HTAP方案分为了“单个系统服务OLTP和OLAP”和“不同系统分别服务OLTP和OLAP”两种。其中混合使用行存和列存的系统的性能要比写入和分析都用相同数据组织方式的系统更好。F1 Lightning就属于这种。

接下来，“不同系统服务OLTP和OLAP”又分为“共享存储”和“TP与AP使用不同存储”两种。前者通常需要修改OLTP系统，也因此通常是基于OLTP系统演化的HTAP系统会采用这一方案。F1 Lightning则是第二种，假设OLTP系统不能被修改。

最后是写入方式。一些系统使用单独的ETL流程来离线导入数据，但这样数据延迟会很大。F1 Lightning使用OLTP系统提供的CDC接口增量修改数据，以及结合了磁盘和内存的LSM结构，保证了数据新鲜度。

## Overview

F1 Lightning整个系统分为三部分：OLTP系统作为source of truth，并暴露CDC接口；F1 Query作为分析引擎；Lightning来维护用于分析的replica。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/f1-lightning-01.jpg)

通常OLTP系统都是面向写入和点查询优化的，采用行式存储，不适用于分析。因此一些团队会自己写pipeline定期将F1数据转换为列存文件（如ColumnIO）再由F1 Query分析。这种方案的问题：
- 整个开发流程散布在不同系统中，且有大量冗余存储（可能一个团队自己存一份）。
- ColumnIO不支持原地修改，而重新导出的开销比较大，因此pipeline通常运行间隔比较久，导致数据修改的可见延迟非常大。
- 用户要显式修改query去读导出表，当有schema变化时要手动维护F1和导出表两边的一致性。
- F1和导出表的访问权限也要同步维护。

这些都是F1 Lightning要解决的问题。

Changepump可以将打开了Lightning的表的全量和增量数据导出到Lightning server。Lightning server维护一种LSM结构，将这些全量增量数据转换为列式存储。它同时还会异步更新二级索引和一些materialized view。这些列式数据会向F1 Query提供snapshot读。F1 Query会判断Lightning中的数据版本是否满足用户需求，如果满足的话就来读Lightning的数据，而不是原始的TP数据。这种判断是表级别的，因此一个跨表的query可能部分数据来自TP系统，部分来自Lightning。

Lightning提供了以下能力：
- 提高了分析的效率，降低了延时。
- 配置简单，不需要冗余的导出表。
- 透明的用户体验。
- 数据的一致性和新鲜性。
- 安全性：Lightning中的数据权限与原始TP数据相同。
- 关注点分离：Lightning和F1或Spanner不是一个团队，因此可以专注于提升分析性能。
- 扩展性：Lightning可以很容易地接入新的OLTP系统。

## Architecture

### Read semantics

在Lightning中数据会使用与源系统中相同的时间戳，从而保证了分析结果不变。但Lightning中不会永久保留所有版本的数据，而是会有一个queryable window，min ts以下的数据可能已经被合并进base了，max ts以上的数据还不可读。这个窗口通常是10小时。

### Tables and deltas

Lightning中table会按key range分为若干个partition，每个partition组织为一棵LSM树，其中每个文件称为一个delta。

delta中数据有INSERT、UPDATE、DELETE三种。Lightning假设每个delta中相同key可能有多个版本，不同delta中可能存在相同key，但同一delta中数据可以由`<key, timestamp>`唯一确定。其中key递增，timestamp递减排列。

一开始Lightning server会运行一个离线任务导入对应表的全量数据。

### Memory-resident deltas

数据进入Lightning server后会先写入一个内存中的行格式的B树。每棵B树最多有两个writer——每个partition有一个在线线程和淘汰数据的背景线程——和多个reader。修改B树时Lightning会使用copy-on-write。

Lightning server没有WAL，数据写入内存B树后还需要再flush到磁盘中才保证持久性。这次flush会定期进行，称为checkpointing。为了保证速度，checkpointing不会修改数据格式，即磁盘中的delta与内存B树格式相同。这次产生的checkpoint不可读，需要再次载入内存。

当磁盘delta比较多或内存压力大时，Lightning会启动compaction将这些行存的delta转为列存数据。

### Disk-resident deltas

Lightning server支持多种列存文件格式，只要满足一个公共接口即可。每个文件有两部分，一个PAX风格的行列混合的数据部分，以及一个用于定位row group的B树格式的index部分。index部分通常是缓存在内存中的。

### Delta merging

这里merging指读的时候将列存delta与行存delta合并组成完整数据。

delta merging包含两种操作：merging和collapsing。merging将同一行的不同修改合并到一起，还可能会执行schema变更。collapsing将相同key的不同版本合并为一个版本。

整个过程采用向量化执行。首先Lightning会列举出可能的delta，然后进行多路归并。归并时每个source会一次出一个block的数据，每轮先确定哪个范围的数据可以进行collapsing。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/f1-lightning-02.jpg)

上面这个例子中，当前轮Lightning只能collapse小于K2的数据（因为K2还没有到头）。K2对应的数据要等到下轮处理。

### Schema management

Lightning中数据有两种schema，一种是逻辑schema，来自OLTP系统中的schema，支持PB和GoogleSQL结构体等复杂类型。对于每种逻辑schema，Lightning还会生成一种或多种物理schema，只支持基本类型。Lightning中的文件接口只按物理schema进行操作。两种schema的转换发生在数据写入文件和读出文件时。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/f1-lightning-03.jpg)

Lightning可以使用不同的物理schema来保存相同逻辑schema的数据，这样可以试验新schema，或者同时保存多种物理schema以平衡不同需求下的开销，或者磁盘与内存中使用不同的物理schema。

两种schema分离也可以在数据schema变化时只修改元数据，生成新的映射方式，并在读取时完成schema变化。

但也有些schema变化是没办法lazy完成的，如新标记的Lightning表需要一个特殊的初始化阶段。

当schema映射关系变化太多时，为了减少数据转换的开销，Lightning也会在compaction时将数据转换为新schema。

### Delta compaction

Lightning中支持四种compaciton，active compaction、minor compaction、major compaction、base compaction：
- active compaction将内存delta持久化到磁盘上。
- minor compaction处理小的、新的磁盘delta。
- major compaction处理大的、旧的磁盘delta。
- base compaction会选择一个早于min timestamp的ts，将所有它之前的数据合并为一个新的snapshot。

其中minor和major的区分是基于size compaction（类似于RocksDB的Universal Compaction）。

除了active compaction是由Lightning server完成外，其余三种compaction都是由单独的task worker完成的，Lightning server负责监听状态变化，并在完成后载入新的delta。

### Change replication

Lightning中的Changepump模块抽象了不同OLTP系统的CDC接口，它的作用：
- 隐藏了不同OLTP系统的细节。
- 将面向事务的log流转为了面向partition的log流。同一个事务可能对应多个partition的修改，此时不同Lightning partition是独立处理的。
- 负责维护事务一致性。它会追踪各个partition最近应用的timestamp，再分别提升它的max safe timestamp。

每个Lightning partition会单独维护一个到Changepump的订阅。每个订阅有个start timestamp。Changepump订阅会返回两类数据：change update和checkpoint timestamp update。前者是数据本身的修改，相同key的修改保证版本号递增，但不同key的修改的版本号可能交错。后者是用于通知各个partition进行checkpoint（从而整体把max safe ts往前推进）。checkpoint是有开销的，因此不能每次数据修改都做，需要在数据新鲜度与开销之间权衡。

Lightning有lazy和eager两种方式来检测OLTP系统是否发生schema变化。lazy发生在每次数据进入Changepump时，如果Lightning之前没见过这次数据的schema，就会暂停这个partition的处理，直到加载完新的schema。lazy的问题是它发生在导入路径上，会增加延时。eager是用背景线程定期去OLTP系统中查询是否有schema变化。

Changepump本身也是一个sharding的service，因此一个订阅可能实际上要与多个Changepump server连接，由Changepump的client将来自多个server的数据合并为一个log流。Changepump的sharding与Lightning server的sharding不同，前者是按增量数据量均分，后者是按全量数据量均分。

Changepump本身对增量数据有内存cache，这样Lightning一个partition的不同replica可以共享这些数据，另外也能加快Lightning partition的failover。

Lightning本身是将OLTP中的派生表——二级索引和materialized view——与正常表同等看待的，但OLTP系统通常不会为这些派生数据提供CDC，因此Lightning需要自己算派生数据的修改。Lightning partition负责生成对应的派生数据修改，再写进BigTable中。引入BigTable是为了解决主表与派生表的key order不同。

Index server会根据BigTable的改动修改index partition，也负责生成index partition的checkpoint。

目前Lightning只能处理有限的几种materialized view，比如简单的聚合。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/f1-lightning-04.jpg)

Lightning支持动态重分区，基本上只需要修改元数据，不需要搬数据，因此不影响在线请求。

在分裂一个partition时，Lightning先将新的partition都注册为inactive，此时这些新partition会共享老partition的所有delta。之后新partition会开始从Changepump的订阅处获得数据并应用为新的delta。直到新partition追上最新数据后，它才会被标记为active。之后老partition会被标记为inactive，等到服务完所有已有请求后再被清理掉。

这些新partition在读共享的delta时需要应用一个row filter来过滤掉不在自己范围内的行。后续的compaction中新partition会逐步将数据搬到非共享的新delta中。

Lightning支持M个partition合并为N个partition（应该是需要连续的吧？），此时N个新partition会共享M个老partition的delta。其它过程与分裂相同（整个过程类似于Tablestore的tunnel分裂合并）。

### Fault tolerance

在一个datacenter内，Lightning会为每个partition设置多个replica，这些replica独立维护内存delta，但共享磁盘delta。每个partition只有一个replica有权力执行compaction来修改磁盘delta，在compaction完成后它会通知其它replica。这样所有replica可服务的数据是相同的，因此query可以通过load balancer发往任意replica。

当server升级时，Lightning在调度层面上会控制同一时间只有最多一个replica重启，且重启时它会尝试向其它replica处获得数据，而不是再读一次Changepump，好处开销更低。

Lightning也可以在多个datacenter都服务相同的表，这些实例相互数据是完全独立的，不会有同步开销。所有这些实例都会共享相同的元数据DB（我们可以假设元数据DB永远可用），有着相同的table schema和partitioning，这样一个datacenter挂掉了的话，请求可以直接转给其它datacenter的相同partition处理。

Lightning可以部署在与OLTP系统不同的datacenter，除了容错方面的考虑，这种部署方式还能提供更好的数据本地性（离client更近）以及提升服务能力。

Changepump server本身是无状态的（元数据可能在元数据DB中），因此如果一个Changepump server挂了，client可以连接另一个server继续服务。

如果OLTP系统本身有问题，因为Google的OLTP系统通常也是跨datacenter部署的，Changepump能自动连接另一个datacenter的OLTP系统。

Lightning master也会定期检查各个datacenter的partition的数据新鲜度，如果有某个datacenter落后程度超过阈值，Lightning master会重启这个datacenter的partition，从其它好的datacenter搬数据过来。

如果某张table本身有问题，Lightning可以将这张表加入黑名单。通常这样的表就不能从Lightning端读了，因为数据不再更新，但有些用户仍然愿意读不新鲜但分析更快的Lightning表，这里就需要进行权衡了。

## F1 Query Integration

Lightning的OLAP端目前与F1 Query耦合得比较紧，共同实现了两个特性：无感知重写query plan，以及下推subplan。

F1 Query通常可以为read-only query选择一个近期的timestamp，此时F1 Query会直接取Lightning的max safe timestamp。如果用户指定了timestamp且位于Lightning的queryable window内，这次请求仍然可以使用Lightning。

F1 Query总会按读OLTP的方式生成一份逻辑plan，这样简化了逻辑，也能保证无论读OLTP还是Lightning语义都是相同的。如果选择了Lightning，F1 Query会为其生成物理plan，同时使用一些额外的rule，如subplan下推。

F1 Query目前可以向量化列式处理query（类似于Procella），将plan中的某个子树下推到Lightning中。Lightning重用了F1的列式内存格式，因此F1 Query不需要再转换一次Lightning传回的数据。
