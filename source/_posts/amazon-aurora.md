Amazon Aurora目前有两篇文章：
- 2017年的[Amazon aurora: Design considerations for high throughput cloud-native relational databases](https://dl.acm.org/doi/abs/10.45/3035918.3056101)。
- 2018年的[Amazon aurora: On avoiding distributed consensus for i/os, commits, and membership changes](https://dl.acm.org/doi/abs/10.1145/3183713.3196937)。

其中第一篇内容比较多，重点讲架构；第二篇重点讲的是实现细节。

## 背景

在Aurora之前，云数据库有两个思路：
1. RDS，单机的MySQL/PostgreSQL几乎原封不动地移到VM上运行，底下是云存储（EBS），我们称这种为shared-disk，属于计算存储分离的架构。
1. Spanner，将数据水平切分为若干个shard，由多个节点服务，相互不共享数据，涉及到不同节点的事务需要使用两阶段提交（2PC），我们称这种为shared-nothing。

> 两种思路的优缺点可以参考[可否对比一下 TiDB 与 AWS Aurora ？ - 朱元的回答](https://www.zhihu.com/question/56669587/answer/552118039)。

shared-disk的优点是最大限度保证兼容（MySQL/PostgreSQL），通常只需要动存储层，不需要改上面的query解析、plan生成等，另外不涉及分布式事务，性能比较稳定；缺点就是规模上不去。

shared-nothing的优点是架构更scalable，缺点就是很难保证与已有应用的完全兼容，另外单shard事务与多shard事务性能会有区别，分布式事务开销比较大。

Aurora仍然是顺着RDS的shard-disk的思路演进。它要解决的最大问题是RDS的网络吞吐问题。

### 写放大

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/aurora-01.png)

如上图，一个MySQL进程会产生如下写流量：
1. redolog
1. binlog
1. data block
1. double-write
1. frm文件

而为了实现高可用，通常用户会将主备实例分别放到两个AZ，每个AZ再分别有两份存储。两个AZ的实例之间会采用block级别的replication。这样的架构有两个问题，一是写放大非常严重，会产生非常多的网络流量，考虑到计算节点是单点，它的网络带宽就成为了瓶颈。二是上图中的1、3、4是串行同步的，多几跳网络会增加延时，还会显著增加延时抖动的概率（OLTP场景非常在乎延时抖动）。

这几种写流量里，binlog和frm可以放到一边（可以单独存储），对于redolog、data、double-write，Aurora的解法就是只写redolog，将其它的page管理全都offload到存储层，也就是“THE LOG IS THE DATABASE”。

## 架构

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/aurora-02.png)

Aurora默认会部署到三个AZ中，可以有一个可写的主实例与多个只读实例。

每个DB的数据会切为若干个10GB大小的Protection Group（PG），10GB这个大小可以保证单个PG副本的MTTR不会太长（在10Gbps的网络下10秒内可以恢复一个副本），从而提升可用性。整个DB最大是64TB。

每个PG在每个AZ会写两份数据，这样一共是6份存储，每份称为一个Segment。一致性协议上Aurora仍然延续了Amazon的传统，使用了6/4/3的quorum协议，即6份存储、写至少要4份、读至少要3份。

> 使用quorum协议而不是MultiPaxos/Raft有两个好处，一是网络少一跳（不需要经leader转发），二是quorum能容忍单个Segment的写出现空洞，可用性更高。

采用这种配置的目的是能在1个AZ整个挂掉的情况下继续服务写，在1个AZ加1份存储挂掉的情况下继续服务读。

> [请问Aurora系统为什么必须要6份copy来支持“AZ+1”failures？ - 江枫的回答](https://www.zhihu.com/question/435302730/answer/1634390470)

## 存储层

Aurora最大的创新就是将复杂的page管理放到了存储层。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/aurora-03.png)

DB将redolog写进存储层的UPDATE QUEUE之后就返回了，之后存储层内部完成log补齐（quorum协议会导致每个Segment的log不全，相同PG的不同Segment之间会通过gossip协议交换log record）、page管理（应用log、多版本管理、垃圾回收）、备份到S3等操作。

## 一致性保证

Aurora的一致性保证的前提是单写多读（实际支持多写，但论文中没提，似乎多写也不保证全局唯一顺序，这里不讨论），由唯一的写实例来控制存储的整体推进，从而在quorum协议（通常被认为只能保证最终一致性）基础上实现了DB需要的各种一致性。整个过程中PG的各个副本（Segment）之间不会有任何的信息同步，保证了低开销。

> 第二篇paper中有一句话：“Storage nodes do not have a vote in determining whether to accept a write, they must do so”。

DB负责为每个log record分配一个单调增的Log Sequence Number（LSN）。

每个Segment收到的log可能是有空洞的，其中Segment Complete LSN（SCL）表示最大的保证完整的LSN，即这个Segment在SCL之前是完整、没有空洞的。

DB可以根据每个Segment的SCL生成Protection Group Complete LSN（PGCL，即PG的最大完整LSN）和Volume Complete LSN（VCL，即Volumn的最大完整LSN）。

下图说明了PGCL和VCL的区别。PG1只保存奇数LSN，PG2只保存偶数LSN，则PGCL1为103，PGCL2为104，VCL为104。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/aurora-04.png)

每个DB事务在存储引擎这里可能会分为若干个mini-transaction（MTR），如页面的分裂合并等，每个MTR可能包含多个log record，在恢复时要保证MTR的完整性。因此Aurora会标记每个MTR的最后一条log为Consistency Point LSN（CPL），在恢复数据时只能恢复到VCL之前最大的一个CPL，称为Volume Durable LSN（VDL）。

假设DB目前看见三个CPL分别是900、1000、1100，此时VCL为1007（大于1007的log不完整），则VDL为1000，大于1000的log都会被截断。

### log chain

不同的log record会发送给它修改的block所在的PG，因此每个PG/Segment只能看到部分log record。为了避免Segment之间的同步，每个log record会包含有：
- 整个Volume中它的上一个LSN。
- 这个Segment中它的上一个LSN。
- 它修改的block的上一个LSN。

这样Segment根据log chain就可以知道自己当前的log是否完整了。

### 写入

Aurora的DB在执行写操作时是全异步I/O，在将log record发给对应的Segment后就会将当前请求放到等待队列中继续处理下一个请求。每当有请求执行完，DB会判断是否要提升VDL，之后将等待队列中所有LSN小于等于VDL的请求标记为提交完成。为了避免太多请求等待，DB在分配LSN时会保证LSN与VDL之间的gap不会太大（LSN Allocation Limit，LAL，默认为1000万）。

### 恢复

可以看到client的commit在超前于VDL时是不返回的，因此Aurora可以在failover时将大于VDL（不完整）的所有log都截断掉而不影响一致性。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-01/aurora-05.png)

Aurora中DB初始化与故障恢复走相同流程：
- DB确认可连接的PG达到read quorum。
- DB收集各Segment的SCL，本地计算得到PGCL、VCL、VDL。
- DB向各个Segment发送truncate，将大于VDL的log删掉，避免故障前的请求飘到Segment上。后续新生成LSN时会将截断这段（VDL+1到VDL+LAL）跳过去。
- 修复那些达不到write quorum的PG（仍然使用gossip协议）。
- 提升所有达到write quorum的PG的epoch，避免旧的DB实例连上来。采用epoch比lease好的地方在于它不需要等待lease过期。

在PG都恢复之后，DB自己可以慢慢做undo，不影响服务。

### 读取


