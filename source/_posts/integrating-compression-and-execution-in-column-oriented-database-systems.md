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

## C-Store Architecture

C-Store的内容可以参考[之前的博客](/2020/08/13/c-store-a-column-oriented-dbms)，这里简单回顾一下。

C-Store中数据分为RS和WS，RS是全量数据，WS是增量数据，有定期的tuple mover将WS的数据合并进RS。insert会直接写进WS，delete则需要在RS中维护一个标记，update实现为insert+delete。

C-Store的一大特点是数据可以保存为若干个projection，每个projection可以是完整schema的部分列，有自己的sort order（可有多个sort key），很多压缩算法（如RLE）都可以获得更好的压缩效果。不同projection之间通过join index关联。

C-Store的算子是面向列存的，相比传统的面向行存的算子，区别在于：
- selection返回bitset，支持高效合并。
- 有特殊的permute算子可以使用join index来重排序一列。
- projection不需要修改数据，因此没有额外开销；两个顺序相同的projection也可以无开销地连起来。
- join产生position，而不是value。

## Compression Schemes
 
### Null Suppression

Null Suppression的定义参考[Wikipedia](https://en.wikipedia.org/wiki/Zero_suppression)。它是将数据中的连续的0或空白替换为出现次数和出现位置。本文的实现参考了[[4]]，但做了如下改动：允许变长字段，方法是在每个值前面附上实际长度。

比如在压缩一组int时，作者不会每个值固定4B，而是用2b编码这个值实际长度，再将所有长度编码保存在一起以保证字节对齐。

### Dictionary Encoding

字典编码的定义参考[Wikipedia](https://en.wikipedia.org/wiki/Dictionary_coder)。它是将经常出现的模式替换为对应的更短的编码。

row-store中使用的字典编码通常只能考虑一条记录的值。本文实现的面向列存优化的字典编码则可以考虑多条记录的值。首先计算编码单个值需要的位数X（直接通过distinct数量计算），再计算多少个X位的编码可以放进1-4B中。例如一列有32个值，则X为5，所以1个编码可以放进1B中，3个放进2B，4个放进3B，6个放进4B。假设我们选择3/2，接下来要创建所有可能的3个5b编码与它们的原始值的映射。例如，1、25、31被编码为00000、00001、00010，则字典中会有一项是：

```
X000000000100010 -> 31 25 1
```

其中X代表未使用的一位。解码过程就很直接了：每读取2B就得到3个值。

注意到我们前面做了字节对齐。即使现代CPU的移位操作已经非常快了，字节对齐仍然能在解码这个CPU性能critical的场合下发挥作用（[[5]]有类似结论）。

#### Cache-Conscious Optimization

前面编码组的长度为1-4B，选择时要考虑到L2缓存大小。前面的例子中我们选择了3/2方案，则可能的字典项为32<sup>3</sup> = 32768个，因此字典大小为512KB（每项16B，对应3个原始值加上2B的编码也才14B，另外2B不清楚用在哪了）。作者的机器L2缓存为1MB，则512KB刚好是一半。

#### Parsing Into Single Values

前面的解码过程很容易降级到每次取一个值：用mask与一下就可以了：

```
(X000000000100010 & 0000000000011111) >> 0 = 00010
(X000000000100010 & 0000001111100000) >> 5 = 00001
(X000000000100010 & 0111110000000000) >> 10 = 00000
```

很多场合我们可以直接在压缩数据上取出编码值操作，将解压推迟到整个query plan最顶端。

作者实现的字典编码是定长但不保留顺序的，相比保留顺序但变长的方案要有一定性能优势。

### Run-length Encoding

RLE的定义参考[Wikipedia](https://en.wikipedia.org/wiki/Run-length_encoding)。它是将连续出现的值替换为三元组：`(value, start position, run_length)`，其中每个元素都是定长的（因此不能直接用于字符串）。

行存中RLE通常只用于压缩长的字符串，但在列存中RLE有着更广泛的应用，因为一列中经常有连续的值。

### Bit-Vector Encoding

Bit-vector encoding的定义参考[Wikipedia](https://en.wikipedia.org/wiki/Bitmap_index)。它用于cardinality比较小的场景，每个distinct的值对应一个bit-string，长度为这列所有值的数量，其中为1的位代表目标值出现，0代表没出现。如`1 1 3 2 2 3 1`可以编码为：

```
1: 1100001
2: 0001100
3: 0010010
```

这种编码在行存中被用作bit-map索引[[6]]，有很多关于进一步压缩bit-map和这种压缩对query性能影响的研究。但最近的研究指出[[7]][[8]]只在有bit-map非常稀疏（1/1000这个量级）时进一步的压缩才不会阻碍query性能。本文的bit-map只在cardinality很小时使用，此时每个bit-map会比较稠密，因此也没做进一步的压缩。

### Heavyweight Compression Schemes

LZ编码可以参考[Wikipedia](https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Welch)和[之前的博客](/2020/10/09/understanding-compression)。它有非常多的变种，是使用最广泛的通用压缩算法。

作者选择了[LZ算法的一个可以任意使用的版本](http://www.lzop.org)，针对解压速度有优化。

## References

1. [G.Graefe and L.Shapiro. Data compression and database performance. In ACM/IEEE-CS Symp. On Applied Computing pages 22 -27, April 1991.][1]
1. [Z. Chen, J. Gehrke, and F. Korn. Query optimization in compressed database systems. In SIGMOD ’01, pages 271–282, 2001.][2]
1. [J. Goldstein, R. Ramakrishnan, and U. Shaft. Compressing relations and indexes. In ICDE ’98, pages 370–379, 1998.][3]
1. [T. Westmann, D. Kossmann, S. Helmer, and G. Moerkotte. The implementation and performance of compressed databases. SIGMOD Rec., 29(3):55–67, 2000.][4]
1. [M. Zukowski, S. Heman, N. Nes, and P. Boncz. Super-scalar ram-cpu cache compression. In ICDE, 2006.][5]
1. [P. O’Neil and D. Quass. Improved query performance with variant indexes. In SIGMOD, pages 38–49, 1997.][6]
1. [K. Wu, E. Otoo, and A. Shoshani. Compressing bitmap indexes for faster search operations. In SSDBM’02, pages 99–108, 2002. LBNL-49627., 2002.][7]
1. [K. Wu, E. Otoo, A. Shoshani, and H. Nordberg. Notes on design and implementation of compressed bit vectors. Technical Report LBNL/PUB-3161, 2001.][8]

[1]: https://cs.brown.edu/courses/cs227/archives/2008/Papers/Compression/GraefeShapiro.pdf
[2]: https://dl.acm.org/doi/abs/10.1145/375663.375692
[3]: https://ieeexplore.ieee.org/abstract/document/655800/
[4]: https://dl.acm.org/doi/abs/10.1145/362084.362137
[5]: https://ieeexplore.ieee.org/abstract/document/1617427/
[6]: https://dl.acm.org/doi/abs/10.1145/253260.253268
[7]: https://ieeexplore.ieee.org/abstract/document/1029710/
[8]: https://escholarship.org/uc/item/9zz3r6k7