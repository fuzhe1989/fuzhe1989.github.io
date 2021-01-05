> 原文：[Integrating Compression and Execution in Column-Oriented Database Systems](https://dl.acm.org/doi/abs/10.1145/1142473.1142548)

## TL;DR

数据库中压缩算法的使用是非常影响性能的。这方面列存要比行存更有优势：每列排列在一起天然就适合压缩，而行存中相同列的值是被其它列隔开的。

这篇文章探讨的是，如何在列存数据库中使用压缩，以及不同压缩算法在不同场景中的比较。

<!--more-->

## Advantages of Column-Stores vs. Row-Stores in Compression

列存相比行存，在压缩方面有以下优势：
- 列存可以用的压缩算法种类更多。一列的值连续排列，就允许跨行压缩多个值，比如RLE等。此外行存常用的字典等方法仍然可以在列存中使用。
- 列存的压缩率通常更高。一列的值通常比较类似。
- 列存的page通常比行存的page更紧凑，扫描时cache命中率更高，CPU消耗更低（尤其是值等大小时），可以使用向量化代码提高解压速度。
- 列存可以有不同的key order（参考C-Store），而排序好的值更容易压缩。
- 像RLE这样的压缩算法可以不解压直接操作（比如看到`("x", 100)`就可以直接选中或淘汰100个值），更是加速了操作。

## Related Work

Graefe和Sahpiro[[1]])指出了延迟物化的好处，数据在内存中保持压缩状态，只在必要时解压。

Chen等人[[2]]指出有些算子可以临时性解压来求解谓词，如果有效的话仍然返回压缩数据。

[[1]]也提出了直接在压缩数据上操作的想法。他们指出如果谓词中的常量部分与数据的压缩方式相同，则可以直接在压缩数据上进行exact-match比较和natural-join。如果使用顺序一致的压缩方式的话，还可以不解压直接做exact-match的index查找。接下来还可以做projection和去重。

但以上工作都没有尝试一个操作同时作用在多个值上，这是本文的贡献之一。

本文的另一个贡献是提出了一种新架构，可以在算子间传递压缩数据，且最小化算子代码复杂度，同时最大化直接在压缩数据上操作的可能。之前的一些工作（[[2]], [[3]], [[4]]）也指出了将压缩本身与更高level的代码隔离的重要性，但通常只是在算子间传递压缩数据，算子内处理前解压。

与Zukowski等人在MonetDB/X100上的工作[[5]]相比，本文更侧重列存上的压缩算法与压缩数据上的直接操作，而[[5]]更侧重提升行存上轻量压缩算法的CPU/cache性能。

## References

1. [G.Graefe and L.Shapiro. Data compression and database performance. In ACM/IEEE-CS Symp. On Applied Computing pages 22 -27, April 1991.][1]
1. [Z. Chen, J. Gehrke, and F. Korn. Query optimization in compressed database systems. In SIGMOD ’01, pages 271–282, 2001.][2]
1. [J. Goldstein, R. Ramakrishnan, and U. Shaft. Compressing relations and indexes. In ICDE ’98, pages 370–379, 1998.][3]
1. [T. Westmann, D. Kossmann, S. Helmer, and G. Moerkotte. The implementation and performance of compressed databases. SIGMOD Rec., 29(3):55–67, 2000.][4]
1. [M. Zukowski, S. Heman, N. Nes, and P. Boncz. Super-scalar ram-cpu cache compression. In ICDE, 2006.][5]

[1]: https://cs.brown.edu/courses/cs227/archives/2008/Papers/Compression/GraefeShapiro.pdf
[2]: https://dl.acm.org/doi/abs/10.1145/375663.375692
[3]: https://ieeexplore.ieee.org/abstract/document/655800/
[4]: https://dl.acm.org/doi/abs/10.1145/362084.362137
[5]: https://ieeexplore.ieee.org/abstract/document/1617427/