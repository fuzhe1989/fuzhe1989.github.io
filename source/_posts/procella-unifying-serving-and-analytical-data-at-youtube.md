---
title:      "[笔记] Procella: Unifying serving and analytical data at YouTube"
date:       2020-12-06 16:01:20
tags:
    - 笔记
    - OLAP
    - Google
    - Columnar
---

> 原文：[Procella: Unifying serving and analytical data at YouTube](https://dl.acm.org/citation.cfm?id=3352063.3360438)

## TL;DR

Procella是一种可以适应不同查询需求的分析引擎，它的特点：
- 松耦合的schema要求，只要数据schema与table schema兼容，不需要完全一致，且文件本身的索引结构也可以惰性生成。
- 使用了新的列存格式Artus，内带索引结构，同时支持高性能的点查和扫描，且使用高性能的、不解压就可以操作的压缩算法。
- 使用Lambda架构，实时节点与compaction节点分离，前者支持高性能读写，后者可以在后台做复杂的优化。
- 多种优化，包括自适应的基于运行期抽样的query优化。
- 广泛使用多种cache。
- 完整的SQL支持。

<!--more-->

## Introduction

Procella可以同时满足在/离线的读写请求，涵盖以下YouTube的需求：
- 报表（reporting）与仪表盘（dashboard），需要支持准实时响应大数据量的复杂分析，以及读到足够新鲜的数据。
- 内嵌统计（embeded statistics），如video的喜爱或观看数，需要支持实时响应模式简单但参数变化非常多的读写请求，请求量级可以达到每秒百万次。
- 监控（monitoring），类似于dashboard，还需要有降精度、数据淘汰、近似函数、以及时序函数。
- 临时分析（ad-hoc analysis），qps不大，能忍受分钟级延时，但查询复杂（且难以预测），数据量可能非常大。

之前YouTube使用了不同系统分别满足以上需求，但遇到了下面这些问题：
- 数据需要用不同的ETL流程导入不同的系统，导致了资源浪费、数据不一致或质量差、加载时间长、开发和维护代价高、业务响应时间慢等问题。
- 不同系统有不同的API，需要用不同的工具（尤其有些系统不支持完整的SQL），学习成本高。
- 部分系统在YouTube的数据量级下有性能或伸缩性上的问题。

Procella实现了以下特性来解决上述问题：
- 支持几乎完整的SQL标准，包括复杂的多阶段join、分析函数和set操作。
- 通过计算（borg）与存储（colossus）分离实现了高伸缩性。
- 高性能，同时提供高吞吐与低延时。
- 通过lambda架构提供足够的数据新鲜度。

## Architecture

Procella基于了多种Google的infrastructure，这些系统的特性也影响了Procella的设计：
- 存储用Colossus，文件写完后不可变，远程访问延时高。
- 计算用Borg，运行大量小task比少量大task更有助于提高总的利用率（调度容易）；failover概率大，外加没有本地存储，更有理由拆小task了；单个task的性能难以预测。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-01.jpg)

### Data

Procella中数据按表组织，每张表保存为多个文件（或称为tablet、partition），主要使用一种叫Artus的列存格式，也支持Capacitor等格式。同一份数据可以被多个Procella实例服务。

与很多现代分析引擎类似，Procella也没有使用传统的B树来构建索引，而是使用了更轻量的结构，如zone map、bitmap、bloom filter、partition、sorted keys。这些结构部分是由registration server在文件注册时从文件头获取的，部分是由data server在评估query时惰性生成。

schema、table到file的映射、统计信息、zone map等数据主要存储在metadata store（BigTable和Spanner）中。

DDL命令（CREATE、ALTER、DROP等）会发给registration server，再存储到metadata store中。用户可以指定各种选项，包括实时还是离线导入。对于实时导入的表，用户还可以指定过期淘汰规则、降精度和compact的方式。

用户可以自己离线生成好数据，再用DDL命令把文件导入到表中。这个过程中RgS（就是registration server）会从文件头解析出table到file的映射和索引结构。如果文件头没有索引结构的话，Procella也能接受。

RgS还负责检查文件的schema与table的schema是否兼容。如果不兼容的话还可能裁剪或compact schema。

实时写入会发给ingestion server（IgS），如果数据schema与table schema不一致的话，IgS会做转换，再写进WAL中。同时IgS还会把数据发给对应的data server（根据data partition）。数据会先进data server的内存buffer中，再定期持久化下去（但只为了容错，不与全量数据合并）。IgS也可以同时把数据发给多个data server从而实现冗余服务。

WAL会被背景compact成data file。

两种路径并存可能导致最近几秒内数据是“脏”的，但随着compaction的进度会达到最终一致。如果为了一致性的考虑，query可以跳过buffer中的数据，但这样数据可见性就变差了。

compaction server会定期compact和重分区掉WAL文件，并转换为PAX风格的列存文件。过程中compaction server也可以执行用户定义的SQL逻辑来做过滤、聚合、淘汰等操作。每轮compaction之后compaction server会更新metadata server。被替换掉的文件随后会被清理掉。

### Query Lifecycle

client的query会发给Root Server（RS）。RS负责解析、重写、生成逻辑plan、优化，最后得到执行plan。这个过程中它会用到Metadata Server（MDS）的数据去掉不需要的文件。RS会构建一棵执行树以满足满足复杂query的时序（如shuffle）、数据依赖（如broadcast join）、多变的join策略等需要，其中节点是query block（多个算子），边是数据流（聚合、远端执行、错峰执行等）。还可以向执行树中插入自定义的算子来做多种优化。RS完成整个过程后会把执行树和相关的统计信息、错误等发回给client。

Data Server（DS）会从RS或其它DS处收到它要处理的query部分。执行时数据可以来自本地内存、Colossus文件、RDMA、或其它DS。Procella会激进地下推计算，将filter、projection、aggregation（包括TOP、UNIQUE、COUNT DISTINCT、QUANTILE等近似聚合）、join等都尽量推给最靠近相应数据的DS，允许DS使用各种编码原生的函数做计算以得到最优的性能。

## Optimizations

### Caching

Procella使用了多种cache来缓解计算存储分离带来的延时上升：
- DS会缓存Colossus的文件的handle。
- DS使用了单独的LRU cache来缓存每个文件的头。
- DS使用单独的cache缓存列存数据。Artus的内存与磁盘使用相同格式，减小了cache的开销。另外DS还会缓存一些派生信息，如复杂算子的结果，或是bloom filter。
- MDS会使用本地的LRU cache缓存各种元数据。
- Procella实现了affinity调度，将对相同数据和元数据的操作尽量调度到相同的DS或MDS上，极大提升了cache命中率。且这种affinity是松耦合的，如果请求跑到了其它server上，只是命中不了cache，不会有任何错误。

最终效果是在Procella的报表实例中，尽管内存只能装下2%的数据，但文件handle的命中率达到了99%，数据命中率达到了90%。

### Data format

Procella一开始使用Capacitor作为列存格式，但这种格式不适合于点查和小范围的scan，因此Procella又开发了Artus格式，同时支持高性能的点查和scan。它的特性是：
- 使用自定义编码而不是像LZW一样的通用压缩算法，可以不解压数据就直接seek。
- 进行多轮自适应编码，先扫描一轮数据得到比较轻量的信息（如distinct值的数量、min/max、有序性等），再选择适合的编码，如字典编码、RLE、delta等，从而得到2倍于ZSTD等通用算法的压缩率，同时仍然保留了不解压直接操作数据的能力。每种编码都有函数来预估它处理特定数据的压缩率和速度，Procella会根据用户的偏好来做选择。
- 选择能对有序列做二分查找的编码，同时还能做O(1)的基于行号的seek。对于像RLE这样的变长编码，Procella维护了一个skip block，每B行记录一个值。
- 使用了一种不同于ColumnIO（参见Parquet）的方式来表示嵌套和可重复的数据类型，即不记录RL和DL，而是将optional和repeated的中间字段也按列持久化下来，它们的值是元素数量（optional为0或1，repeated是非负值），parent不存在的字段则不会被记录下来。这样重建对象时就可以先读到parent的元素数量，再直接去子字段中读相应数量的记录即可。另一个好处是这样可以支持对子字段的O(1)的seek。
- 直接暴露字典索引、RLE等编码信息给evaluation engine。
- 在文件头和列头中记录丰富的元数据，如数据schema、排序、min/max、编码信息、bloom filter等。
- 支持倒排索引，目前主要是优化数组的`IN`操作。

以下是Capacitor与Artus的比较。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-02.jpg)

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-04.jpg)

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-03.jpg)

### Evaluation Engine

许多现代的分析系统会在查询时使用LLVM将执行plan编译为native code来提高性能。但Procella同时要服务在线和离线请求，对于在线请求编译开销太大。Procella因此用其它方式实现了名为Superluminal的evaluation engine：
- 重度使用C++的模板元编程从而在编译期生成代码。
- 以block为单位处理数据以利用向量化计算和可感知cache的算法的优势。
- 直接操作底层数据编码（而不是丧失了编码信息只能通过中间层来操作），并尽量保持这一特性。
- 以一种全列存的方式处理结构化数据，不持久化中间结果。
- 动态合并filter，并沿着执行plan一直下推到扫数据的节点。

以下是Superluminal和开源的Supersonic运行TPC-H的对比：

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-05.jpg)

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-06.jpg)

### Partitioning & Indexing

Procella支持多级的分区和聚类。通常fact table是按date分区，再按多个维度聚类的。维表通常会按维度key分区排序。这种特点利于Procella快速裁剪掉不需要读的文件和做co-partition的join而不需要shuffle数据。

MDS的内存中的元数据也使用了多种压缩算法，用有限的内存装下海量的元数据。MDS的裁剪对Procella的性能影响巨大，因此保证MDS的大多数操作只走内存是非常重要的。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-12/procella-07.jpg)

DS在处理query时会使用bloom filter、min/max、倒排索引等文件元数据来最小化磁盘访问，这些元数据会异步缓存在LRU cache中。

### Distributed operations

Procella有多种join策略，既可以显式使用hint，也可以由优化器来选择：
- broadcast，当join一边的表特别小时使用。
- co-partitioned，当join key分别是两边的partition key时使用。
- shuffle，当两边都很大，没办法根据join key分区的时候，数据会按join key发给若干个中间server来shuffle。
- pipelined，当右边是个复杂的query，但很可能结果集很小时，先执行右边，再把结果发送给左边做类似broadcast join的操作。
- remote lookup。很多时候，维表（构建端）很大，但按join key分区，但fact table（探测端）却不是。此时探测端DS会向构建端DS发送RPC，获取这次join需要的key和value。为了减少RPC的开销，探测端要用上所有可能的filter，批量发送key，最大程度减少RPC的数量。构建端也会下推projection和filter以保证只传输必要的数据。得益于Artus的高性能点查，Procella可以高效地执行lookup join。

Procella还应用了以下方法来缓解长尾请求对服务的影响：
- RS会在query执行期间维护DS延时的分位数，如果遇到了明显慢于中位数的DS，RS会选另一台DS发送一个backup请求。
- RS会控制发给DS的请求，以避免太多请求将DS压垮。
- RS会区分发往DS的请求的优先级，小query优先级更高。相应地，DS对高低优先级分别有一个线程池。

Procella会为非常大型的聚合增加一层预聚合。

### Query Optimization

Procella中有一种virtual table，类似于materialized view。在query时Procella会自适应使用virtual table来实现最优化的查询：
- 选择virtual table时不光使用size来判断，还会通过在table schema上匹配filter来看数据组织方式（分区、聚类等）是否合适。
- 可以从多个virtual table中读数据，再使用`UNION ALL`拼起来。
- 可以从不同virtual table中读不同时间段的数据，再使用`UNION ALL`拼起来。
- 可以自动识别star模式的join，并为没有在fact table中反规范化的维表插入一个join。

Procella的优化器同时使用了静态和自适应优化技术。在编译期使用基于规则的优化器，如filter下推、subplan去相关、常量折叠等。在执行期会对这次query用到的真实数据抽样得到统计信息，再基于统计信息来选择或调优算子。

传统的收集统计信息的方式不仅更复杂，而且它有个问题是越靠近叶子节点统计越准确。相比之下Procella中随着数据流过query plan而更新统计信息的方式更简单，保证了整个plan有着相同的准确度。

具体实现是在query plan中增加一种“collection sites”，基于这种信息来决定如何如何转换未执行的部分。目前Procella会在shuffle时收集信息，因为shuffle是一个天然的物化点。这些信息随后会被用于决定如何切分数据（包括了切分函数和reducer数量）。

Procella在做聚合的时候会根据预估的行数决定需要多少个预聚合节点。在join的时候Procella会为每个partition统计它的key count、min/max、可能还有bloom filter（如果key count不是特别大），这些统计信息通常就不抽样了，而是从全部数据中得到。

基于这些信息Procella能做出以下join的优化：
- broadcast join，会用到RDMA。
- 如果filter端可以构建出一个不太大的、假阳性率在10%左右的bloom filter，则可以用这个bloom filter来裁剪探测端。
- 如果join的一端已经按join key分区了，则可以只shuffle另一端匹配这些区间（min/max）的数据。
- 如果没有其它优化可做了，则可以根据两边的表大小来自动选取分区数量再shuffle。

在处理`ORDER BY`时，Procella会先用一个query来估计要排序的行数，确定分成多少个shard，再用一个query来估计分位数，来确定如何划分数据。

以上自适应优化对大型query效果很好，但其开销对于小query来说太大了。此时用户可以传入hint而不开启自适应优化。

另外目前自适应优化还不能应用到join顺序的选择上，这里可以继续使用传统的基于统计信息的优化。

### Data Ingestion

Procella提供了离线数据生成工具，将用户的数据用MapReduce转换为最适合Procella处理的形式。

### Serving Embeded Statistics

Procella需要支持特别高频的嵌入页面的计数器query，如`SELECT SUM(views) FROM Table WHERE video_id = N`，这种query扫过的数据量并不会太大，但要能以非常低的延时服务非常高的QPS，且这些值的更新频率也很高。Procella会以“stats serving”模式来运行这些实例，会启用以下优化：
- RgS会在新数据注册后立即通知DS加载数据，而不是惰性加载。
- MDS直接与RS编译在一起，省掉了RPC的开销。
- 所有元数据都会被预加载到内存中并保持更新。
- 激进地缓存query plan。
- RS会把相同key的请求合并到一起同时发给两个DS，从而避免延时长尾。
- 监控RS和DS的错误率和延时，一旦有问题就替换。
- 高开销的优化会被关掉。

## Performance

（略）

## Related Work

### Google Technologies

- Dremel主要是stateless，没有这么广泛使用cache；Dremel全球使用一个实例，而Procella为不同用户使用不同实例（从而开启不同优化）；Dremel存储使用Capacitor，没有索引，而Artus有丰富的索引。
- Mesa场景与Procella不太一样，本身不支持SQL。
- F1 Query主打的是query federation，查询与存储引擎分离，主要靠挖掘不同引擎的特点来适应不同的场景。而Procella则是使用同一套存储引擎来服务不同场景，查询与存储耦合在一起。
- PowerDrill主要针对大数据量的简单查询来优化，如日志等。
- Spanner主打的是ACID的OLTP需求。

### External Technologies

（只列一下产品）

- Ad-hoc analysis：
    - Presto以及对应的AWS Athena
    - Spark SQL
    - Snowflake
    - Redshift
- Real time reporting：
    - Druid
    - Pinot
    - ElasticSearch
    - Amplitude
    - Mixpanel
    - Apache Kylin
- Monitoring：
    - Stackdriver
    - Cloudwatch
    - Gorilla和Beringei
    - InfluxDB
    - OpenTSDB
- Serving Statistics：
    - HBase

