---
title:      "[笔记] Consensus on Transaction Commit"
date:       2021-03-16 19:51:30
tags:
    - 笔记
    - BigData
---

> 原文：[Consensus on Transaction Commit](https://dl.acm.org/doi/abs/10.1145/1132863.1132867)

## TL;DR

这篇文章是Jim Gray和Leslie Lamport两位图灵奖大佬的合作产物，是数据库与系统两个领域在consensus方面的结合。

Paxos Commit算法使用Paxos弥补了2PC在容错性上的软肋，而进一步的Fast Paxos Commit算法则减少了一轮commit的延时跳数，代价是增加了总的通信次数。

文章另外还论证了经典的2PC就是Paxos Commit的一种特例。

<!--more-->

## Introduction

本文介绍了一种Paxos consensus commit算法，结合了传统的2PC与Paxos。

文章假设：
- 整个算法由若干进程执行，这些进程之间通过消息传递来通信。
- 每个进程运行在网络中的一个节点上，每个节点上可以有多个进程。
- 节点间的通信延迟不可忽略，同节点进程间的通信延迟可以忽略。
- 消息可能丢失或重复，但不会发生无法察觉的损坏（不考虑拜占廷问题）。
- 失败节点上的进程只会停止运行，而不会执行错误的操作，且节点状态不会丢失。

一个consensus算法通常需要满足两种正确性相关的性质：safety和liveness。safety性质描述了什么可能发生（allowed），而liveness性质描述了什么一定会发生（must）。

Paxos consensus是异步的，即它的safety不依赖于进程要及时处理，或通信延时要有上界。而它的进度（progress）则依赖于进程处理的及时性和通信延时。

假如一个节点上的进程能够在一个已知的时间内完成消息处理，我们称它为nonfaulty节点。当且仅当一个网络中所有节点都是nonfaulty的，且所有消息通信的延时也有上界时，这个网络也是nonfaulty的。

## Transaction Commit

分布式系统中处理事务的进程称为resource manager（RM）。每个RM可以自行决定事务中与自己有关的部分要commit还是abort，而整个事务想要commit则必须所有参与的RM都同意commit。整个transaction commit的关键就在于所有RM要对事务是commit还是abort达成共识。

本节假设每个事务涉及的RM是预先确定的。

事务处理过程中RM的状态变化如图：

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/consensus-commit-01.png)

两个safety要求：
- Stability：RM一旦到达committed或aborted状态就不再改变。
- Consistency：所有RM的终态相同，不可能一个RM到达committed而另一个到达aborted状态。

另外还有两个要求：
- 任意RM到达committed状态的前提是所有RM都已经经历过prepared状态了：prepared阶段用来确定是否所有RM一致同意commit。
- RM可以直接从working状态到达aborted状态：任意RM可以提前abort。

上述算法的问题在于当RM可能失败（如宕机）或因通信错误而无法连接时，不存在non-trivial的方法让所有RM达成共识。FLP理论[[1]]也暗示了一个确定性的、完全异步的算法无法在允许哪怕单节点失败时满足上述stability和consistency性质。

考虑到liveness后，transaction commit协议需要有以下两个性质：
- Non-triviality。如果在commit过程中网络是nonfaulty的，则：
    - 如果所有RM到达prepared状态，它们也会到达committed状态。
    - 如果任意RM到达aborted状态，所有RM会到达aborted状态。
- Non-blocking。只要存在某个时刻，有足够多的网络节点已经处于nonfaulty状态足够长时间了，这些节点上的RM就可以都到达committed或aborted状态。

作者进一步明确了transaction commit算法允许的操作。算法的初始断言是所有RM都处于working状态，另外有两个状态断言：
- canCommit：当且仅当所有RM处于prepared或committed状态时为TRUE。
- notCommitted：当且仅当没有RM处于committed状态时为TRUE。

RM在状态转换中的每一步都只能执行以下两种操作：
- Prepare：RM状态从working到prepared。
- Decide：如果RM处于prepared状态，且canCommit，则它可以转变为committed状态；如果RM是working或prepared状态，且notCommitted，则它可以转变为aborted状态。

## Two-Phase Commit

### Protocol

2PC算法引入了transaction manager（TM）来协调整个事务的运行。TM有init、preparing、committed、aborted状态，RM的状态不变。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/consensus-commit-02.png)

任意RM到达prepared状态后，它会发送prepared消息给TM，TM随后向其它所有RM发送prepare消息。当收集到所有RM的prepared后，TM可以到达committed状态，并向所有其它进程发送commit。收到的RM就可以到达committed状态。

这个过程中TM的prepare消息是可选的，RM也可以自发进入prepared状态（如超时触发），这也是很多2PC的变种用来节省通信次数的方法。

当有RM自发从working变成aborted状态（如超时触发），如果TM不是committed状态，它也会进入aborted状态，随后向其它RM发送abort。

### Cost

一个transaction commit算法的关键效率指标就是它在正常路径（transaction到达commited）下的开销。假设有N个RM，则正常路径下2PC会发送如下消息：
- 触发2PC的RM发送prepared给TM（1次）。
- TM发送prepare给其它RM（N-1次）。
- 其它RM发送prepared给TM（N-1次）。
- TM发送commit给所有RM（N次）。

总开销为3N-1次消息。假设TM与其中某个RM在一个节点上，则消息数减少为3N-3。

如果令RM自发到达prepared状态，则可以省掉N-1次TM到RM的prepare消息，总消息数为2N，但此时要么引入额外的通信延时，要么对实时性有假设。

另外2PC还有3次落盘的写延时：初始RM的prepare写、其它RM的prepare写、TM作出commit决定的写。如果令所有RM都并发prepare，前2种写延时可以合并，总延时变为2次写。

### Problem

2PC中的任意RM失败（包括TM没有及时收到prepared）意味着整个事务abort，但如果TM失败了，整个流程会阻塞到TM恢复。尤其是如果所有RM都发送完prepared之后TM失败了，它们没办法知道事务是成功还是失败了，也就没办法做决定。

non-blocking commit协议要求单个进程失败不会阻止其它进程决定事务状态。很多这样的协议都是在2PC上打补丁，引入备TM。但据作者所知其中没有算法能给出清晰的正确性证明，如[[2]]就无法解释当一个进程收到两个自称为TM的进程的消息时该如何处理。

## Paxos Commit

### Paxos

Paxos算法是在解决若干个进程的共识问题。这些进程称为acceptor，它们要达成的共识是选择一个值。每个acceptor位于不同节点上。最基本的safety要求是只能选出一个值。为了排除无意义的解法，这个值必须是client提议过的值。liveness需求的断言是，如果有足够多的acceptor构成了足够长时间的nonfaulty网络，就一定能选出一个值。可以证明，如果没有严格的同步假设的话，为了能在最多F个acceptor失败时选出一个值，需要至少2F+1个acceptor（多数性）。

Paxos算法要求先选出leader，所有请求由leader处理，当前leader失败后会选出新的leader。Paxos需要有唯一leader来保证liveness，但可以在没有leader或多个leader下保证safety。

Paxos中每个proposal对应一个id，每个id从属于唯一的leader（不同leader产生的id不会重复）。

下面是Paxos的完整流程（假设有一个预设的leader）：
- phase 1a。leader选择一个大于所有已知id的新id bal，并将带有bal的p1a消息发给所有其它acceptor。
- phase 1b。当acceptor收到id为bal的p1a消息时，如果它还没有对大于等于bal的任何其它消息做操作，它就会回复一个p1b消息，其中包含：
    - 它收到p1a消息中的最大id。
    - 它已经回复的p2b消息中的最大id（如果有的话）。
- phase 2a。当leader收到bal的p1b回复达到多数，有两种可能：
    - Free：目前还未产生任何值。
    - Forced：已经产生了一个值v。

    Free下leader可以提议任意值（通常是第一个client提议的值）。Forced则要求leader必须提议v。接下来leader需要将带有bal和它选择的值v的p2a消息发给其它acceptor。
- phase 2b。当acceptor收到带有bal和v的p2a消息时，如果它还没收到过id大于bal的p1a或p2a消息，它会accept当前消息，并回复带有bal和v的p2b消息。
- phase 3。当leader收到的bal和v的p2b回复达到多数，v就是这次Paxos的结果，leader可以将带有bal和v的p3消息发给所有参与者（包括client和acceptor）。

在快速路径上（failure-free），leader在收到client的提议后，可以直接向其它acceptor发送id为0的p2a消息。如果回复p2b的acceptor达到多数，leader可以接着发送p3消息，整个流程结束。选择id为0是确认其它acceptor不会收到过其它leader的消息。

如果快速路径没成功，leader需要走完整流程，从p1a开始。

Paxos有两种优化思路。在快速路径上leader一开始可以只向F个acceptor发送p2a消息（如果收集不到足够的p2b回复再补发其它p2a消息）。另一个思路是acceptor广播p2b消息，这样可以省掉p3消息的延时，但增加了消息次数。

### Paxos Commit

如果将2PC中的TM做出committed/aborted决策这一步替换为一轮Paxos，就能解决2PC的容错问题。但此时TM仍然需要先得知所有RM都prepared，至少需要一轮消息延时。而本文提出的Paxos Commit算法则消除了这次延时。

Paxos Commit中为每个RM分别运行一个Paxos实例，决定对应RM是进入prepared还是aborted状态。所有这些Paxos实例共享一个leader和2F+1个acceptor，且假设RM知道acceptor列表。

每个RM会在自己的Paxos实例中发送id为0值为prepared或aborted的p2a消息来宣布它的决定。

每轮Paxos Commit通常是开始于某个RM决定prepare并向leader发送BeginCommit。leader随后发送prepare给所有RM，每个RM再在自己的Paxos实例中决定prepared还是aborted，而acceptor再将p2b回复给leader。对于每个Paxos实例，当leader收集到F+1个p2b回复后它就知道对应RM已经决定好了。当且仅当所有RM都回复了prepared，leader就可以选择commit。

为了提高效率，acceptor可以将它的多个p2b消息合并为一个发送，leader也可以将它对每个RM的多个实例的p3消息合并为一个。

如果有RM出错（如超时），leader会回退到发送p1a消息以确定当前状态。如果在p2a阶段leader发现它处于Free状态，说明没有RM做出决定，它就会走到abort分支。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/consensus-commit-03.png)

### Cost

假设有N个RM，2F+1个acceptor，且p2a消息只发给F+1个acceptor。正常路径下Paxos Commit包含以下消息：
- 第一个RM发送BeginCommit给leader（1次）。
- leder发送prepare给其它RM（N-1次）。
- 每个RM发送它的p2a消息给F+1个acceptor（N(F+1)次）。
- 每个acceptor将各个实例的p2b消息合并发送给leader（F次）。
- leader发送合并的p3消息给各个RM（N次）。

总次数为(N+1)(F+3)-2。

如果leader本身也是acceptor，且第一个RM的BeginCommit跟着它发给leader（也是acceptor）的p2a消息一起，则总次数减少为(N+1)(F+3)-4。如果每个acceptor与RM在相同节点上，且第一个RM与leader在一个节点上，则BeginCommit和F次p2a消息可以忽略，总次数减少为N(F+3)-3。

如果acceptor将p2b发送给所有RM，则延时少一跳，变成4跳，但总通信次数增加为N(2F+3)。如果加上前面这些优化，总次数减少为(N-1)(2F+3)。

现在Paxos Commit需要4跳延时，但其中有2跳是为了将prepare发送给所有RM。如果允许RM自发进入prepared状态，则延时只剩2跳。此时算法已达到最优，因为[[3]]已经证明了任何容错的共识算法至少需要2跳才能选出一个值。

在Paxos Commit中RM仍然需要一次写盘，每个acceptor也需要一次写盘（所有实例合并写），因此总计需要N+F+1次写盘。

## Paxos vs. Two-Phase Commit

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/consensus-commit-04.png)

在有5个RM，F为1（3副本）的系统中，2PC需要12次通信，默认的Paxos Commit需要17次，Fast PC需要20次。当N非常大时，三种算法分别需要3N、4N、5N次通信。

假设F为0，此时只有一个acceptor，且不可失败（RM仍然可以失败）。令acceptor与leader在相同节点上，此时2PC、Paxos Commit与Fast PC就相同了。因此2PC就是Paxos Commit在F为0时的特例。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/consensus-commit-05.png)

[1]: https://dl.acm.org/doi/abs/10.1145/3149.214121
[2]: http://www.sigmod.org/publications/dblp/db/books/dbtext/bernstein87.html
[3]: https://www.sciencedirect.com/science/article/pii/S0196677403001652