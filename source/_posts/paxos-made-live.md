---
title:      "[笔记] Paxos Made Live - An Engineering Perspective"
date:       2021-03-16 19:51:30
tags:
    - 笔记
    - Paxos
    - Consensus
---

> 原文：[Paxos Made Live - An Engineering Perspective](https://dl.acm.org/doi/abs/10.1145/1281100.1281103)

## TL;DR

<!--more-->

## Introduction

[Chubby](/2020/08/28/the-chubby-lock-service-for-loosely-coupled-distributed-systems)的第一个版本基于一个第三方可容错的商业数据库（下文称为3DB），它的replication有很多bug，并没有基于任何已有证明的算法，也无法证明其正确性。最终Chubby将3DB替换为了基于Paxos的实现。

作者在实现Paxos的过程中，发现这个工作并不trivial：
- 描述Paxos的伪代码只要一页就够了，但作者的C++实现多达几千行。膨胀的原因是实际的生产系统需要的很多特性和优化是原paper中没有的。
- 社区习惯于证明篇幅短的算法（一页伪代码）的正确性，但其证明方法难以扩展到几千行代码的规模，导致作者必须使用不同的广域来证明其实现的正确。
- 容错算法只能处理一个精心选择的固定集合中的错误，而真实世界有着多得多的错误类型，包括了算法本身的错误、实现的错误、操作错误。
- 真实系统很少能有精确的spec，甚至在实现过程中还会修改spec，就需要实现本身具有可塑性，也许就会有实现“误解”了spec导致的错误。

这篇文章就在讲作者将Paxos从理论搬到实际的过程中的一些挑战。

## Architecture Outline

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/paxos-made-live-01.png)

在Chubby的架构中，Paxos用来构建最底层的log，有意地与上层的DB分离，这样log层可以给其它系统复用。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/paxos-made-live-02.png)

log层的API如上图。一旦一个值submit了，client在各个replica上设置的callback会被触发，并得知submit的值。

## On Paxos

### Paxos Basics

基本的Paxos算法包含三个阶段：
1. 选出coordinator。
1. coordinator选出一个值，将其包含进一个accept消息，广播给其它replica。其它replica可以回复acknowledge或reject。
1. 一旦多数replica回复了acknowledge，共识达成，coordinator会广播一个commit消息。

有可能出现以下情况，多个replica都认为自己是coordinator，且选出不同的值。为了保证仍然能达成共识，有以下两种机制：
1. 连续的coordinator之间有序。
1. 限制每个coordinator对值的选择。

每个coordinator有自己的sequence number（或称term）来保证顺序。一个replica想要成为coordinator时，它需要产生一个大于已知所有sequence number的新number并广播包含这个number的propose消息，其它replica的回复称为promise（前面的阶段1）。

达成共识的值就不能再被修改。因此Paxos需要保证后续coordinator仍然选出相同的值，做法是replica的promise消息中包含它见过的最新的值和sequence number，新的coordinator会使用所有回复中最新的值。如果所有回复中都没有值，新coordinator才可以自由选择值。

### Multi-Paxos


