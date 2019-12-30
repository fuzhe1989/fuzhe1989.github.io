---
title:      "C++对象模型（一）Alignment"
date:       2018-03-07 20:31:30
tags:
    - C++
---

# word

第一个概念，word（字）。

word是cpu领域的一个重要概念，它被定义为cpu使用数据的一个自然单位（natural unit），cpu的很多数据长度都与其相关，比如：

* 通常cpu的大多数寄存器长度为一个word。
* 通常cpu最大寻址空间为一个word（即指针大小通常是一个word）。
* 总线宽度通常为一个word，即cpu单次读写内存的量通常最大为一个word。
* 很多cpu每条指令的长度也为一个word。

32位/64位cpu中的32位和64位就是指它的字长（word-length或word-size）为32位或64位。

这里不详细介绍不同cpu的word的具体含义。我们说一下word对内存读写的影响：

* 早期的cpu通常只能沿着一个word的边界读写数据，如果一次读/写操作的目标地址不是字长的整数倍，cpu会报错。
* 现代的X86 cpu可以在任意地址读写数据，但如果目标地址不是字长的整数倍，底层会将这次操作按word分界分成多个读写操作，对性能有明显影响。
* X86-64 cpu可以在任意地址读写数据，且不会有明显的性能影响。

![alignment](http://7xipsa.com1.z0.glb.clouddn.com/alignment.png)

上图的第一个例子，我们按alignment存放了1个int（4字节数据），cpu只需要一次内存操作就可以完成存取。而第二个例子中，cpu需要两次内存操作来完成int的存取。

现代的cpu通常有多个字长概念（word、1/2word、1/4word等），针对不同的数据长度，可以有不同的字长。X86和X86-64对长度为1/2/4/8字节的数据，其字长也为1/2/4/8字节。

# alignment和padding

第二个概念，alignment。

考虑到数据不按word边界存放可能引起的问题，编译器在排列变量时，会尽量将其按对应的字长来排列。这种行为就被称为对齐（alignment），而因为alignment导致的数据间产生未使用的空洞，则被称为填充（padding）。

```c
void func() {
    char c;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    printf("c:%ld i16:%ld i32:%ld i64:%ld\n", &c, &i16, &i32, &i64);
}
```

在我的环境下（X86-64，gcc4.8.5），输出为：
```
c:140730763468703 i16:140730763468700 i32:140730763468696 i64:140730763468688
```

可以看到`i16`与`c`之间有2字节的padding，`i32`与`i16`之间有2字节的padding，`i64`与`i32`之间有4字节的padding，实际上是这么排列的：

```c
    char c;
    char padding0[2];
    int16_t i16;
    char padding1[2];
    int32_t i32;
    char padding2[4];
    int64_t i64;
```

# struct的alignment

struct的alignment规则很简单：

* 空struct的size与alignment均为1。
* 非空的struct，其alignment为各成员的alignment的最大值。其最后一个成员后面若有需要，也要padding。

注意第2条规则，会导致struct占用的空间比我们预期的更多，例子：

```c
struct S {
    char c0;
    int64_t i64;
    char c1;
};
```

它的自然大小为10B（1+8+1），但考虑到alignment的影响，真实大小却是24B（8+8+8）！

因此`S`的真实布局为：

```c
struct S {
    char c0;
    char padding0[7]；
    int64_t i64;
    char c1;
    char padding1[7];
};
```

而当`S`作为其它struct的成员时，它的size和alignment分别是24和8，会影响到上层struct的alignment。

适当的重新排列`S`的成员，可以显著减小它的size：

```c
struct S {
    char c0;
    char c1;
    int64_t i64;
};
```

此时它的真实布局为：

```c
struct S {
    char c0;
    char c1;
    char padding[6];
    int64_t i64;
};
```

`S`的size减小到了16B。

# 与alignment有关的编译器扩展

这里只介绍gcc的相关扩展。

gcc允许我们用`__attribute__`来修饰变量，其中用于改变alignment的有以下几种。

注意：修改alignment可能会影响ABI兼容性和可移植性，通常不推荐。

## aligned

语法1：`__attribute__ ((aligned (size_in_byte)))`，显式指定alignment。注意：指定比默认更小的alignment是无效的，会被编译器忽略。

```c
int x __attribute__ ((aligned (16))) = 0;

struct S {
    int x[2] __attribute__ ((aligned (8)));
};

struct R {
    char c;
} __attribute__ ((aligned (8)));
```

语法2：`__attribute__ ((aligned))`，让编译器选择可能的最大alignment，对64位环境而言通常是8。

```c
struct R {
    char c;
} __attribute__ ((aligned));
```

## packed

语法：`__attribute__ ((packed))`，表示该变量或struct选择可能的最小alignment，对64位环境而言通常是1。

下面这个struct，加上`packed`后其大小变为10，与其自然大小相等：

```c
struct S {
    char c0;
    int64_t i64;
    char c1;
};
```

## 编译选项

编译时如果加上`-fpack-struct`，则默认所有变量和struct都会按`packed`处理。

如果加上`-Wpadded`，则编译器增加padding的地方会有warning。注意只包含struct场景。

# 相关链接

* [Alignment in C](https://wr.informatik.uni-hamburg.de/_media/teaching/wintersemester_2013_2014/epc-14-haase-svenhendrik-alignmentinc-paper.pdf)
* [Wiki - Word](https://en.wikipedia.org/wiki/Word_(computer_architecture))
* [Specifying Attributes of Variables](https://gcc.gnu.org/onlinedocs/gcc-3.2/gcc/Variable-Attributes.html)
