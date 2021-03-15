---
title:      "[笔记] Pig Latin: A Not-So-Foreign Language for Data Processing"
date:       2020-10-11 18:18:37
tags:
    - 笔记
    - BatchProcessing
---

> 原文：[Pig Latin: A Not-So-Foreign Language for Data Processing](https://dl.acm.org/doi/abs/10.1145/1376616.1376726)

## TL;DR

> 关于Pig Latin的逸事：[大数据那些事(7)：腾飞的拉丁猪](https://mp.weixin.qq.com/s/1OwY353VTfVrPfY2bxhGhA)

<!--more-->

## Introduction

Pig Latin又是一个瞄准了MapReduce表达能力弱点的类SQL语言，它声称“集合了体现SQL精神的高阶查询与类MapReduce的过程式程序”。

下面的SQL：

```sql
SELECT category, AVG(pagerank)
FROM urls WHERE pagerank > 0.2
GROUP BY category HAVING COUNT(*) > 100000;
```

在Pig Latin中写成：

```
good_urls = FILTER urls BY pagerank > 0.2;
groups = GROUP good_urls BY category;
big_groups = FILTER groups BY COUNT(good_urls) > 100000;
output = FOREACH big_groups GENERATE category, AVG(good_urls.pagerank);
```

Pig Latin写起来比MapReduce更容易，表达能力更强，而又比SQL更过程化，更容易手动优化。

Pig Latin内建对GROUP BY、FILTER、FOREACH、COUNT等操作的支持允许它的编译器和runtime对query plan进行深度优化，如果这些操作都藏在mapper和reducer内部，就没办法做这些优化了。

SQL在这方面有比较大的优势，相同的query可以被解释为非常不同的plan，充分发挥声明式语言的优势。但同时用户一方面可能没办法很好预测最终的执行plan，缺乏掌控感，另一方面很多时候query optimizer无法生成最优的plan，要么因为人掌握的信息无法输入进去，要么因为optimizer能力不足。而这是Pig Latin的优势。

Pig Latin的其它优点包括：
1. 支持嵌套类型（map、set、tuple）。
1. 支持Java的UDF。
1. 支持in-situ ad-hoc query，可以直接操作无schema的文件：`good_urls = FILTER urls BY $2 > 0.2;`。

## Details

Pig Latin支持以下四种数据类型：
- atom：单个值，`'alice'`。
- tuple：一组可以类型不同的值（还可以嵌套），`('alice', 'lakers')`。
- bag：一组可以重复的tuple，不要求所有tuple有相同的field和元素数量。

    ![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/pig-latin-02.png)
- map：一组key-value对，其中key必须是atom，value可以是任意类型。

    ![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/pig-latin-03.png)

下表是Pig Latin的表达式类型。

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/pig-latin-01.png)

Pig Latin中通过`LOAD`来导入数据：

```
queries = LOAD 'query_log.txt'
          USING myLoad()
          AS (userId, queryString, timestamp);
```

queries的类型是bag。上面的`USING`和`AS`是可选的。注意整个Pig Latin的执行是lazy的，直到数据需要被输出（如`STORE`）的时候，`LOAD`等指令才会真正执行。

Pig Latin中的`FOREACH`类似于`map`：

```
expanded_queries = FOREACH queries GENERATE
                   userId, FLATTEN(expandQuery(queryString)）;
```

expanded_queries的类型也是bag。

可以用`FLATTEN`来平铺嵌套类型（看起来像`CROSS JOIN`）：

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/pig-latin-04.png)

`FILTER`：

```
real_queries = FILTER queries BY userId neq 'bot';
```

`COGROUP`会将不同set的数据按各自的key做colocation，与`JOIN`的区别在于它不会有一个product效果，如以下两个bag：

```
results: (queryString, url, position)
revenue: (queryString, adSlot, amount)
```

的`COGROUP`为：

```
grouped_data = COGROUP results BY queryString,
                       revenue BY queryString;
```

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/2021-03/pig-latin-05.png)
