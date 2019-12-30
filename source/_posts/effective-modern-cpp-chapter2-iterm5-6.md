---
title:      "Effective Modern C++ 笔记 Chapter2 auto (Item 5-6)"
date:       2017-05-22 22:05:59
tags:
    - C++
    - Effective Modern C++
---

`auto`能大大简化我们的代码，但用不好也会带来正确性和性能上的问题。本章覆盖了`auto`的方方面面，帮助我们避开陷阱，高高兴兴的把手动的类型声明替换成`auto`。

## Item5: 优先选用`auto`而不是显式类型声明

### 优点1: 避免忘记初始化

```cpp
int x1;      // potentially uninitialized
auto x2;     // error! initializer required
auto x3 = 0; // fine, x3 is well-defined
```

### 优点2: 方便声明冗长的，或只有编译器知道的类型

```cpp
template <typename It>
void dwim(It b, It e) {
    for (; b != e; ++b) {
        typename std::iterator_traits<It>::value_type currValue = *b;
        // or
        auto currValue = *b;
    }
}
```

以及：

```cpp
auto derefUPLess = 
    [](const std::unique_ptr<Widget>& p1,
       const std::unique_ptr<Widget>& p2)
    { return *p1 < *p2; }
```

C++14中我们还可以写成：

```cpp
auto derefLess = [](const auto& p1, const auto& p2) { return *p1 < *p2; }
```

这个例子中，我们根本没办法知道derefLess的类型了，但编译器知道，我们也就能通过`auto`拿到这个类型了。

### 优点3: 对函数来说，`auto`比`std::function`体积更小，速度更快

上例中`derefUPLess`的类型应该`bool(const std::unique_ptr<Widget>&, const std::unique_ptr<Widget>&)`，但如果要手动声明的话，我们要用`std::function`：

```cpp
std::function<bool(const std::unique_ptr<Widget>&, const std::unique_ptr<Widget>&)> 
derefUPLess =
    [](const std::unique_ptr<Widget>& p1,
       const std::unique_ptr<Widget>& p2)
    { return *p1 < *p2; }
```

一个重要的事实是`std::function`与`auto`是不同的。

`std::function`里面要hold我们传入的closure。相同函数签名的不同closure可能捕获了不同的元素，因此它们需要的体积也不同，但一个函数签名对应着一个确定的`std::function`特化类型，这个类型的体积是固定的，这说明`std::function`内部可能会根据不同的closure分配不同大小的堆上内存。这个机制还会影响函数的inline。

结果就是`std::function`几乎一定比`auto`体积大，调用慢，还可能会抛`out-of-memory`的异常。

而且`auto`还能比`std::function`少写很长一段代码。

### 优点4: 声明类型更准确

```cpp
std::vector<int> v;
unsigned sz = v.size();
```

`v.size()`实际返回的是`std::vector<int>::size_type`，它是一个无符号整数类型，因此很多人习惯声明为`unsigned`，但这是不准确的。

32位环境下`unsigned`和`std::vector<int>::size_type`都是`uint32_t`，没问题。但64位环境下，前者还是32位的，后者却是64位的。

而用`auto sz = v.size()`就能避免这个问题。

另一个例子：

```cpp
std::unordered_map<std::string, int> m;
...
for (const std::pair<std::string, int>& p: m) {
    ...
}
```

上面的代码有一个大问题：`std::unordered_map`的key是`const`的，因此`p`的类型应该声明为`const std::pair<const std::string, int>&`。

问题还没结束，编译器会努力的为`p`找到一个从`std::pair<const std::string, int>`到`std::pair<std::string, int>`的转换，而这样的转换是存在的：生成一个`std::pair<std::string, int>`的临时对象。

结果就是每次循环都会生成一个临时对象。

而用`auto`就没有这个问题了：

```cpp
for (const auto& p: m) {
    ...
}
```

用`auto`不光是效率上的问题，还有正确性的问题：如果我们取`p`的地址，我们能百分百确定它是在`m`中，而不用`auto`，我们可能取到一个临时对象的地址。

上面两个例子说明，合理的使用`auto`有助于写出**显然正确**的代码，而不需要我们小心翼翼的确定要声明的类型。

### 如何取舍`auto`与代码的可读性

`auto`只是一种选项，不是强制要求，如果显式声明类型能让代码更干净，更好维护，就继续用显式类型声明。

但根据其它语言中的经验，自动类型推断并没有阻碍我们对大型的工业级代码库的开发和维护。

有人担心`auto`略去了类型，会影响我们对代码的理解，但很多时间一个好的名字能解决这个问题，比如知道这个变量是容器、计数器，还是一个智能指针。

显式写出类型，往往只能引入微妙的问题，而没有提供很多信息。使用`auto`还能帮助我们做重构。比如一个变量的初始类型是`int`，有一天你想换成`long`，那么用了`auto`的地方自动就变掉了，但用了`int`去声明的地方则要你一个一个的找出来。

## Item6: 在`auto`推断非预期时显式声明类型

一些场景下表达式的类型与我们想要的类型并不一致，我们依赖于隐式类型转换才能得到想要的类型。这个时候我们需要显式声明类型，如果用`auto`就会得到非预期的类型。

一种常见场景是表达式返回一个代理类型，比如`std::vector<bool>::operator[]`返回`std::vector<bool>::reference`，而不是我们预期的`bool`。类的设计者预期我们会把返回值的类型声明为`bool`，再通过`reference::operator bool()`来做隐式转换。

```cpp
std::vector<bool> features();
...
bool highPriority = features()[5];  // reference -> bool, not bool&
...
processWidget(w, highPriority);
```

而如果声明变量为`auto`，那么变量的类型就是`std::vector<bool>::reference`！

```cpp
std::vector<bool> features();
...
auto highPriority = features()[5];  // std::vector<bool>::reference
...
processWidget(w, highPriority);     // undefined behavior
```

更严重的是，`features()`返回了一个临时的`std::vector<bool>`对象，而`highPriority`中包含一个指向这个临时对象的指针，在这行结束时，这个临时对象就会析构，`highPriority`中的指针就变成了空悬指针，`processWidget`的调用就会成为未定义行为。

很多C++库都用到了一种叫做“表达式模板”的技术，也会导致上面的问题。

一个例子：

```cpp
Matrix sum = m1 + m2 + m3 + m4;
```

通常来说这会产生3个临时的`Matrix`对象：每次`operator+`产生1个。如果我们定义一个代理类作为`Matrix::operator+`的返回值，这个类只会持有`Matrix`的引用，不做实际的运算，直到调用`=`时再去生成最终的`Matrix`，就能避免这几个临时对象的产生。

这个例子中我们也没办法直接声明`auto sum = ...`。

怎么避免出现`auto var = expression of "invisible" proxy class type;`这种情况呢？

1. 看文档，一般设计成这样的类会有特殊说明；
2. 看头文件，看具体调用的返回值类型是不是符合预期；
3. 用`static_cast`，保证返回值的类型符合预期。

`static_cast`的例子：

```cpp
auto highPriority = static_cast<bool>(features()[5]);
auto sum = static_cast<Matrix>(m1 + m2 + m3 + m4);
```

一些依赖于基础类型的隐式转换的场景也可以用`static_cast`：

```cpp
double calcEpsilon();
float ep = calcEpsilon();   // implicitly convert double -> float
auto ep = static_cast<float>(calcEpsilon());
```

（我觉得`static_cast`只适合用于“确定了用`auto`”的场景，否则还是显式声明类型好一些）

## 目录

* [Chapter1 类型推断 (Item 1-4)](/2017/05/14/effective-modern-cpp-chapter1-iterm1-4/)
* [Chapter2 auto (Item 5-6)](/2017/05/22/effective-modern-cpp-chapter2-iterm5-6/)
* [Chapter3 现代C++（Item 7-10)](/2017/05/22/effective-modern-cpp-chapter3-iterm7-10/)
* [Chapter3 现代C++（Item 11-14)](/2017/05/22/effective-modern-cpp-chapter3-iterm11-14/)
* [Chapter3 现代C++（Item 15-17)](/2017/07/09/effective-modern-cpp-chapter3-iterm15-17/)
* [Chapter4 智能指针 (Item 18-22)](/2017/07/27/effective-modern-cpp-chapter4-iterm18-22/)
* [Chapter5 右值引用、移动语义、完美转发（Item 23-26)](/2017/08/08/effective-modern-cpp-chapter5-iterm23-26/)
* [Chapter5 右值引用、移动语义、完美转发（Item 27-30)](/2017/08/22/effective-modern-cpp-chapter5-iterm27-30/)
* [Chapter6: Lamba表达式 (Item 31-34)](/2017/09/06/effective-modern-cpp-chapter6-iterm31-34/)
* [Chapter7: 并发API (Item 35-37)](/2017/09/24/effective-modern-cpp-chapter7-iterm35-37/)
* [Chapter7: 并发API (Item 38-40)](/2017/10/09/effective-modern-cpp-chapter7-iterm38-40/)
* [Chapter8: 杂项 (Item 41-42)](/2017/10/26/effective-modern-cpp-chapter8-iterm41-42/)