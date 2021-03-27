---
title:      "[笔记] Ceph: A Scalable, High-Performance Distributed File System"
date:       2021-03-27 19:20:23
tags:
    - 笔记
    - BigData
---

> 原文：[Ceph: A Scalable, High-Performance Distributed File System](https://www.usenix.org/events/osdi06/tech/full_papers/weil/weil_html)

这篇文章介绍了Ceph，一种分布式文件系统。它的最大特点是数据层与元数据分离所带来的高弹性。不像其它分布式文件系统（如GFS）master通过本地表记录数据分布，在Ceph中数据是由一种伪随机函数CRUSH来确定分布在哪个对象存储设备（OSD）上的。OSD本身是高度自治的单元，负责replication、错误检测、恢复。这种设计具备了非常高的弹性与灵活性。

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

当client需要读文件的时候，就拿着MDS返回的capability去对应的OSD上读条带对象；需要写文件的时候直接写存在的条带对象或
