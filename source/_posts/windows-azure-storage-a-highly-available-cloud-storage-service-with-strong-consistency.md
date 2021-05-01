---
title:      "[笔记] Windows Azure Storage: A Highly Available Cloud Storage Service With Strong Consistency"
date:       2021-04-28 22:48:13
tags:
    - 笔记
    - Storage
---

> 原文：[Windows Azure Storage: A Highly Available Cloud Storage Service With Strong Consistency](https://dl.acm.org/doi/abs/10.1145/2043556.2043571)
>
> 年代：2011

Windows Azure Storage（WAS）是Azure的存储层，它的特色在于基于同一套架构提供了对象存储（Blob）、结构化存储（Table）和队列服务（Queue）。

这三种服务共享了一个提供了强一致、global namespace、多地容灾、支持多租户的存储层。

> 个人猜测这种设计（同时推出三种服务）更多出于组织架构的考虑，而不是技术层面上有非这么做的理由。
> 
> 以及这篇文章作者中有多名后来加入阿里云存储的大佬，所以可以从这里面看到一些后来阿里云存储产品发展的影子。

<!--more-->

三种服务都使用了同一套global namespace，格式为：

```
http(s)://AccountName.<service>.core.windows.net/PartitionName/ObjectName
```

其中：
- AccountName会作为DNS解析的一部分，来确定这个account对应的primary data center（如果需要跨地域就需要使用多个account）。
- PartitionName是跨对象事务的边界。
- ObjectName是可选的，Blob就没有。

Table的每行的primary key由PartitionName和ObjectName组成。Queue的PartitionName用来标识队列，而ObjectName则用来标识消息。

WAS的底座是Windows Azure Fabric Controller，负责分配和管理资源，WAS会从它那里获取网络拓扑、集群物理布局、存储节点的硬件信息等。

一套WAS环境由若干套storage stamp和一套location service组成：

![](/images/2021-05/was-01.png)

每套storage stamp是一个小集群，典型的大小是10-20个rack，每个rack有18个节点。第一代storage stamp容量是2PB，下一代会提升到30PB。storage stamp的目标是达到70%的使用率，包括了容量、计算、带宽。

location service负责管理account到storage stamp的映射与迁移（一个account只能有一个primary storage stamp）。

storage stamp内部分为三层：
- stream层提供类似于GFS的能力。数据组织为extent（类比chunk），extent再组织为stream（类比file）。这一层是三种服务共享的。
- partition层提供三种服务特有的能力。它负责提供应用层抽象、namespace、事务与强一致的能力、数据的组织、cache。partition和stream的server是部署在一起的，这样最小化通信成本。
- front-end（FE）负责处理请求。它会缓存partition map从而快速转发请求。另外它还可以直接访问和缓存stream层的数据。

storage stamp提供了两种replication：
- stamp内部，在stream层提供同步的replication，提供数据的强一致性。
- stamp之间，在partition层提供异步的replication，提供数据的多地容灾能力。

区分两种replication还有一个好处是stream层不需要感知global namespace，只需要维护stamp内的meta，这样metadata就不会那么多，更容易全部缓存到内存中。

在stream层数据组织为三层：
- block是读写的最小单元，但不同block可以有不同大小。每个block有自己的checksum，每次读都会校验。另外所有block定期还会被后台校验checksum。
- extent是stream层replication的单元，它由一系列block组成。每个extent最终会长到1GB大小，partition层可以控制将大量小对象存储到一个extent，甚至一个block中。
- stream类似于一个大文件，但它不拥有extent，只是保存了若干个有序的extent引用。将已有的extent组织起来就可以得到新的stream。

![](/images/2021-05/was-02.png)

注意stream层是append-only的，每个stream只有最后一个extent可以被写入，其它extent都是不可变的（immutable，被seal了）。（猜测未seal的extent不可被多个stream共享）

stamp内部有两类组件：stream manager（SM）和extent node（EN），前者是master，后者是data node。

![](/images/2021-05/was-03.png)

（大概讲SM怎么与EN通信来管理extent，类似于GFS，略过）

extent支持原子append多个block，但因为重试原因可能数据会被写入多次，client要有能力处理这种情况。

client会控制extent的大小，如果超过阈值则发送seal指令。被seal的extent不可再被写入，stream层会对sealed extent做一些优化，如使用erasure coding等。

stream层提供的强一致保证：
- 一旦写入成功的消息告知了client，后续所有replica上这次写入的数据都可见（read committed）。
- 一旦extent被seal了，后续所有已经seal的replica的读保证看到相同内容（immutable）。

每个extent有一个primary EN和若干个secondary EN，未被seal的extent的EN是不会变的，因此它们之间不需要有lease等同步协议。extent的写入只能由primary EN处理，但可以读取任意secondary EN（即使未seal）。primary会将所有写入排好序，确定每笔写入的offset，再发给所有secondary EN。所有replica都写成功了之后primary才会告知client。

client在写入过程中会本地缓存extent的meta，不需要与SM通信（直到需要分配新extent）。如果某次写入失败了，client可以要求SM来seal这个extent，然后立即开始写新extent，而不需要关心旧extent末尾是否有数据不一致。

seal过程中SM会与每个replica EN通信，并使用可用的EN中**最小的**commit length。这样能保证所有告知过client的数据都不会丢，但有可能会有数据还没来得及告知client。这是client需要自己处理的一种情况。（是不是可以让client缓存一个commit length，seal时告知SM）

client在读多副本的extent时可以设置一个deadline，这样一旦当前EN无法在deadline之前读到数据，client还有机会读另一个EN。而在读erasure coded数据时，client也可以设置deadline，超过deadline后向所有fragment发送读请求，并使用最先返回的N个fragment重新计算缺少的数据。

