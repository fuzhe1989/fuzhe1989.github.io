---
title: "[笔记] Apache Pulsar Versus Apache Kafka"
date: 2020-07-30 23:06:42
tags:
    - Pulsar
    - Kafka
    - Queue
---

只有33页的新书，可以从[MemSQL](https://www.memsql.com/resources/ebook-oreilly-pulsar-vs-kafka)上下载到。

<!--more-->

## Architecture

Kafka broker既负责处理请求，也负责存储数据，因此：
- 它是有状态的，对新node接管有要求：要有一份replica。
- 移动topic需要复制数据，因此rebalance也很复杂。
- 请求处理的压力与存储空间的压力耦合在一块，没办法灵活扩容。
- 扩容时容易出现配置不一致与容量异构，导致管理成本高。

Pulsar是计算存储分离的架构，存储层交给Apache BookKeeper，因此：
- broker是无状态的，迁移不需要复制数据，rebalance也非常容易（有内置的load balancer）。
- 基于BookKeeper，实现了分布式存储，数据更安全。
- 计算与存储节点的扩容是独立的。

### Replication Model

Kafka是leader-follwer模型，因此：
- 不发生failover和新增topic的话节点的数据分布是静态的，新节点加入后没办法立即分担压力。

Pulsar是quorum-vote模型，因此：
- 读写性能更好（压力更平均）。
- 新节点加入后立即可以分担压力。
- failover处理更简单（转交给BookKeeper处理）。

## Pub-Sub Messaging

Kafka与Pulsar有着非常类似的Log Abstraction，它们在Pub-Sub方面的区别在于对Traditional Messaging的处理方式。

### Queues and Competing Consumers

Kafka中一个topic可以有多个partition，message按round-robin或按key分派给某个partition。但一个partition只能对应一个consumer，多出来的consumer只能闲置。这样应用方需要提前确定好要多少个partition。另外，向consumer group新增consumer还会触发rebalance这个topic对应的所有consumer（为什么？），期间整个message投递都会暂停。

与传统的MessageQueue不同，Kafka不会重复向consumer投递未确认的message，需要应用方自己重试。

Pulsar中与consumer group类似的概念叫subscription。shared subscription就是competing consumers，但不需要有partition与consumer的对应关系。新增consumer也不会触发rebalance。Pulsar中有partition，但控制message消费的维度是subscription，而不是partition。

Pulsar的shared subscription支持：
- 定期向consumer发送未确认的message。
- 只确认单条message（对应确认某个offset之前的所有消息）。
- 恢复单条message到未确认状态（允许这条message被其它人消费）。

Pulsar还支持其它subscription模式：
- exclusive：对应topic只允许一个consumer。
- failover：允许一个活跃的consumer和多个不活跃的consumer。
- key_shared：对应key只允许一个consumer，这样能保序，且不需要引入partition。

结论：
- Kafka可以用来实现work queue，但需要考虑的东西比较多。因此很多人另外使用传统的MQ来配合Kafka使用，这样增加了管理负担。
- Pulsar既支持Kafka的功能，也支持MQ的功能，不需要管理两个系统。

## Log Abstraction

Kafka中，每个topic是一个log流（应该是partition维度吧？），topic不能分裂成多个给多个broker，或使用同一broker的多块盘，因此无法无限增大，需要扩容时只能增加盘的大小，在用本地盘时是有上限的。在迁移topic时需要做大量的数据复制，而且热迁移的运维也非常复杂。

Pulsar中每个topic也是一个log流，但Pulsar把它分成了若干个segment，每个segment独立写入BookKeeper，这样整个log流的大小只受限于集群大小，在broker挂掉或新增时也不需要实际移动数据。另外不同segment分布在不同机器上，也加快了topic的恢复速度。

使用BookKeeper也增加了Pulsar的数据安全性：数据每写入BookKeeper成功时就确保落盘了，而Kafka是定期flush，有failover时很难避免丢数据。如果Kafka配置成每笔写都flush，性能就会受到很大影响。

得益于存储计算分离，Pulsar很容易就支持了分层存储，冷数据可以存到其它云存储介质中，进一步降低成本。另外应用可以因此做长时间的溯源，使用events来恢复最终状态。

## Partitions

Kafka中每个topic都分为一个或多个partition，partition数量就是这个topic的消费并行度。

但随着软硬件性能的提升，现在一个topic可能只需要一个partition就可以满足性能需求了。但在Kafka中一个partition就只能对应一个consumer。

Kafka中可以增加partition（如果是按key划分的partition，会导致相同key的message在增加前后落到不同的partition上，影响保序性），但不可以减少partition。

Pulsar中partition是可选的，可以不用partition而支持多个consumer。使用partition是为了增加性能，或需要按key保序消费message。producer可以向topic投递，也可以直接向某个partition投递。consumer同理。与Kafka相同，Pulsar中partition也是只能增加不能减少。

结论：Pulsar中可以像Kafka那样使用partition，但不用一上来就必须了解partition，这样显著降低了学习成本。

## Performance

[GigaOm的一份报告](https://oreil.ly/vGoPy)显示：
- Pulsar的吞吐是Kafka的150%。
- Pulsar的延时比Kafka低40%，且一致性等级更高。
- 得益于更好的伸缩性，Pulsar在不同的message大小和partition数量下有着一致的结果。

[另一份基于OpenMessage的报告](https://oreil.ly/34h_v)有类似的结论。

## Tenancy

Pulsar有namespace和ACL，支持多租户。Kafka不支持多租户，不同团队使用同一个Kafka集群时需要协调好。

## Geo-Replication

Geo-replication是Pulsar一开始设计时就考虑到的功能。Pulsar中可以按namespace打开或关闭geo-replication，可以配置哪些topic可以或不可以replicate。producer还可以指定跳过哪些data center。

Pulsar支持多种拓扑：active-standby、active-active、full-mesh、edge aggregation。

Kafka中可以用MirrorMaker来做geo-replication，它是将一个data center的consumer和另一个data center的producer链接起来，不能动态配置，也不支持在两个data center间同步配置或subscription。

另一个工具是Uber的uReplicator，它比MirrorMaker更好用，但本身是一个单独集群，有管理成本。

Kafka也有商用方案，如Confluent Replicator。

结论：Pulsar内置对geo-replication的良好支持，Kafka需要使用外部工具，且运维负担重。

## Ecosystem

Kafka本身是ASF控制的，但它的很多第三方工具由大公司控制，未来有开源协议变动的风险。

Pulsar本身和主要工具都是由ASF控制的，未来协议变动的风险非常低。

## Summary

两个系统的对比：

|Dimension |Kafka |Pulsar|
|----------|------|------|
|Architectural components |ZooKeeper, Kafka broker |ZooKeeper, Pulsar broker, BookKeeper|
|Replication model |Leader–follower |Quorum-vote|
|High-performance pub–sub messaging |Supported |Supported|
|Message replay |Supported |Supported|
|Competing consumers |Supported with limitations |Supported|
|Traditional consuming patterns |Not supported |Supported|
|Log abstraction |Single node |Distributed|
|Tiered storage |Not supported |Supported|
|Partitions |Required |Optional|
|Performance |High |Higher|
|Geo-replication |Available through tool or external projects |Built-in|
|Community and related projects |Large and mature |Small and growing|
|Open source |Mixture of ASF and others |All ASF|

（附另一组对比：[Kafka vs. Pulsar vs. RabbitMQ: Performance, Architecture, and Features Compared](https://www.confluent.io/kafka-vs-pulsar/)）
