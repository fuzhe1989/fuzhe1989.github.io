---
title: "[C++] 带有引用类型成员的类居然默认允许移动"
date: 2021-07-24 23:58:12
tags:
    - C++
---

**TL;DR**

对于一个成员 X 是另一个成员 Y 的引用的类，C++编译器会默认为它生成移动构造函数和移动赋值函数，但这两个函数是有问题的。建议手动禁掉这种类的移动函数。

但这类问题（dangling）是 C++的根本缺陷，没有好的解法，不要想着有什么万全之策了，接受它。

<!--more-->

C++中带有引用类型成员的类不会默认生成复制构造函数和复制赋值函数，但也许是因为兼容性方面的考虑，它会默认生成移动构造函数和移动赋值函数。移动前后引用类型成员指向同一个地址。当类中一个成员 X 是另一个成员 Y 的引用时，这个默认生成的移动构造函数就有问题了：Y 在移动前后地址已经变了，X 却仍然指向前地址，产生 dangling reference。

```cpp
struct X {
 int data;
 int& ref;

 explicit X(int v) : data(v), ref(data) {}
};

void test() {
 std::vector<X> vec;
 for (int i = 0; i < 10; ++i) {
 vec.emplace_back(i);
 }
 for (const auto & x: vec) {
 std::cout << x.ref << std::endl; // boom!
 }
}
```

**建议：手动禁掉这种一个成员引用另一个成员的类的移动函数。**

如：

```cpp
struct X {
 int data;
 int& ref;

 explicit X(int v) : data(v), ref(data) {}
 X(X&&) = delete;
 X& operator=(X&&) = delete;
};
```

再展开一点。C++的内存安全性的一大隐患就是 dangling reference/pointer。我们需要保证一个对象在被指针或引用指向期间在内存中保持不动。这是一个非常强的要求：对象本身无法知道它是否正在被指向（除非使用 counter 来统计指向的数量，比如使用`std::shared_ptr`管一切，但这是极端场景）。但由程序员来保证又太难了，有太多情况下对象会被无意间移动（或干脆析构了）。

上面的建议只是封掉了一个特定场景的口子，但假如`data`不由`X`自己持有呢？`X`是没办法知道`ref`已经 dangling 了，外面的`data`也没办法知道这里有个引用在指向它。

没有银弹，但我们仍要前进。**使用 ASan 这类工具可以很大程度上发现这类问题，但前提是你要有测试。**

最后回到正题。我们是在使用 ClickHouse 的 [`WriteBufferFromOwnString`](https://github.com/ClickHouse/ClickHouse/blob/master/src/IO/WriteBufferFromString.h) 时发现的这个问题。看起来这个类没有被直接或间接（作为其它类的成员）放到`std::vector`等会移动元素的容器中，真是幸运。