---
title:      "[笔记] WiscKey: Separating Keys from Values in SSD-Conscious Storage"
date:       2017-05-19 10:03:07
tags:
    - 笔记
    - KV
    - LSM
---

WiscKey: Separating Keys from Values in SSD-Conscious Storage

WiscKey是一个基于LSM的KV存储引擎，特点是：针对SSD的顺序和随机读写都高效的特点，Key和Value分开存储以最小化IO放大效应。YCSB场景中它比LevelDB和RocksDB都快。

<!--more-->

## 1 介绍

目前的KV存储引擎中，对写性能要求比较高的大多数都采用了LSM，典型的有BigTable/LevelDB/Cassandra/HBase/RocksDB/PNUTS/Riak。LSM相比其它索引树（如B树）的主要优势在于，它的写都是顺序写。B树在有少量修改时，都可能产生大量的随机写，不管是SSD还是SATA上都表现不佳。

为了保证写性能，LSM会不停的批量把KV对写成文件；为了保证读性能，LSM还需要不停的做背景compaction，把这些文件合并成单个按Key有序的文件。这就导致了相同的数据在它的生命期中被反复读写。一个典型的LSM系统中数据的IO放大系数可以达到50倍以上。

LSM的成功在于，它充分利用了SATA磁盘顺序IO性能远超随机IO的特点（100倍以上），只要IO放大不超过这个数字，那么用顺序IO来替代随机IO就是成功的。

但到了SSD上就不一样了。SSD与SATA的几个不同：

1. 顺序IO和随机IO的差别没那么大，这让LSM为了减少随机IO而付出的额外的IO变得不再必要。
2. SSD可以承受高并发的IO，而LSM利用的并不好。
3. 长期大量的重复写会影响SSD的性能和寿命。

以上3个因素综合起来，会导致LSM在SSD上损失90%的吞吐，并增加10倍的写负载。

本文介绍的WiscKey是专门面向SSD的改良LSM系统，其核心思想是分离Key和Value，只在LSM中维护Key，把Value放在log中。这样Key的排序和Value的GC就分开了，在排序时避免了Value的写放大，整个LSM更小，cache效率更高。

分离Key和Value带来的挑战：

1. Scan时性能受影响，因为Value不再按Key的顺序排列了。WiscKey的解法是充分利用SSD的高并发。
2. 需要单独做GC来清理无效数据，回收空间。WiscKey提出在线做轻量GC，只需要顺序IO，最小化对前台负载的影响。
3. crash时如何保证一致性。WiscKey利用了现代文件系统的一个特性：append不会产生垃圾。

大多数场景WiscKey的性能都远超LevelDB和RocksDB，除了一个场景：小Value随机写，且需要大范围的Scan。

## 2 背景和动机

### 2.1 LSM

![wisckey_lsm.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-01.png)

可以看到LSM中一个kv对要经历5次写：

1. log文件；
2. memtable；
3. immutable memtable;
4. L0文件；
5. 其它level的文件。

LSM用多次的顺序IO来避免随机IO，从而在SATA磁盘上获得比B树高得多的写性能。

> （下面是对compaction的介绍，LevelDB的基于层的compaction，略）

在读的时候，LSM需要在所有可能包含这个Key的memtable和文件中查找，与B树相比，多了很多IO。因此LSM适合于写多读少的场景。

### 2.2 LevelDB

LevelDB的整体架构见上节的图。LevelDB包括一个磁盘上的logfile，两个内存中的memtable（memtable和immutable memtable)，以及若干个磁盘上的L0-L6的SSTable文件。

LevelDB插入时先写logfile，再写进memtable；memtable满了之后变成immutable memtable，再写成L0的SSTable文件。每层SSTable文件的size比例差不多是10。L1-L6的SSTable都是通过compaction生成的，LevelDB保证每一层的各个SSTable文件的KeyRange不重叠，L0除外。

查找时就是在所有memtable和SSTable中做归并。

### 2.3 读写放大

读写放大是LSM的主要问题。

写放大：文件从L<sub>i-1</sub>到L<sub>i</sub>的过程中，因为两层的size limit差10倍，因此这次Compaction的放大系数最大可以到10。这样从L0到L6的放大系数可以达到50（L1-L6每层10）。

读放大：假设L0有8个文件，那么查找时最多需要读14个文件(L1-L6每层最多1个文件)；假设要读1KB的数据，那么每个文件最多要读24KB的数据（index block + bloom-filter blocks + data block）。这么算下来读的放大系数就是14*24=336。如果要读的数据更小，这个系数会更大。

一项测试中可以看到实际系统中的读写放大系数：

![wisckey_wr_amplification.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-02.png)

必须要说明的是，LSM的这种设计是为了在SATA磁盘上获得更好的性能。SATA磁盘的一组典型数据是寻址10ms，吞吐100MB/s，而顺序读下一个Block的数据可能只要10us，与寻址相比延时是1:1000，因此只要LSM的写放大系数不超过1000，就能获得比B树更好的性能。而B树的读放大也不低，比如读1KB的数据，B树可能要读6个4KB的Block，那么读放大系数是24，没有完全拉开和LSM的差距。

### 2.4 快速存储硬件

SSD上仍然不推荐随机写，因为SSD的整块擦除再写以及代价高昂的回收机制，当SSD上预留的Block用光时，它的写性能会急剧下降。LSM的最大化顺序写的特性很适合SSD。

![wisckey_ssd_performance.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-03.png)

但与SATA非常不同的是，SSD的随机读性能非常好，且支持高并发。

## 3 WiscKey

WickKey的设计出发点就是如何利用上SSD的新特性：

1. Key与Value分离，Key由LSM维护，而Value则写入logfile。
2. 鉴于Value不再排序，WiscKey在读的时候会并发随机读。
3. WiscKey在Value log的管理上有自己的一致性和回收机制。

WiscKey在去除了LSM的logfile后仍然能保证一致性。

### 3.1 设计目标

WiscKey脱胎于LevelDB，可以作为关系型DB和分布式KV的存储引擎。它兼容LevelDB的API。

设计目标：

1. 低写放大：既为了写性能，也为了SSD的寿命。
2. 低读放大：读放大会降低读的吞吐，同时还降低了cache效率。
3. 面向SSD优化。
4. 丰富的API。
5. 针对实际的Key-Value大小，不做太不实际的假设。通常的Key都很小（16B），Value则从100B到4KB都很常见。

### 3.2 Key与Value分离

compaction就是导致LSM低效的主要原因：一遍遍的过数据。但不做compaction又没办法保证读的性能。

WiscKey受到了这么一个小发现的启示：我们要排序的只是Key，Value完全可以另行处理。通常Key要比Value小很多，那么排序Key的开销也就比Value要小很多。

WiscKey中与Key放在一起的只是Value的位置，Value本身存放在其它地方。

![wisckey_ssd_layout.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-04.png)

常见的使用场景下，WiscKey中的LSM要比LevelDB小得多。这样就大大降低了写的放大系数。Key为16B，Value为1KB的场景，假设Key的放大系数是10（LSM带来的），Value的放大系数是1，那么WiscKey的整体放大系数是(10 × 16 + 1024) / (16 + 1024) = 1.14。

查找的时候，WiscKey先在LSM中查找Key，再根据Key中Value的位置查找Value。因为WiscKey中的LSM比LevelDB中的小很多，前面的查找会快很多，绝大多数情况下都能命中cache，这样整个开销就是一次随机查找。而SSD的随机查找性能又这么好，因此WiscKey的读性能就比LevelDB好很多。

插入一组kv时，WiscKey先把Value写入ValueLog，然后再把Key插入到LSM中。删除一个Key则只从LSM中删除它，不动ValueLog。

当然这样的设计也遇到了很多挑战。

### 3.3 挑战

#### 3.3.1 并发范围查找

LevelDB中这么做RangeQuery：先Seek()，然后根据需求反复调用Next()或Prev()读出数据。LevelDB中Key和Value是存放在一起的，这么扫一遍对应底层就只有顺序IO，性能很好（不考虑读放大）。

WiscKey中Key和Value是分开存放的，这么做就会带来大量的串行随机IO，不够高效。WiscKey利用SSD的高并发随机读的特性，在对LSM调用RangeQuery期间，并发预读后面的N个Value。

#### 3.3.2 垃圾回收

LSM都是通过compaction来回收无效数据的。WiscKey中Value不参与compaction，就需要单独为Value设计GC机制。

一个土办法是扫描LSM，每个Key对应的Value就是有效的，没有Key对应的Value就是无效的。这么做效率太低。

WiscKey的做法是每次写入Value时也写入对应的Key。

![wisckey_log_layout.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-05.png)

上图中的head总是指向ValueLog的尾部，新数据写到这里。而tail会随着GC的进行向后移动。所有有效数据都在tail~head区间中，每次GC都从tail开始，也只有GC线程可以修改tail。

GC时WiscKey每次从tail开始读若干MB的数据，然后再查找对应的Key，看这个Key现在对应的Value还是不是log中的Value，如果是，再把数据追加到head处。最终，ValueLog中的无效数据就都被清理掉了。

为了避免GC时crash导致丢数据，WiscKey要保证在真正回收空间前先把新追加的数据和新的tail持久化下去：

1. 追加数据；
2. GC线程调用fsync()将新数据写下去；
3. 向LSM中同步写一条记录：`<'tail', tail-vlog-offset>`；
4. 回收空间。

WiscKey的GC是可配置的，如果Key的删除和更新都很少发生，就不需要怎么做GC。

#### 3.3.3 崩溃时的一致性

WiscKey为了保证系统崩溃时的一致性，使用了现代文件系统（ext4/btrfs/xfs等）的一个特性：追加写不会产生垃圾，只可能在尾部缺少一些数据。在WiscKey中这个特性意味着：如果Value X在一次crash后从ValueLog中丢失了，那么所有X后面写入的Value就都丢了。

crash中丢失的Key是没办法被发现的，这个Key对应的Value会被当作无效数据GC掉。如果查找时发现Key存在，但对应的Value不在ValueLog中，就说明这个Value丢失了，WiscKey会将这个Key从LSM中删除，并返回"Key不存在"。（没办法找回上一个Value了是吗？）

如果用户配置了sync，WiscKey会在每次写完ValueLog后，写LSM前，调用一次fsync。

总之WiscKey保证了与LevelDB相同的一致性。

### 3.4 优化

#### 3.4.1 ValueLog的写缓冲

WiscKey不会每笔写入都调用一次ValueLog的write，这样效率太低。WiscKey为ValueLog准备了一个buffer，所有写都写进buffer，当写满或者有sync请求时再write写到ValueLog中。读取的时候优先读取buffer。

缺点是在crash丢的数据会多一些，这点与LevelDB类似。

![wisckey_write_unit_size.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-06.png)

#### 3.4.2 优化LSM的log

WiscKey中LSM只用于存储Key，而ValueLog中也存储了Key，那么就没必要再写一遍LSM的log了。

WiscKey在LSM中存储了一条记录`<'head', head-vlog-offset>`，在打开一个DB时就可以从head-vlog-offset处开始恢复数据。将head保存在LSM中也保证了一致性不低于LevelDB，因此整体的一致性仍然不低于LevelDB。

### 3.5 实现

ValueLog会被两种方式访问：

1. 读取时会随机访问ValueLog。
2. 写入时会顺序写入ValueLog。

WiscKey用pthread_fadvise()在不同场景声明不同的访问模式。

WiscKey为RangeQuery准备了一个32个线程的背景线程池来随机读ValueLog。

为了有效地从ValueLog中回收空间，WiscKey利用了现代文件系统的另一个特性：可以给文件打洞（fallocate)。现代文件系统允许的最大文件大小足够WiscKey用了（ext4允许64TB，xfs允许8EB，btrfs允许16EB），这样就不需要考虑ValueLog的切换了。

## 4 评价

机器配置：

1. CPU：Intel(R) Xeon(R) CPU E5-2667 v2 @ 3.30GHz * 2；
2. 内存：64GB；
3. OS：64-bit Linux 3.14；
4. 文件系统：ext4；
5. SSD：500-GB Samsung 840 EVO SSD，顺序读500MB/s，顺序写400MB/s。

### 4.1 基准测试

1. 工具：db_bench；
2. DB：LevelDB/WiscKey；
3. Key：16B；
4. Value：很多大小；
5. 压缩：关闭。

#### 4.1.1 Load

第一轮：顺序插入100GB的数据。第二轮：uniform随机写。注意第一轮顺序写不会导致LevelDB和WiscKey做compaction。

![wisckey_load_perf_seq.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-07.png)

即使在256KB场景中，LevelDB的写入吞吐仍然距离磁盘的带宽上限很远。

![wisckey_load_perf_ldb_dist.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-08.png)

可以看到小Value时LevelDB的延时主要花在写log上，而大Value时延时主要花在等待写memtable上。

![wisckey_load_perf_rand.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-09.png)

LevelDB的吞吐如此之低，原因在于compaction占了太多资源，造成了太大的写放大。WiscKey的compaction则只占了很少的资源。

下图是不同ValueSize下LevelDB的写放大系数。

![wisckey_load_perf_write_amp.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-10.png)

#### 4.1.2 Query

第一轮：在100GB随机生成的DB上做100000次随机查找。第二轮：在100GB随机生成的DB上做4GB的范围查找。

![wisckey_query_perf_rand.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-11.png)

LevelDB的低吞吐原因是读放大和compaction资源占用多。

![wisckey_query_perf_range.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-12.png)

ValueSize超过4KB后，LevelDB生成的SSTable文件变多，吞吐变差。此时WiscKey吞吐是LevelDB的8.4倍。而在ValueSize为64B时，受限于SSD的随机读能力，LevelDB的吞吐是WiscKey的12倍。如果换一块支持更高并发的盘，这里的性能差距会变小一些。

但如果数据是顺序插入的，那么WiscKey的ValueLog也会被顺序访问，差距就没有这么大。64B时LevelDB是WiscKey的1.3倍，而大Value时WiscKey是LevelDB的2.8倍。

#### 4.1.3 垃圾回收

测试内容：1. 随机生成DB；2. 删掉一定比例的kv；3. 随机插入数据同时后台做GC。作者固定Key+Value为4KB，但第二步删除的kv的比例从25%-100%不等。

![wisckey_gc_perf.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-13.png)

100%删除时，GC扫过的都是无效的Value，也就不会写数据，因此只降低了10%的吞吐。后面的场景GC都会把有效的Value再写进ValueLog，因此降低了35%的吞。

无论哪个场景，WiscKey都比LevelDB快至少70倍。

#### 4.1.4 崩溃时的一致性

作者一边做异步和同步的Put()，一边用ALICE工具来模拟多种系统崩溃场景。ALICE模拟了3000种系统崩溃场景，没有发现WiscKey引入的一致性问题。（不比LevelDB差）

WiscKey在恢复时要做的工作比LevelDB多一点，但都与LSM最后一次持久化memtable到崩溃发生之间写入的数据量成正比。在一个最坏的场景中，ValueSize为1KB，LevelDB恢复花了0.7秒，而WiscKey花了2.6秒。

WiscKey可以通过加快LSM中`head`记录的持久化频率来降低恢复时间。

#### 4.1.5 空间放大

我们在评估一个kv系统时，往往只看它的读写性能。但在SSD上，它的空间放大也很重要，因为单GB的成本变高了。所谓空间放大就是kv系统实际占用的磁盘空间除以用户写入的数据大小。压缩能降低空间放大，而垃圾、碎片、元数据则在增加空间放大。作者关掉了压缩，简化讨论。

完全顺序写入的场景，空间放大系数很接近1。而对于随机写入，或是有更新的场景，空间放大系数就会大于1了。

下图是LevelDB和WiscKey在载入一个100GB的随机写入的数据集后的DB大小。

![wisckey_space_amp_perf.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-14.png)

LevelDB多出来的空间主要是在加载结束时还没来得及回收掉的无效kv对。WiscKey多出来的空间包括了无效的数据、元数据（LSM中的Value索引，ValueLog中的Key)。在GC后无效数据就没有了，而元数据又非常少，因此整个DB的大小非常接近原始数据大小。

KV存储没办法兼顾写放大、读放大、空间放大，只能从中做取舍。LevelDB中GC和排序是在一起的，它选择了高的写放大来换取低的空间放大，但与此同时在线请求就会受影响。WiscKey则用更多的空间来换取更低的IO放大，因为GC和排序被解耦了，GC可以晚一点做，对在线请求的影响就会小很多。

#### 4.1.6 CPU使用率

![wisckey_cpu_usage.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-15.png)

可以看到除了顺序写入之外，LevelDB的CPU使用率都要比WiscKey低。

顺序写入场景LevelDB要把kv都写进log，还要编码kv，占了很多CPU。WiscKey写的log更少，因此CPU消耗更低。

范围读场景WiscKey要用32个读线程做背景的随机读，必然用多得多的CPU。

LevelDB不是一个面向高并发的DB，因此CPU不是瓶颈，这点RocksDB做得更好。

### 4.2 YCSB测试

![wisckey_ycsb.png](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2020-11/wisc-key-16.png)

（直接上图，结论不说了）