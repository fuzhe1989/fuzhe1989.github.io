---
title:      "[笔记] Dryad: Distributed Data-Parallel Programs from Sequential Building Blocks"
date:       2020-10-11 15:25:21
tags:
    - 笔记
    - BatchProcessing
    - Microsoft
---

> 原文：[Dryad: Distributed Data-Parallel Programs from Sequential Building Blocks](https://cse.buffalo.edu/~stevko/courses/cse704/fall10/papers/eurosys07.pdf)

## TL;DR

Dryad是一个分布式批处理框架，相比MapReduce，它的特点是：
- 支持多种operator组成的DAG，且支持组合多个DAG。
- DAG中多个顶点可以分配给一个进程。
- 不同顶点间的消息传递不限于文件，还可以通过TCP和in-memory FIFO。
- 支持按网络拓扑插入中间节点做部分聚合。

Dryad属于是底层框架，用户真正用到的还是上面的框架，类似于MapReduce和Sawzall的关系。

<!--more-->

## 架构

Dryad有一个JobManager负责调度，每台机器上有一个daemon负责起进程和监控，还有name server可以查询机器和机器的位置。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/dryad-01.jpg)

Dryad底下也有一个类似于GFS的共享存储，顶点间默认用它来通信，但也可以使用TCP Pipe和in-memory FIFO。

后两种通信方式性能更高，但因为是同步通信，可能有死锁问题，比如接收端顶点未启动。

## 操纵DAG

Dryad可以处理以下对DAG的修改：
- 增加顶点。
- 组合两个DAG以增加边。
- 合并两个DAG。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/dryad-02.jpg)

## 执行

Dryad中有逻辑DAG和物理DAG，逻辑DAG里的一个stage可能对应物理DAG的一组顶点，这样来简化监控和处理。

Dryad只负责顶点间纯字节流的通信，具体的序列化和反序列化由应用自己负责。

一台机器可以根据自己的负载能力一次接受多个顶点，这些顶点程序会在一个进程中，通过in-memory FIFO进行通信。这样不需要把不同顶点真的合并在一起，简化了处理，又保证了它们不会跨进程、机器通信，提高了性能。

每个stage对应一个stage manager，它可以在运行期从JM获得各个顶点的运行情况，从而做一些比较复杂的操作，比如冗余运行这个stage中比较慢的节点。

## 优化

大多数数据中心的网络都是两层结构，机器直连rack的交换机，rack的交换机再连主交换机。Dryad可以根据网络拓扑，在原有的DAG中插入中间层进行部分聚合。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/dryad-03.jpg)

也可以复制更多的中间顶点以提高整体的并行度。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/dryad-04.jpg)
