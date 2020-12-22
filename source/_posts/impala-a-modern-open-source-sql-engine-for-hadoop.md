> 原文：[Impala: A Modern, Open-Source SQL Engine for Hadoop](https://2013.berlinbuzzwords.de/sites/2013.berlinbuzzwords.de/files/slides/Impala%20tech%20talk.pdf)

## TL;DR

TODO

<!--more-->

## User View of Impala

Impala是建立在Hadoop生态之上的MPP query engine，用到了多种Hadoop标准模块（Metastore/HDFS/HBase/YARN/Sentry）并提供了类似于RDBMS的体验。

Impala瞄准的是标准BI场景，用户可以通过ODBC/JDBC访问Impala。

### Physical Schema Design

用户可以在建表时提供一组partition列：

```sql
CREATE TABLE T (...) PARTITIONED BY (day int, month int) LOCATION '<hdfs-path>' STORED AS PARQUET;
```

类似于Hive，未分区的表的数据文件直接存放在表的根目录下，有分区的表的数据文件则存放在分区对应的子目录下，如`<root>/day=17/month=2/`。

Impala支持多种文件格式，如文本文件（压缩/未压缩）、sequence file、RCFile、Avro、Parquet。用户可以在`CREATE TABLE`或`ALTER TABLE`时指定文件格式，还可以指定具体受影响的分区，如`ALTER TABLE PARTITION（day=17, month=2) SET FILEFORMAT PARQUET`。

### SQL Support

Impala支持Java/C++写的UDF和C++写的UDA。