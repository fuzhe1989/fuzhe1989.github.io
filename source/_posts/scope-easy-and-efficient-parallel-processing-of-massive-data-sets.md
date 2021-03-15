---
title:      "[笔记] SCOPE: Easy and Efficient Parallel Processing of Massive Data Sets"
date:       2020-10-11 18:18:37
tags:
    - 笔记
    - BatchProcessing
---

> 原文：[SCOPE: Easy and Efficient Parallel Processing of Massive Data Sets](http://www.vldb.org/pvldb/vol1/1454166.pdf)

## TL;DR

SCOPE是一种分布式批处理语言，将类SQL的脚本编译为可以运行在Cosmos平台上的分布式作业。

它的特点：
- 同时支持类SQL语法和分步的声明式语法。
- 支持使用C#编写扩展类和脚本内直接调用C#表达式。
- 支持导入已有的脚本，且可传递参数。
- 编译期支持基于开销进行优化，重写执行计划；运行期也支持优化，比如按rack做预聚合。

<!--more-->

## 语法

SCOPE支持两种语法，既可以直接写成类似SQL的形式：

```
SELECT query, COUNT(*) AS count
FROM "search.log" USING LogExtractor
GROUP BY query
HAVING count > 1000
ORDER BY count DESC;
OUTPUT TO "qcount.result";
```

也可以使用声明式语法：

```
e = EXTRACT query
 FROM “search.log"
 USING LogExtractor;
s1 = SELECT query, COUNT(*) as count
 FROM e
 GROUP BY query;
s2 = SELECT query, count
 FROM s1
 WHERE count > 1000;
s3 = SELECT query, count
 FROM s2
 ORDER BY count DESC;
OUTPUT s3 TO “qcount.result";
```

## 架构

![](https://fuzhe-pics.oss-cn-beijing.aliyuncs.com/scope-01.jpg)

SCOPE是建立在Cosmos分布式执行平台之上的（参考[Dryad](/2020/10/11/dryad-distributed-data-parallel-programs-from-sequential-building-blocks/)）。

## SCOPE语言

SCOPE提供了EXTRACT和OUTPUT两条指令来定义输入和输出：

```
EXTRACT column[:<type>] [, …]
FROM < input_stream(s) >
USING <Extractor> [(args)]
[HAVING <predicate>]

OUTPUT [<input>
 [PRESORT column [ASC | DESC] [, …]]]
TO <output_stream>
[USING <Outputter> [(args)]]
```

用户可以用C#实现自己的Extractor和Producer来扩展这两条指令。

SCOPE还支持SELECT和JOIN，其中FROM子句支持嵌套，但不支持子查询。但用户可以用OUTER JOIN来绕开这一限制，如下面的SQL：

```
SELECT Ra, Rb
FROM R
WHERE Rb < 100
 AND (Ra > 5 OR EXISTS(SELECT * FROM S
 WHERE Sa < 20
 AND Sc = Rc))
```

可以改写为：

```
SQ = SELECT DISTINCT Sc FROM S WHERE Sa < 20;
M1 = SELECT Ra, Rb, Rc FROM R WHERE Rb < 100;
M2 = SELECT Ra, Rb, Rc, Sc
 FROM M1 LEFT OUTER JOIN SQ ON Rc == Sc;
Q = SELECT Ra, Rb FROM M2
 WHERE Ra > 5 OR Rc != Sc; 
```

SCOPE脚本中还可以直接调用C#表达式，如：

```
R1 = SELECT A+C AS ac, B.Trim() AS B1
 FROM R
 WHERE StringOccurs(C, “xyz”) > 2

#CS
public static
int StringOccurs(string str, string ptrn)
{
 int cnt=0; int pos=-1;
 while (pos+1 < str.Length) {
 pos = str.IndexOf(ptrn, pos+1) ;
 if (pos < 0) break;
 cnt++;
 }
 return cnt;
}
#ENDCS
```

其中`#CS`和`#ENDCS`中间可以直接嵌入C#代码。

用户还可以自己定制PROCESS、REDUCE、COMBINE三类算子，从而实现类似于[MapReduceMerge](2020/09/27/map-reduce-merge-simplified-relational-data-processing-on-large-clusters/)的操作。
