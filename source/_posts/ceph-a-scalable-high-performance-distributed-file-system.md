---
title:      "[笔记] Ceph: A Scalable, High-Performance Distributed File System"
date:       2021-03-28 13:03:20
tags:
    - 笔记
    - Storage
    - FileSystem
    - 开源系统
---

> 原文：[Ceph: A Scalable, High-Performance Distributed File System](https://www.usenix.org/events/osdi06/tech/full_papers/weil/weil_html)

这篇文章介绍了Ceph，一种分布式文件系统。它的最大特点是数据层与元数据分离所带来的高弹性。不像其它分布式文件系统（如GFS）master通过本地表记录数据分布，在Ceph中数据是由一种伪随机函数CRUSH来确定分布在哪个对象存储设备（OSD）上的。OSD本身是高度自治的单元，负责replication、错误检测、恢复。这种设计具备了非常高的弹性与灵活性。

（通篇看下来感觉Ceph是挺适合云上部署的，另外明显感觉到分布式系统、存储、数据库三个领域需要融合，至少在2006年）

<!--more-->

## System Overview

![](/images/2021-03/ceph-01.png)

Ceph系统有三个主要组件：
- client，负责提供类POSIX接口（相比POSIX功能上有一定扩展，且有更宽松的一致性语义，以更好地满足应用需求和提供更好的性能）。
- OSD集群，保存所有数据与元数据。
- MDS（Metadata Server）集群，管理namespace（文件名与目录）。

整套架构的关注点是扩展性、性能、可靠性。其中扩展性需要考虑存储量和吞吐在内的多维度。性能需要考虑单个client、单个目录、单个文件，包括很多极端场景，比如数十万个client同时读/写一个文件，或同时在一个目录下创建文件。

其它很多分布式文件系统的切入点是在file层以下，如block/chunk，整个文件实际由若干个block/chunk这样的小对象组成，但这种设计对元数据的管理有比较高的要求，即元数据层制约了整个系统的扩展性。而Ceph则是在文件层切入，MDS只负责管理元数据的操作，如open、rename等。每个文件条带化之后存储在多个OSD上，通过CRUSH来计算如何分布。这样其它工具也可以自己计算文件所在，不需要询问MDS。

另外Ceph还支持在不同MDS之间自动均衡目录树，称为Dynamic Subtree Partitioning，从而进一步缓解单个MDS的压力（但据说坑很多？）。

Ceph中数据迁移、replication、错误检测、恢复等功能都下放到了OSD集群中，OSD集群整体提供一个单一的逻辑对象视图。这种设计能充分利用OSD的资源，实现线性扩展。

（种种设计都在为MDS减负，说明POSIX接口或传统的单体文件系统的元数据管理压力实在太大，不适合原封不动地迁移到分布式架构中）。

## Client Operation

Ceph中client运行在各个宿主机的用户态，可以直接链接到程序中，也可以经FUSE挂载。每个client有自己的文件和buffer cache、独立的内核page。

当client需要open一个文件时，收到请求的MDS会遍历文件系统，将文件名转换为inode，同时可能会返回capability或security key等。Ceph中文件都是条带（stripe）化的，每个文件对应一组条带对象，对象名就是inode number + stripe number。通过CRUSH可以知道每个条带对象对应的OSD。Ceph中允许某个条带对象或字节范围不存在，即文件中可以有空洞。

当client需要读文件的时候，就拿着MDS返回的capability去对应的OSD上读条带对象；需要写文件的时候直接写OSD上的条带对象。在close的时候client需要交还capability，并将新的文件大小告知MDS（同一条带对象并发写的语义是什么？）。

POSIX语义要求：写后立即能读到；写操作是原子的（并发写的结果是按某种特定顺序的覆盖，听起来像是linearizability）。当MDS发现文件被多个client并发读写的时候，它就会收回所有client的buffer capability，强制client执行同步的读写操作。这样单个OSD的并发操作的顺序完全由OSD自己决定，极大简化了设计。当写跨多个对象时，client就需要显式在OSD上加锁了（owner挂了怎么办？）。

在科学计算中并发读写同一个文件是常见的场景，此时就需要放松一致性以获得更好的性能。Ceph提供了POSIX的扩展，比如`O_LAZY`表示宽松的一致性。另外应用可以自己管理一致性，如保证不同client写文件的不同区域，这样每个client都可以使用cache了。client还可以显式调用`lazyio_propagate`保证自己的写反映到文件上，`lazyio_synchronize`保证后续读反映最新的写内容（类似于`atomic_release`和`atomic_require`）。

client与MDS的所有namespace相关操作（`readdir`/`stat`/`unlink`/`chmod`等）都是同步的，不提供显式的加锁，以及callback（对HPC场景无用/开销大）。

为了降低这些同步namespace操作的延时，Ceph针对常见场景做了优化。比如`readdir`随后对每个文件调用`stat`（`ls -l`），在Ceph中`readdir`就会加载整个目录的inode，这样随后的`stat`就会直接返回刚刚加载的内容，牺牲了一致性（假设之间有更新），但保证了性能。这种行为也被纳入了`readdirplus`扩展。

Ceph不像其它一些系统那样（如早期的NFS）会长时间（如30秒）缓存元数据，这样对一致性的冲击太大。相反Ceph会选择扩展那些会损害性能的接口。如对并发写的文件的`stat`会使MDS收回所有写的capability，询问每个对象的长度，取最大值，再重新授予capability，保证获得正确的文件大小和修改时间。不需要这种保证的应用可以调用`statlite`。

## Dynamically Distributed Metadata

有[研究发现](https://static.usenix.org/event/usenix2000/general/full_papers/roselli.2/roselli.pdf)，文件系统的workload中元数据操作最多能占到一半，且都在关键路径上，因此MDS集群的性能至关重要（回答了前面的疑问）。同时元数据又不像数据那样容易线性扩展。

Ceph中文件和目录的元数据都很小，几乎只包含目录项（文件名）和inode（80B）。与传统文件系统不同的是，Ceph中不需要记录block分配表（编码到条带对象名字中，使用CRUSH定位）。这些设计都能缓解MDS的压力，允许它管理海量的文件和目录（大部分缓存在内存中）。

每个MDS有自己的journal，以streaming的方式存储到OSD上。这些journal后续会合并到MDS的长期存储中（也由OSD负责）。长期存储针对读优化，如inode直接嵌入目录中，这样一次读操作就可以把整个目录的inode拿到。每个目录的内容会分布到相同的OSD上。每个MDS都有自己的inode范围，另外有一个全局的anchor table保存对应多个hardlink的inode——尽量优化单个link的场景。

Ceph的一个创新是MDS之间基于目录树的负载均衡，称为Dynamic Subtree Partitioning。

![](/images/2021-03/ceph-02.png)

MDS会统计每个inode对应的操作数并以指数时间衰减，一个操作会增加从root到对应inode的整个链路的计数。这样MDS之间就可以方便地迁移目录树了。长期存储是共享的，不需要迁移。迁移过程中新MDS会把数据都写进journal，之后新增修改会写进新旧两个MDS的journal，直到迁移完成（移动内存数据）。

迁移过程中inode的内容会分为三组：
- security（owner/mode）
- file（size/mtime）
- 不可变（inode number/ctime/layout）

前两组在迁移过程中可能会变，需要加锁。但security基本不变，相反file则很可能有修改，因此需要不同的加锁策略。

针对特别热的目录，Ceph可以有特殊处理：
- 读特别热的目录可以分散到多个节点上。
- 写特别热的目录可以按filename hash后分散到多个节点上。

这些内容都可以由MDS返回给client。

## Distributed Object Storage

从client和MDS的角度看Ceph的整个OSD集群是一个整体，所有replication、数据迁移、容错恢复等机制都下放到了这套名为Reliable Atuonomic Distributed Object Store（RADOS）的数据存储层（Ceph挺能发明词的……）。

![](/images/2021-03/ceph-03.png)

Ceph首先将对象根据hash分成若干个placement group（PG），随后再通过CRUSH将PG分配给OSD，PG与OSD的数量比例大概是100:1。CRUSH只需要知道pg和OSD map。这种设计的好处是：
- client和工具可以自己计算PG所在的OSD，不需要通过MDS。
- 保证了整个集群存储负载的均衡。
- OSD map更新频率非常低，每次更新只会影响到非常少量的PG（类比一致性hash）。

CRUSH在执行时会根据placement rule，比如一个PG要对应3个OSD等。OSD map中每个OSD也会有权重、位置、错误状态等。OSD map中有个epoch number，每次更新时增加。

RADOS使用了primary-copy的一个变种来做replication。replication的单位是PG。client在写PG时会先将数据发给第一个正常工作的OSD，OSD会为被写的对象和PG分配新version，随后转发写请求给其它参与replication的OSD。当所有replica都执行完修改回复primary后，primary再执行修改并回复client。读请求则均由primary处理（可以用Paxos/Raft）。

值得注意的是，上面所说的执行修改只发生在内存，后续client可以单独查询这些修改是否提交成功，如下图。RADOS分离了同步与safety，目的是同时服务需要低延时和高安全性的应用。client因此在收到请求成功之后，仍然要本地缓存写过的数据，直到知道提交成功。

![](/images/2021-03/ceph-04.png)

RADOS中每个OSD会监控与它共享PG的其它OSD，相互会ping那些长时间（因没有请求而）没有响应的远端OSD。短时间无响应的OSD会被标记为down，它的primary也会让给其它OSD。长时间标记为down的OSD会被标记为out，其它OSD会接管它的PG。

另有一个小集群负责收集这些错误信息并过滤掉暂时的异常，以及管理OSD map，因此需要高可用。OSD map的变化会先发给受影响的OSD，随后在相互间的响应中传播开（类似于[Dynamo](/2020/11/11/dynamo-amazons-highly-available-key-value-store)的gossip协议和[Cassandra](/2020/09/23/cassandra-a-decentralized-structured-storage-system)的ϕ，但感觉机制有点简单）。

（恢复机制略过，感觉RADOS不如直接换成Dynamo等去中心化的kv存储，或者PG之间直接用Paxos/Raft协议管理）

OSD的本地文件系统称为Extent and B-tree based Object File System（EBOFS，又一个新词），不用ext3的原因是后者不适合对象的workload。EBOFS是纯用户态的文件系统，直接操纵裸的块设备，支持同步与提交的分离、多个对象的原子写等特性。

（Ceph是否可以用中心化的对象存储替换RADOS？似乎也没有很明显的障碍，毕竟这些系统的控制平面也往往与关键路径解耦了）