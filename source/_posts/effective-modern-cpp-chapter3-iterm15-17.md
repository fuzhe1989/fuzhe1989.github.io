---
title:      "Effective Modern C++ 笔记 Chapter3 现代C++（Item 15-17)"
date:       2017-07-09 21:53:27
tags:
    - C++
    - Effective Modern C++
---

## Item15: 尽可能使用`constexpr`

C++11中最难理解的关键字可能就是`constexpr`了：修饰对象时，它就是加强版的`const`；但修饰函数时，它有着非常不同的含义。

概念上`constexpr`表示一个值不仅是`const`，而且是在编译期确定的。但修饰函数时，这个函数不一定返回`const`，也不一定能在编译期确定它的返回值。

编译期确定的值可以放到只读内存中，因此很适合于嵌入式系统。而编译期确定的整数类型的值还可以用于各种C++中要求“整数常量表达式”的地方，比如模板参数、数组长度、枚举值、对齐规格等等：

```cpp
int sz;                            // non-constexpr variable
...
constexpr auto arraySize1 = sz;    // error! sz's value not known at compilation
std::array<int, sz> data1;         // error! same problem
constexpr auto arraySize2 = 10;    // fine, 10 is a complie-time constat
std::array<int, arraySize2> data2; // fine, arraySize2 is constexpr
```

注意：这些地方没办法用`const`。

`constexpr`函数会在所有参数都是编译期常量时产生一个编译期常量，否则就会在运行期产生值，即与普通函数一样。因此，如果所有参数都是编译期常量，那么`constexpr`函数也可以用于那些要求常量表达式的地方，否则，这个函数还可以用于普通的函数调用场景。

```cpp
constexpr int pow(int base, int exp) noexcept { ... }
constexpr auto numConds = 5;
std::array<int, pow(3, numConds)> results;  // call pow at compile-time

auto base = readFromDB("base");
auto exp = readFromDB("exponent");
auto baseToExp = pow(base, exp);            // call pow at runtime
```

C++11中对`constexpr`的限制非常严，函数体中只能有一个可执行的语句：return。当然我们还可以用三元比较符`?:`和递归。

```cpp
constexpr int pow(int base, int exp) noexcept {
    return (exp == 0)? 1: base * pow(base, exp - 1);
}
```

C++14中放宽了限制，允许：

```cpp
constexpr int pow(int base, int exp) noexcept {
    auto result = 1;
    for (int i = 0; i < exp; ++i) result *= base;
    return result;
}
```

`constexpr`函数要求所有参数和返回值都是“字面值类型”。C++11中所有内置类型（除了`void`）都满足这个条件，但自定义类型也可以满足，因为构造函数和成员函数也可以是`constexpr`的：

```cpp
class Point {
public:
    constexpr Point(double xVal = 0, double yVal = 0) noexcept
        : x(xVal), y(yVal)
    {}

    constexpr double xValue() const noexcept { return x; }
    constexpr double yValue() const noexcept { return y; }
private:
    double x, y;
};

constexpr Point p1(9.4, 27,7);
constexpr Point p2(28.8, 5.3);
```

`Point`的`xValue`和`yValue`两个成员函数也是`constexpr`，即如果被调用的对象是`constexpr`对象，它们的返回值就是`constexpr`值。这样我们可以写出下面的`constexpr`函数：

```cpp
constexpr Point midpoint(const Point& p1, const Point& p2) noexcept {
    return {(p1.xValue() + p2.xValue()) / 2,
            (p1.yValue() + p2.yValue()) / 2};
}

constexpr auto mid = midpoint(p1, p2);
```

C++11中对声明为`constexpr`的成员函数有两个限制：

1. 必须是`const`函数，因此不能修改对象本身。
1. 必须返回一个字面值类型，因此不能返回`void`。

因此我们没办法为`Point`声明下面两个`constexpr`成员函数：

```cpp
class Point {
public:
    ...
    constexpr void setX(double newX) noexcept {
        x = newX;
    }
    constexpr void setY(double newY) noexcept {
        y = newY;
    }
    ...
};
```

C++14中去掉了这两个限制，上面两个函数就可以用了，还可以这么用：

```cpp
constexpr Point reflection(const Point& p) noexcept {
    Point result;
    result.setX(-p.xValue());
    result.setY(-p.yValue());
    return result;
}

constexpr Point p1(9.4, 27.7);
constexpr Point p2(28.8, 5.3);
constexpr auto mid = midpoint(p1, p2);

constexpr auto reflectedMid = reflection(mid);
```

本节的建议是尽可能地用`constexpr`，因为`constexpr`对象和函数的适用范围远比非`constexpr`的对象和函数要广。但要注意：

1. `constexpr`是函数签名的一部分，如果把`constexpr`从函数签名中去掉，可能会破坏用户代码。
1. `constexpr`函数可以把原本运行期的运算移到编译期进行，这会加快程序的运行速度，但也会影响编译时间。

## Item16: 保证`const`函数成员线程安全

假设我们有一个多项式类，它有一个计算根的成员函数：

```cpp
class Polynomial {
public:
    using RootsType = std::vector<double>;
    ...
    RootsType roots() const;
};
```

求根计算开销很大，我们可能想加个cache，不要每次都算：

```cpp
class Polynomial {
public:
    using RootsType = std::vector<double>;
    ...
    RootsType roots() const {
        if (!rootsAreValid) {
            ...
            rootsAreValid = true;
        }
        return rootVals;
    }
private:
    mutable bool rootsAreValid{false};
    mutable RootsType rootVals{};
};
```

假设有两个线程对同一个对象调用`roots`，因为这是一个`const`成员函数，通常意味着它是只读的，因此不需要有任何互斥手段。但实际上这两个线程都会去试图修改`rootsAreValid`和`rootVals`，导致未定义结果。

问题就在于`roots`声明为`const`，但又没有保证线程安全性。我们可以用C++11增加的`mutex`来实现线程安全：

```cpp
class Polynomial {
public:
    using RootsType = std::vector<double>;
    ...
    RootsType roots() const {
        std::lock_guard<std::mutex> g(m);
        if (!rootsAreValid) {
            ...
            rootsAreValid = true;
        }
        return rootVals;
    }
private:
    mutable std::mutex m;
    mutable bool rootsAreValid{false};
    mutable RootsType rootVals{};
};
```

值得注意的是，`std::mutex`是个不可移动或复制的类型，这也导致`Polynomial`也成为不可移动或复制的类型。

有些场景下`std::mutex`可能太重了，比如我们只需要一个计数器，那么用`std::atomic`就可以了：

```cpp
class Point {
public:
    ...
    double distanceFromOrigin() const noexcept {
        ++callCount;
        return std::hypot(x, y);
    }
private:
    mutable std::atomic<unsigned> callCount{0};
    double x, y;
};
```

`std::atomic`也是不可移动或复制的类型，也会导致`Point`不可移动或复制。

通常`std::atomic`开销比`std::mutex`低，你也许因此在各种场景下用`std::atomic`：

```cpp
class Widget {
public:
    ...
    int magicValue() const {
        if (cacheValid) return cachedValue;
        else {
            auto val1 = expensiveComputation1();
            auto val2 = expensiveComputation2();
            cachedValue = val1 + val2;            // marked as A
            cacheValid = true;                    // marked as B
            return cachedValue;
        }
    }
private:
    mutable std::atomic<bool> cacheValid{false};
    mutable std::atomic<int> cachedValue;
};
```

考虑下面的场景，你会发现这里用`std::atomic`是错的：

* 线程1调用`Widget::magicValue`，看到`cacheVaild`为`false`，就开始做两个很重的计算，再把它们的和赋给`cachedValue`。
* 与此同时，线程2也调用`Widget::magicValue`，也看到`cacheValid`为`false`，开始重复的计算。

为了解这个问题，你交换了A行和B行的顺序，但还是错的：线程1在做完B行，没做A行时，线程2看到`cachedValue`为`true`，就把还没有更新的`cachedValue`返回出去了！

实际上这说明：单个变量的原子修改可以用`std::atomic`，但多个变量的原子修改还是要用`std::mutex`的。

回到正题上。对于根本不考虑并发调用的类型，它的成员函数的线程安全性并不重要，因此我们不需要为它的`const`成员函数添加开销昂贵的同步机制。但这样的类型实际上是越来越少的，对于可能被并发调用的`const`成员函数，使用者往往会忽略外部的同步机制，这也是为什么我们要自己保证`const`成员函数的线程安全性。

## Item17: 理解特殊成员函数的产生机制

按C++的官方说法，“特殊成员函数”指编译器自己会生成的成员函数。C++98中有4个这样的函数：默认构造函数、析构函数、复制构造函数、赋值函数。当然，这些函数只会在需要时才生成。这些默认生成的成员函数都是`public`且`inline`的。除了派生类的析构函数外（基类的析构函数为虚函数），其它情况下这些成员函数都是非虚的。

C++11中又增加了两个特殊成员函数：移动构造函数和移动赋值函数：

```cpp
class Widget {
public:
    ...
    Widget(Widget&& rhs);             // 移动构造函数
    Widget& operator=(Widget&& rhs);  // 移动赋值函数
};
```

这两个函数的生成规则和行为与对应的复制版本非常类似：只在需要时生成，行为是逐个移动非静态成员变量，也会调用基类对应的移动函数。注意，我们实际上是发出了“移动”的请求，不代表这些成员变量真的执行了移动的操作。对于那些没有定义移动函数的类型（比如C++98中的类型），“移动”请求实际上是通过复制函数完成的。逐个移动的过程的核心是对每个非静态成员变量调用`std::move`，并在重载决议时决定是调用移动函数还是复制函数。

如果你声明了某个移动函数，编译器就不再生成另一个移动函数。这与复制函数的生成规则不太一样：两个复制函数是独立的，声明一个不会影响另一个的默认生成。这条规则的背后原因是，如果你声明了某个移动函数，就表明这个类型的移动操作不再是“逐一移动成员变量”的语义，即你不需要编译器默认生成的移动函数的语义，因此编译器也不会为你生成另一个移动函数。

进一步地，如果你声明了某个复制函数，编译器也不再生成这两个移动函数了。这条规则的背后原因与上一条类似：自定义的复制函数表示你不想要“逐一”复制的语义，那么很大概率上“逐一”移动你也不想要，那么编译器就不会为你生成移动函数。

反过来的规则也成立：如果你声明了移动函数，那么编译器就不会生成复制函数。

C++98中有所谓的“三法则”：如果你声明了复制构造函数、复制赋值函数或析构函数中的一个，你也应该定义另外两个。该原则的原因是如果你声明了其中任意一个函数，就表明你要自己管理资源，而这三个函数都会参与到资源管理中，因此如果声明就要全声明掉。STL中的每个容器类都声明了这三个函数。

三法则的一个推论就是，自定义了析构函数往往意味着逐一的复制语义并不适用于这个类，因此自定义析构函数也应该阻止编译器生成复制函数。但在C++98标准产生过程中，三法则还没有被广泛认可，因此C++98中自定义析构函数并不会影响编译器生成复制函数。C++11中为了兼容老代码，并没有改变这一条规则，但要注意的是自定义析构函数会阻止编译器生成移动函数。因此移动函数的产生规则为，编译器只在以下三条都成立时才生成默认的移动构造函数和移动赋值函数：

* 没有声明复制函数。
* 没有声明移动函数。
* 没有声明析构函数。

C++11中也将声明了复制函数或析构函数的类中自动生成的复制函数标记为“已过时”，未来某个版本中会禁止这种行为。

如果希望声明一个默认生成的特殊函数，在C++11中你可以标记其为“=default”，显式要求编译器生成一个这样的函数：

```cpp
class Widget {
public:
    ...
    ~Widget();
    Widget(const Widget&) = default;
    Widget& operator=(const Widget&) = default;
};
```

这种方法广泛应用于纯虚基类中。纯虚基类往往会声明一个虚的空析构函数，但这会阻止编译器为其生成复制和移动函数，此时就可以用“=default”来要求编译器生成这样的函数：

```cpp
class Base {
public:
    virtual ~Base() = default;
    Base(Base&&) = default;
    Base& operator=(Base&&) = default;
    Base(const Base&) = default;
    Base& operator=(const Base&) = default;
};
```

事实上，即使编译器默认生成的复制和移动函数已经足够了，你仍然可以在类中显式声明这些函数为“=default”，明确表达你的意图，且能避免无意间对编译器隐式生成的行为产生影响。一个例子：

```cpp
class StringTable {
public:
    StringTable() {}
    ...               // functions for insertion, erasure, lookup, etc.. but no copy/move/dtor funcs
private:
    std::map<int, std::string> values;
};
```

这样的类型，编译器隐式生成的复制函数、移动函数、析构函数已经足够用了。但如果有一天，你决定在这个类的构造和析构时打一条LOG：

```cpp
class StringTable {
public:
    StringTable() {
        makeLogEntry("Creating StringTable object");
    }
    ~StringTable() {
        makeLogEntry("Destroying StringTable object");
    }
private:
    std::map<int, std::string> values;
};
```

看起来很合理，但因此编译器不再为`StringTable`生成移动函数，而生成的复制函数不受影响。这样一来，原始版本中可以调用移动构造函数或移动赋值函数的地方，现在都改为调用复制构造函数和复制赋值函数。程序没有报错，但性能却在无人注意时下降了。而如果我们一开始就显式声明这些函数为“=default”，既利用上了编译器生成的函数，又不会在无意间改变程序的行为。

C++11规定了以下特殊成员函数：

* 默认构造函数，与C++98相同。
* 析构函数，基本与C++98相同，但默认为`noexcept`。
* 复制构造函数，与C++98的运行时行为相同。声明了移动函数会阻止生成复制构造函数；声明了析构函数会导致生成的复制构造函数被标记为“deprecated”。
* 复制赋值函数，与C++98的运行时行为相同，其它特性同复制构造函数。
* 移动构造函数和移动赋值函数，执行逐一移动成员的操作，只有在未声明析构函数、复制函数、移动函数时才会自动生成。

这里没有说声明一个模板成员函数会阻止编译器生成这些特殊函数，即如果`Widget`里声明了模板成员函数：

```cpp
class Widget {
    ...
    template <typename T>
    Widget(const T& rhs);

    template <typename T>
    Widget& operator=(const T& rhs);
};
```

并不会阻止编译器继续生成特殊成员函数，即使这两个模板函数在`T`为`Widget`时函数签名与自动生成的复制函数完全相同。Item26会解释为什么会有这条规则存在。

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