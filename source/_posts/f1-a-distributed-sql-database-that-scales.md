> 原文：[F1: A distributed SQL database that scales](https://research.google/pubs/pub41344/)

## TL;DR

F1是Google用于替换分库分表的MySQL的RDBMS系统。它在Spanner之上建立了一套关系模型，拥有Spanner的跨datacenter的同步replication能力。F1还开发了新的ORM模型，从而将同步geo-replication导致的延时隐藏了起来，保证了E2E延时在替换后没有上升。

<!--more-->

## Introduction

F1的设计目标：
- 可伸缩性，要能把扩容变得平凡、业务无感知。
- 可用性，无论是datacenter中断，还是正常维护、schema变更，都不能停止服务。
- 一致性，必须提供ACID的事务，保证数据不出错。
- 易用性，要满足用户对于一个RDBMS的预期，要支持完整的SQL查询、二级索引、ad-hoc查询。

F1建立在Spanner之上，在继承Spanner原有功能（极致规模、同步replication、强一致、有序）之外，F1还增加了以下特性：
- 分布式SQL查询，包括与外部数据源做join。
- 事务一致的二级索引。
- 异步schema变更，包括db重新组织（TODO）。
- 乐观事务。
- 自动记录和发布表变更历史。

F1的设计选择导致了常规读写延时上升，因此F1使用了以下技术来隐藏掉延时，从而提供了与之前MySQL方案差不多的E2E延时：
- 使用分级的表结构和嵌套数据类型，显式将数据聚焦起来，提高了数据局部性，减少了RPC数量。
- 重度使用batch、并行、异步读，并通过新的ORM来体现这些特点。

## Basic Architecture
