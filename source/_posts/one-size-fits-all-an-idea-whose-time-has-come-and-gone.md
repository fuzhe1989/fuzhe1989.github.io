---
title:      "[笔记] ‘One Size Fits All’: An Idea Whose Time Has Come and Gone"
date:       2020-08-06 16:14:43
tags:
    - 笔记
    - Database
---

> 原文：[Michael Stonebraker and Uğur Çetintemel: “‘One Size Fits All’: An Idea Whose Time Has Come and Gone,” at April 2005.](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.68.9136&rep=rep1&type=pdf)

## TL;DR

本文idea：DBMS的未来趋势是多个相互独立的DB engine针对不同的细分领域。

论证过程：
1. 过去若干年中DBMS厂家追求“One size fits all”，原因有若干。
1. 传统DBMS主要针对OLTP，处理模型为outbound，针对性优化短事务，重点在于ACID，这些特点并不适用于OLAP。
1. 随着streaming processing兴起，它需要完全不同的优化方向。
1. 未来使用DBMS的应用类型会越来越多，会有更多针对特定领域的DB engine出现。

感想：
1. 任何优化都是有代价的，选择针对一个方向优化，就会在另一个方向平庸。甚至提供不同优化的开关也是有代价的，只要允许选择，代价就会存在。
1. 不同应用需要不同程度的一致性，但一套系统在架构设计时就要锚定某种一致性，它运行在其它一致性级别时只是稍有优化，而不会达到很理想的水平。
1. 如何控制DB与应用逻辑的耦合度，过高的耦合度会带来安全性、维护性上的问题，而过低的耦合度又会带来性能开销。存储过程，以及各种各样的UDF就是其中的粘合剂。

<!--more-->

## Abstract

过去25年的DBMS发展可以总结为“One size fits all”：用一套DBMS架构支持不同需求和特点的数据应用。

但这句话已经过时了，未来会有一组独立的db engine和统一的前端解析层。

后面主要讨论流计算和数据仓库两个方向。

## Introduction

一开始的RDBMS主要针对“buniness data processing”，特点是：
- OLTP
- 按行存储
- 索引使用B-tree
- 使用基于开销（cost-based）的优化器
- 提供ACID

1980年代的主流DBMS开始陷入“One size fits all”，想用一个系统提供所有DB服务，因为多个系统会导致以下问题：
- 维护开销
- 兼容性，应用需要适配的系统变多了
- 销售问题，销售人员不知道该卖哪套产品给哪类客户
- 市场问题，多个系统需要精准定位

本文观点是这种思想已经过时了。

## Data warehousing

1990年代，企业需要将多个不同DB的数据导入一个data warehouse作分析。这些系统都是在线系统，直接在上面跑分析会拉长对其它用户的响应时间，而且分析往往想要历史趋势，需要关联不同系统的数据，这些都没办法被一个OLTP系统满足。

几乎每个企业都会创建一个大的data warehouse，定期导入来自不同DB的数据。尽管这些系统要么超出预算，要么只满足了一部分需求，但还是物有所值的。

Data warehouse与OLTP非常不同：前者面向ad-hoc查询优化（更复杂），后者面向更新优化。

Data warehouse的标准实现方式（星型）：
- 一个fact table保存每条记录的who、what、when、where
- 若干个dimension table，保存fact table中记录要引用的各维度数据，fact table中使用foreign key来引用这些数据

Data warehouse被认为更适合用bit-map作为索引，而OLTP则更适合使用B-tree。

Data warehouse还经常使用materialized view作为一种优化手段，而OLTP则不然。

很多DB提供商会同时提供data warehouse和OLTP系统，两者共用一套前端解析。这样能更好地实现那些只对其中一个系统有用的功能。例如OLTP系统习惯用2B来表示一个州（如CA），而data warehouse可以只用6b，从而减少bit-map大小。这样的需求是一套系统满足不了的。

但在stream processing领域，一套前端解析也不够，不同系统需要从前端开始分开。

## Stream processing

### Emerging sensor-based applications

这里讨论的stream processing似乎主要指实时处理大量传感器产生的数据，比如：
1. 军队需要能监控士兵、枪械、舰艇的状态，需要能查询历史数据，有aggregation。
1. 智能交通、地图类应用等。

传统的DB没办法满足这些需求，需要专门的stream processing engine。

### An existing applicaton: financial-feed processing

financial-feed指市场动态、特定新闻、完成的交易、投标、询价等实时数据的投递。

例子：一家公司需要订阅多个finalcial feed，如果有某个feed延迟了就提醒交易员。他们不满足于自己实现的系统的性能，需要一个专门的stream processing engine。

## Performance discussion

StreamBase是能满足这个需求的stream processing engine，它可以在一台2005年的普通家用机上每秒处理160K条消息，而对应的DBMS系统只能处理900条。

> 但这个产品已经死了。

### “Inbound” versus “outbound” processing

传统DBMS的核心是不丢数据，它的处理模型称为“process-after-store”：
- 插入数据
- 构建索引并提交事务
- 回复用户

实时应用中，必须在处理前存储，显著增加了延时和处理每条消息的开销。可以用下面的处理模型来避开存储带来的瓶颈：
- 处理过程中数据顺着网络和内存走
- 读写存储都是可选的，且很多时间可以异步进行

这种处理模型称为“inbound”，而传统DBMS的处理模型称为“outbound”。DBMS的trigger可以做一点inbound处理，但：
- trigger是事后补充的，有很多限制
- trigger的安全性没办法保证（如避免死循环）
- trigger不支持materialized view
- 很多系统的trigger的性能都不好

outbound系统使用pull模式，engine把数据从存储介质中拉出来，而inbound则是push模式，数据被上游模块主动推给下游模块。

outbound系统存储数据，在其上执行查询；inbound系统存储查询，将数据流过这些查询。

### The correct primitives

SQL支持复杂的聚合，大概长这个样子：

```sql
SELECT AVG(salary)
FROM employee
GROUP BY department
```

最后一行被处理完之后会输出聚合结果。但这个特点不适合stream processing，后者没有所谓的结束。

相对地，stream processing系统增加了time window概念，window有很多种，前一节的例子很适合用sliding window。

StreamBase支持在带有两个参数的window上执行聚合：
- timeout：可以不等late data直接关闭window
- slack：直到满足条件再关闭window

SQL也可以支持window，但这对存储没意义（因此outbound系统不太可能支持）。

### Seamless integration of DBMS processing and application logic

传统的DBMS是client-server架构，应用代码与DBMS在不同的地址空间，交互会带来开销。但在StreamBase中，三类功能可以自由组合在一起：
- DBMS处理（filter、aggregation）
- 模块间消息传递
- 应用逻辑

这种DBMS逻辑与程序功能的无缝结合在多年前已经出现在Rigel和Pascal-R中了。商业DBMS的存储过程只相当于非常受限的编程系统。Object-relational DB提供了比存储过程更强大的功能，但仍然不够灵活。

stream processing engine也需要存储，如：
- 引用数据
- 转换表
- 历史数据

StreamBase嵌入了BerkeleyDB存储数据，访问成本远低于访问另一个DB server。

传统DBMS基于安全性的考虑，必须要提供隔离机制。因此一个DBMS为了更好地支持实时应用，可能需要同时支持嵌入和非嵌入模型。这也在鼓励放弃“one size fits all”原则。

回到我们的feed例子，这里数据都是来源于其它系统，stream processing和DBMS代码都是同一批人写的，就更没有理由继续支持传统DBMS了。

### High availability

传统DBMS使用了基于log恢复状态的方案来实现高可用，这种方案在stream processing中有几个问题：
- 恢复时间长，恢复期间没办法服务。
- 为了避免丢数据，需要想办法缓存输入的数据流。
- 只能恢复表数据，没办法恢复operator状态。如果要求所有operator都将自己状态保存到DB中，又会带来开销。

一种替代方案是使用串联（Tandem-style）风格的模块对：遇到crash时，应用切换到热备份机器上，这样只需要很短时间就可以继续服务了。因此StreamBase关掉了BerkeleyDB的log。

> 这里对比的是单机DB，不需要联想到分布式协议上。基本思想可能是用本地双写替代log。

很多stream processing engine能接受恢复出不完整的数据。比如监控类应用，如果能接受处理时丢一些数据点，也就能接受恢复时丢一些数据点。而报警类应用能接受重复消息，但不能接受丢消息。

### Synchronization

传统的DBMS使用ACID来隔离不同请求，在streaming系统中，没有多用户，因此可以只用简单的临界区就实现请求间的隔离，而不需要全功能的事务。

## One size fits all?

前面的例子表明StreamBase，这种与传统DBMS非常不同的系统，在处理实时数据上有多么巨大的优势。在其它领域我们也可以有这种专门的系统。

### Data warehouse

传统的OLTP DBMS是基于行的，行与行连续，面向写优化。Data warehouse需要面向读优化，因此列存储模式更高效。

### Sensor networks

传统DBMS在处理大量传感器产生的数据时不够高效，需要有更灵活、轻量的DB系统。

> NoSQL

### Text search

需要：
- 能处理大量append-only的写入
- 能处理大量的ad-hoc读
- 机器数多，容易出现错误，对可用性要求高

> ElasticSearch，Solr

### Scientific databases

> 类似于sensor networks

### XML databases

> 略

## A Comment on Factoring

多数基于stream的应用需要三类服务：
- 消息传输
- 状态存储
- 应用逻辑执行

如果基于传统DBMS，一条消息需要6次网络传输。为了避免这些开销，stream processing engine需要能在一个进程内提供这三种能力。

这就引出一个问题，当前基于模块（应用server、DBMS、ETL、消息总线、文件系统、web server等）来构建系统的作法是否是最优的。

## Concluding Remarks

未来会有许多领域特定的具有不同能力的db engine。未来的DBMS市场会非常有趣。未来会有大量新的，与“business data processing”模式非常不同的应用，看起来没有一套系统可以同时支持这些应用，“one size fits all”不太可能继续下去了。