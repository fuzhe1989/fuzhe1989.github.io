---
title:      "How DBMS handle dead locks: wound-wait and wait-die"
date:       2021-05-06 21:22:58
tags:
    - Database
---

**TL;DR**

Wound-wait 与 wait-die 是 DBMS 中处理死锁的两种常见策略，它们的优缺点为：
- Wait-die 会导致更多 rollback，但 rollback 的代价更低：被 rollback 的事务做过的事情比较少。
- Wound-wait 导致的 rollback 较少，但 rollback 的代价更高：被 rollback 的事务做过的事情比较多。

另外 [[1]] 中提到：

> 还有一种方法，no wait，就是请求不到锁就回滚，不去做判断。现在的一般看法是，不等比等好，尤其是应用于分布式事务时。

<!--more-->

**wait-die 与 wound-wait**

在基于锁的事务实现中，当事务 T<sub>i</sub>需要获得某个 data item 的锁时，发现这把锁当前正在被事务 T<sub>j</sub>持有，我们不能直接让 T<sub>i</sub>等待这把锁，否则可能造成死锁。

在一个单机 DBMS，或有全局死锁检测的分布式 DBMS 中，可以通过死锁检测来做决定。但如果无法获得全局状态，我们可以使用 wait-die 或 wound-wait 策略来避免产生死锁。

这里我们有三种选择：
1. T<sub>i</sub>直接 abort。
1. T<sub>i</sub>等待 T<sub>j</sub>执行完，再获取这把锁。
1. 强行终止 T<sub>j</sub>的执行，从而让 T<sub>i</sub>获取到锁。

Wait-die 是比较 T<sub>i</sub>与 T<sub>j</sub>的 timestamp，如果 Ts<sub>i</sub> < Ts<sub>j</sub>（即 T<sub>i</sub>更老），则 T<sub>i</sub>等待（wait）；否则（T<sub>i</sub>更新）T<sub>i</sub>直接 abort（die）。

Wound-wait 同样是比较 timestamp，但相反，如果 Ts<sub>i</sub> < Ts<sub>j</sub>（即 T<sub>i</sub>更老），则强行终止 T<sub>j</sub>（wound）；否则（T<sub>i</sub>更新）T<sub>i</sub>等待（wait）。

两种策略都只用 timestamp 来做判断，避免依赖各自的读写集，从而获得更好的性能。但需要系统不同节点产生的 timestamp 是可比较的（TimeStatusOracle、TrueTime、HLC 都可以）。

**两者的相同点**

- 更老的事务总会有机会执行。
- 被 abort 的事务随后会使用**相同的**timestamp 重启。最终这些事务会成为系统中最老的事务，从而不再被 abort。

**两者的区别**

- Wait-die 中被 abort 的新事务是试图获取锁的一方
- Wound-wait 中被 abort 的新事务是持有锁，正在运行的一方。

**比较两者的代价**

关于 abort 代价：
- 更新的事务通常持有更少的锁，且已经读写过的数据更少。
- 更老的事务通常持有更多的锁，且已经读写过的数据更多。

因此更老的事务的 abort 代价更高。

关于 abort 数量：
- 大多数锁被老事务持有。
- 大多数加锁请求由新事务发起（新事务持有的锁更少，因此加锁请求更多）。
- 因此大多数冲突是新事务试图获取老事务持有的锁。

Wait-die 中新事务试图获取老事务持有的锁会导致自身被 abort；而 wound-wait 中类似情况下则是 wait，因此 wait-die 会导致更多的 abort。

但 wound-wait 中被 abort 的事务必须已经持有了一部分锁，它们通常也已经读写过一些数据了（并非刚开始的事务），这些事务的 abort 代价更大；而 wait-die 中大多数被 abort 的事务可能一点读写都没做过，它们的 abort 代价更小。因此 wait-die 每个 abort 的代价会更小一些。

结论就是很难说哪种策略更优（视具体情况讨论）。

## References

1. [有哪些分布式数据库可以提供交互式事务？ - 陈广胜的回答 - 知乎](https://www.zhihu.com/question/344517681/answer/815329816)
1. [What is the difference between wait-die and wound-wait algorithms - Stackoverflow](https://stackoverflow.com/q/32794142)
1. [Comparing the wait-die and wound-wait schemes](http://www.mathcs.emory.edu/~cheung/Courses/554/Syllabus/8-recv+serial/deadlock-compare.html)
