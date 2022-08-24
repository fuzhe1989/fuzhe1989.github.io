---
title:      "[笔记] Anti-Caching: A New Approach to Database Management System Architecture"
date:       
tags:
---

> 原文：[Anti-Caching: A New Approach to Database Management System Architecture](https://dl.acm.org/doi/pdf/10.14778/2556549.2556575)

**TL;DR**

Anti-Caching 是基于 H-Store 实现的 in-memory db。作者认为传统的 RDBMS 是以磁盘为主存，内存为缓存（cache），而 in-memory db 则应该反过来，内存才是主存，磁盘只是用于 swap，因此称其为 “anti-caching”。

<!--more-->


