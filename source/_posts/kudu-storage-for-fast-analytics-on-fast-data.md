> 原文：[Kudu: Storage for fast analytics on fast data](https://kudu.apache.org/kudu.pdf)

## TL;DR

<!--more-->

## Introduction

很多Hadoop的用户会使用HDFS上的Parquet或Avro文件做OLAP，使用HBase做OLTP，但没办法结合起来。前者顺序写很厉害，但不支持实时更新，后者随机写很厉害，但分析性能差。一种折衷的方案是数据和修改流式写入HBase，再定期导出为HDFS上的Parquet文件。但这样的架构会有以下问题：
- 应用端要写复杂的代码维护两套系统。
- 要跨系统维护一致性的备份、安全策略、监控。
- 数据可见性差：更新进入HBase到最终能被查询到的延时可能很久。
- 实际场景中经常有要修改已经持久化到HDFS的文件的需求（如迟到的数据、订正等），需要做高开销的文件重写，还可能要人工介入。

Kudu是一种可以弥补HDFS与HBase间的功能gap的存储系统。它同时提供了行级别的insert、delete、update，和类似Parquet的列存数据扫描。

## Kudu at a high level

### Tables and schemas

Kudu中数据组织为表，每张表都要有schema，其中primary key是有序且唯一的。用户可以使用`ALTER TABLE`来增加或移除某些列（除了primary key）。

Kudu与HBase的一个区别在于它的列是有类型的，好处有两点：
1. 允许Kudu做类型特定的压缩，如int可以用bitpacking。
1. 方便与BI系统交互。

Kudu目前还不支持二级索引，或非primary key的列的唯一性保证。

### Write operations

Kudu本身只暴露了行级别的insert、update、delete接口，必须要指定完整的primary key。条件更新需要通过更上层的Impala等系统来实现。

目前Kudu也不支持多行事务，但保证单行写一定是原子的。

### Read operations

Kudu只支持scan这一种读操作，scan时用户可以传入若干个谓词，目前支持两种谓词：比较一列和一个常量，或primary key range。此外用户可以指定需要的列（projection）。

### Other APIs

Hadoop生态的很多系统（如MapReduce、Spark、Impala）都可以利用数据局部性，因此Kudu允许用户指定某个key range的数据存在特定server上。

### Consistency Model

Kudu提供了两种一致性，snapshot（默认）与read-your-write。

默认情况下Kudu不提供外部一致性，即client A写了一笔，外部通知client B，B可能看不到这笔写。解决方案是client A可以要求前面一笔写返回一个timestamp，将它传给B，B用这个timestamp去读。

另外Kudu还支持类似Spanner的commit-wait，但如果没有专门的硬件的话，commit-wait可能引入非常高的延时（默认的NTP配置下100-1000ms）。

Kudu使用的时间戳算法叫HybridTime（没看过，和HLC哪个好？）。

### Timestamps

Kudu与HBase的另一个区别在于它不允许用户自己填数据的timestamp。（时间和版本其实是非常不同的两个概念，HBase等系统将二者混用实际是在给自己挖坑，不幸的是Tablestore还跳进去了）

读的时候用户可以指定一个timestamp进行snapshot读。

## Architecture

### Cluster roles

Kudu有单master负责维护元数据，若干个tablet server服务用户数据。master支持replication。

### Partitioning

Kudu支持将表水平切分为若干个partition，它的特点是支持用户设置一个partition schema，类似一个函数，将primary key转为二进制的partition key。这样除了key-range和hash两种分区策略外，用户还可以自己实现更复杂的策略。

### Replication

Kudu没有使用HDFS，而是自己管理数据。每个tablet可以有多个replica，之间通过Raft协议保持一致。所有写请求都会发给leader，leader有本地的lock manager，使用MVCC来解决并发冲突。默认情况下Raft的心跳间隔是500ms，选举timeout是1.5s。

Kudu对Raft实现了以下小的优化：
- 选举过程中采用指数退避重试。
- 新leader与各个follower对比log时，原始Raft是从lastIndex往前一个一个找，Kudu直接跳回到committedIndex，能加快failover。

各个replica之间只同步log，不同步data，不同的replica可以有不同的compaction和文件格式，这带来了以下好处：
- 不太可能所有replica同时flush或compaction，减少了这些操作对集群负载和用户端延时的影响。
- 一个replica坏掉不会传播开，有机会修复。

#### Configuration Change

Kudu在增加replica数量时遵循Raft的one-by-one算法，如从3份变到5份会触发两次变更，3到4和4到5。

新replica一开始身份是VOTER，它会收到leader的`StartRemoteBootstrap`请求，开始从leader处拷贝log和数据，完成后再加载tablet追上剩余的增量数据。它有个问题是在3到4的过程中会导致多数replica数量变成3个，这就意味着新replica正常工作前已有的3个replica都不能出问题，否则Raft协议就会停住。

Kudu计划（貌似已经做了）新增一个Raft角色叫PRE_VOTER，不参与投票，在正常工作时再转变角色。

在减少replica数量时tablet server不会立即删掉数据，而是由master异步清理。

### The Kudu Master

Kudu的master职责：
- 管理catalog：table、tablet、schema、replication level等。
- 作为集群的coordinator：追踪哪些机器在线，并在机器宕机后重新调度replica。
- 作为tablet的目录：追踪每个tablet server在服务哪些replica。

选择中心化的、多replica的master的原因是实现简单、容易debug、运维方便。

#### Catalog Manager

master自己有一个单tablet的catalog表，并在内存中维护了一个完全写透的cache。

catalog表会保存当前的table schema、状态（creating、running、deleting等）、tablet列表。建表过程中master会先往catalog表里写数据，状态是creating，然后异步通知tablet server创建replica。如果某个tablet没有在多数server上成功创建replica，master会重复这个过程，废弃的replica会在后台清理掉。其它像schema变更和删表等操作也是类似的流程，master failover后可以根据表状态继续这些操作。master给tablet server发的请求都是幂等的，因此可以放心重试。

#### Cluster Coordination

tablet server在启动时会找master注册自己并汇报它所管理的所有tablet，随后定期汇报tablet的变化。

Kudu的一个关键的设计就是，master是catalog的source of truth，但它只是整个集群的tablet状态的旁观者，真正的tablet的状态来自tablet server。tablet的状态是通过Raft变化的，因此master只需要定期查看它持有的Raft的log index就可以知道tablet的状态是否需要更新。

这种设计赋予了tablet server很重的责任，比如leader要负责监控哪些follower已经很久没有通信成功过了，并从Raft配置中把相应的follower移除。一旦这次配置变更生效了，剩余的replica会向master汇报当前状态。

master在要求某个tablet server加载某个tablet时，也是通过发起Raft配置变更来实现的。如果这次变更失败了，master会反复重试，直到tablet server汇报的配置与预期相符了。每次配置变更都有一个唯一index，因此是幂等的，即使master发生了failover也能保证达到终态。

master移除不需要的replica也是通过tablet server的汇报来比对状态。它会反复发送`DeleteTablet`，直到成功。

## Tablet storage

### Overview

Kudu的tablet存储的目标是：
- 快速的列存扫描。
- 低延时的随机更新。
- 性能要稳定。

### RowSets

tablet的数据单元称为RowSet，有些RowSet只在内存中，称为MemRowSet，其它的同时分布在内存与磁盘中，称为DiskRowSet。所有未删除的行只存在于一个RowSet中（避免扫描聚合后还要去重），因此RowSet之间数据不重叠，但它们的key range可能重叠。

每个tablet同时间只会有一个MemRowSet来保存所有新插入的行。背景线程会定期将这个MemRowSet刷到磁盘上。flush过程是并行的：reader仍然可以读正在flush的MemRowSet，而对它的更新和删除则会在flush结束时一起刷到磁盘上（注意不会有插入）。

### MemRowSet Implementation

MemRowSet是一个并行B-tree加上乐观锁，大体上参考了MassTree的设计，但有以下区别：
- 不支持删除元素，而是使用MVCC来表示逻辑删除。
- 不支持任意的原地修改——修改前后值的长度不能变；这样可以用原子的CAS操作将修改追加到每条记录对应的链表上（不太懂，链表的节点是预先分配的吗？）。
- 所有叶子节点都通过`next`指针连起来了，类似于B+树。
- 没有实现完整的“trie of tree”，而是只维护了一棵树，因为Kudu不那么担心超高的随机访问吞吐。

为了优化扫描性能，内部和叶子节点的大小略大，达到了4个cache-line（256B），但牺牲了一些随机访问的性能。

MemRowSet与Kudu中的其它存储不同，使用了行存。在扫描时Kudu会使用SSE2来预取下个叶子节点，还会通过LLVM动态编译record的发射操作。

每行的primary key会被编码为partition key插入到B树中，这样可以通过memcmp来比较大小。

### DiskRowSet Implementation

MemRowSet在flush时会每32MB生成一个DiskRowSet，保证了不会有太大的DiskRowSet，后续的增量compaction也可以保证高效。

一个DiskRowSet有两个主要部分，base和delta。base是按列组织的行（PAX风格？），每列对应一个block，block分为若干个page，有内置的B树索引来做row_offset到column的访问加速。column page会使用很多种编码方式，如字典、bitshuffle、front coding，还可以用通用压缩算法再压一遍。每列用什么编码和压缩都是由用户指定的。

除了schema中有的列之外，Kudu还会把编码过的primary key以及分块的bloom filter写下去。

delta包含了这个文件对应的更新和删除，它包含内存中的DeltaMemStore和磁盘上的DeltaFile（可以是多个）。DeltaMemStore与MemRowSet有相同结构。DeltaFile是二进制类型的列块，两者都维护了`(row_offset, timestamp)`到RowChangeList的映射，其中row_offset是base中的编号，从0开始。

### Delta Flushes

后台的flush线程也负责将DeltaMemStore刷到磁盘上变成DeltaFile。

### INSERT path

在插入时Kudu需要检查所有DiskRowSet以保证没有相同行，通过使用bloom filter可以过滤掉绝大多数文件。bloom filter被切为若干个4KB的块，其上有一个不可变的B树索引。

每个DiskRowSet还会保存它的min/max，并使用一个区间树来索引DiskRowSet。后台的compaction会定期合并DiskRowSet来提高区间树的效率。

### Read path

Kudu的读路径总是批量处理行以平摊函数调用的开销，还为循环展开和SIMD提供机会。Kudu的内存中的batch格式本身也是面向列的，在从磁盘中load数据时不需要反复计算偏移。

在从DiskRowSet读取数据时，Kudu会先尝试应用range谓词来缩小行的范围。之后Kudu一次处理一列。它先定位到这列要读的位置，然后批量load数据到内存中（会解码），之后再看delta中有没有对应的修改，也应用到内存中。这个过程非常高效：delta的key是row_offset而不是primary key，因此只需要比较整数。

tablet server会保存scanner的状态，用户的后续请求可以继续使用这个scanner，不需要重复seek。

### Lazy Materialization

Kudu在扫描时会优先处理有谓词的列，这样可以缩小后续列的扫描范围，甚至不需要了。

### Delta Compaction

Kudu的后台线程会定期compact那些delta过大的DiskRowSet。在compaction过程中Kudu会只更新那些被修改过的列。

### RowSet Compaction

Kudu也会定期将多个DiskRowSet合并掉，并按32MB大小切成若干个新的DiskRowSet。这种compaction有两个目的：
1. 有机会真正删除行。
1. 减少了key-range重叠的DiskRowSet数量，降低了确定某个key时需要的磁盘访问数量的上限。
