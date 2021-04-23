---
title:      "[笔记] One Size Fits All? - Part 2: Benchmarking Results"
date:       2021-03-13 22:36:40
tags:
    - 笔记
    - Database
---

> 原文：[One Size Fits All? - Part 2: Benchmarking Results](http://static.cs.brown.edu/people/jduggan/research/osfa.pdf)

**TL;DR**

> 2005年的前文：[‘One Size Fits All’: An Idea Whose Time Has Come and Gone](/2020/08/06/one-size-fits-all-an-idea-whose-time-has-come-and-gone)

和前文一样，这篇文章仍然在呼吁针对不同的新兴领域使用不同类型的数据库系统，而不是传统的一套系统打所有（one size fits all）。

<!--more-->

## 概述

传统的RDBMS的架构很多年都没怎么变过：行存、B-tree索引、适应磁盘的block大小、面向tuple的处理（volcano）。这些年厂商们增加了以下功能，但主体结构未变：
- 除了传统的shared-memory，现在RDBMS还支持shared-disk和shared-nothing架构。
- XML（时代的印记。。。）。
- 数据仓库相关的功能，如数据压缩、物化视图、index-only的表、join index（C-Store）等。

所有这些增强的意义就在于one size fits all，其背后的理由是：
- 一套代码的维护代价小。
- 销售成本、用户学习成本低。
- 市场推广费用低。

但时代不同了大人，新兴领域需要新的远优于过往的解决方案。作者这里定义“远优于”为至少好10倍（具体数字可以讨论，但显然2到3倍是不够的），如1分钟响应与6秒响应，20核的刀片机组与2核的普通PC等。在这种优势下，用户会愿意尝试新架构，否则还不如等待硬件的进步。

作者举出了可能存在这种10倍优势方案的4个新领域：
- 文本检索。
- 数据仓库。
- 流计算。
- 科学计算与AI。

面对这些新领域的挑战，作者认为DBMS架构可能有以下几种变化：
- 不当回事（yawn）。传统的DBMS仍然有自己的细分市场（niche），但新系统会抢走它们在新领域的市场。
- 多个系统共享一个parser，一个入口，多个实现。作者认为挑战在于如何实现出结合历史与流数据的StreamSQL（另一个挑战在于SQL的表达能力足够吗）。
- 一个DBMS底下有多个插件，如使用列存引擎来打数据仓库，但最终产物可能不那么完美，比较像旧方案。
- 数据联邦（data federation，翻译太沙雕），与多个系统共享parser的区别在于前者是静态的，已知的，而数据联邦是动态的，未知的。作者认为这条路已经走了差不多30年，还看不到出路。
- 从头重写。一个全新的系统是有可能同时满足以上领域的需求的，想象一下它的存储可以是动态的，从行到列（HTAP）。这样的系统需要有更具通用性的优化器和执行器。另一个思路是为全文检索增加fast path。
