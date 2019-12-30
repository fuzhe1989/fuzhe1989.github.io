---
title:      "C++对象模型（六）复制与移动"
date:       2018-03-25 00:02:52
tags:
    - C++
---

C++里类有4种特殊的成员函数：
* 构造函数。
* 析构函数。
* 复制函数，包括复制构造函数和复制赋值函数。
* 移动函数，包括移动构造函数和移动赋值函数。

这些函数的特点是：有些时候，编译器会帮你生成这些函数；有些时候，编译器又会拒绝生成这些函数；还有些时候，编译器还会往你自己写的特殊函数中添加操作。鉴于这些特殊函数的重要性，我们有必要好好了解一下它们背后的故事。

本文介绍的是后两类，复制函数和移动函数。

注1：本文环境为Ubuntu 16.04，gcc5.4.0，使用c++14标准。
注2：本文大量内容来自[《深入探索C++对象模型》](https://book.douban.com/subject/10427315/)。

# 复制函数

## 复制函数的签名

通常我们说到复制函数，是指：
* 复制构造函数：`S(const S& ano)`。
* 复制赋值函数：`S& operator=(const S&)`。

其中后者返回`S&`是为了能模仿C的连续赋值：`a = b = c`。

复制构造函数还可以是`S(S& ano)`，但这样会给人一种可能修改源对象的错觉。

复制赋值函数可以有很多签名，比如返回值改为`const S&`或者干脆是`void`，但这样的赋值函数可能不会有太多用处。如果把它的参数类型改为`S`或`const S`，这也是复制赋值函数。如果参数类型为其它类型，则不再满足复制赋值函数的要求。

## 什么时候发生复制

如下三个场景都会调用到复制构造函数：

```cpp
// case 0
S s1(s0);
// case 1
S s2 = s0;
// case 2
void Func(S s3);
Func(s0);
```

实际上，这也是构造函数会被调用的三个场景，只是它的参数恰好与类相同。

而当我们对一个已定义的对象进行赋值时，如果`=`右边的表达式类型与类相同（去掉cv与左值引用后），那么就会调用复制赋值函数。

```cpp
s1 = s0;
s2 = GetTempS();
```

## 编译器生复制函数的条件

当一个类满足以下条件时，编译器就会为其生成复制构造函数或复制赋值函数：
* 没有自定义的复制函数或声明复制函数为`= default`，且
* 没有用`= delete`删除复制函数，且
* 所有非静态成员变量都可复制，且
* 代码中调用了复制函数。

其中，如果类定义了一个复制函数而没有定义另一个，且满足上述条件，那么编译器也会为其生成另一个复制函数。

根据上面的要求，如果一个类有const成员或引用类型的成员，那么这个类显然没办法复制了。

然而我们又要重复说bitwise与memberwise了。

如果编译器在为一个类生成复制函数时，发现这个类满足bitwise标准，只需要trivial的复制函数，那么就不会真的生成这个函数。前文中我们说，对于满足bitwise构造的类型来说，不建议放任编译器生成trivial的默认构造函数，原因是trivial的默认构造函数会导致成员变量没有确定的初值。但与默认构造函数不同，bitwise的复制函数的确定性取决于它的参数值，因此一个类型如果复制函数是trivial的，但通过自定义默认构造函数的方式保证了不会有trivial的默认构造函数，那么trivial的复制函数就是安全的。

而如果类不满足bitwise条件，编译器就会为其生成memberwise的复制函数，即按声明顺序，依次调用每个非静态成员变量的相应复制函数。

## 复制与析构的“三法则”

以下内容来自[Effective Modern C++ Item17](/2017/07/09/effective-modern-cpp-chapter3-iterm15-17/)。

> C++98中有所谓的“三法则”：如果你声明了复制构造函数、复制赋值函数或析构函数中的一个，你也应该定义另外两个。该原则的原因是如果你声明了其中任意一个函数，就表明你要自己管理资源，而这三个函数都会参与到资源管理中，因此如果声明就要全声明掉。STL中的每个容器类都声明了这三个函数。
>
> 三法则的一个推论就是，自定义了析构函数往往意味着逐一的复制语义并不适用于这个类，因此自定义析构函数也应该阻止编译器生成复制函数。但在C++98标准产生过程中，三法则还没有被广泛认可，因此C++98中自定义析构函数并不会影响编译器生成复制函数。

## 设置虚表指针

当发生派生类到基类对象的复制时，很重要的事情就是保证基类对象的虚表指针指向正确的虚表。对于`Base b = Derived()`这种情况，我们知道其中发生Slicing，即只有`Derived`对象中的基类子对象复制给了`b`。但其中编译器还会正确地设置虚表指针，保证这样构造出来的`b`的虚表指针指向的是`Base`的虚表，而不是`Derived`的虚表。

如果我们自定义了复制函数，编译器会插入相应的代码，保证复制过程中虚表指针被正确设置了。

## 正确复制基类子对象

我们在实现派生类的复制函数时，通常会比较注意是不是有成员忘了处理。但除了派生类本身的成员变量外，有时候我们可能会忘了复制基类子对象：

```cpp
struct Base {
    Base(): x(0) {}
    Base(const Base& b): x(b.x) {}
    Base& operator=(const Base& b) {
        x = b.x;
    }
    int x;
};

struct Derived: public Base {
    Derived(): y(1) {}
    Derived(const Derived& d): y(d.y) {}
    Derived& operator=(const Derived& d) {
        y = d.y;
    }
    int y;
};

int main() {
    Derived d0;
    d0.x = 2;
    d0.y = 2;
    Derived d1 = d0;
    printf("%d %d\n", d1.x, d1.y);
    d1.x = 3;
    d1 = d0;
    printf("%d %d\n", d1.x, d1.y);
}
```

```
0 2
3 2
```

这个例子中`Derived`的两个复制函数都错了。

复制构造函数中，没有显式调用基类的复制构造函数，导致基类子对象被默认构造，丢失了`d0.x`。而复制赋值函数中，忘记调用了基类的复制赋值函数，同样导致`d1.x`没有被修改。

此时似乎编译器应该介入，但回想一下C++的设计理念，充分信任程序员，如果作者真的就预期这种行为呢？所以编译器不会帮我们把基类复制好。

因此一定要记住，在派生类的复制构造函数中，成员初始化列表的第一项就要是基类：

```cpp
Derived::Derived(const Derived& d): Base(d), y(d.y) {}
```

而在复制赋值函数中，没有初始化列表，我们就在第一行显式调用基类的复制赋值函数：

```cpp
Derived& Derived::operator=(const Derived& d) {
    Base::operator=(d);
    y = d.y;
}
```

说实话这么写代码有点丑，但我们还是要认清现实，毕竟保证正确性更重要。

## 判断是否为自身

本节与下节来自[《Effective C++》](https://book.douban.com/subject/5387403/)的Item11（“Handle assignment to self in operator=”）与Item25（“Consider support for a non-throwing swap”）。

当复制赋值函数被调用时，如果不判断源与目标是否为相同对象，可能会导致资源泄漏甚至进程崩溃。比如有这么个类：

```cpp
class Widget {
    ...
private:
    Bitmap* pb;
};

Widget& Widget::operator=(const Widget& rhs) {
    delete pb;
    pb = new Bitmap(*rhs.pb);
    return *this;
}
```

如果这里`rhs`与`*this`是相同对象：
* pb被删除，析构了，之后pb变成了空悬指针。
* 解引用pb导致未定义行为。

因此传统做法是在赋值函数中先判断一下是否为自身：

```cpp
Widget& Widget::operator=(const Widget& rhs) {
    if (this != &rhs) {
        delete pb;
        pb = new Bitmap(*rhs.pb);
    }
    return *this;
}
```

然而这个函数还是有问题：它不是异常安全的，如果`new Bitmap`抛了异常，被赋值的对象的pb已经析构，没办法恢复了。怎么解决呢？看下节。

## 使用swap实现异常安全的复制

对于上节提到的`Widget`赋值的异常安全问题，传统做法是先复制，再赋值：

```cpp
Widget& Widget::operator=(const Widget& rhs) {
    if (this != &rhs) {
        Bitmap* pOrig = pb;
        pb = new Bitmap(*rhs.pb);
        delet pOrig;
    }
    return *this;
}
```

然而更优雅的方式为`Widget`增加一个`swap`函数，并直接利用复制构造函数：

```cpp
Widget& Widget::operator=(const Widget& rhs) {
    if (this != &rhs) {
        Widget tmp(rhs);
        swap(tmp);
    }
    return *this;
}
```

这种方式将复制相关的逻辑都集中在了一个地方，复制构造函数中，更不容易弄错，代码也更整洁。

接下来是一个小要求：保证`swap`不抛异常。各个类型的`swap`很多时候都是用来实现强的异常安全的，因此会有`swap`不抛异常的假设。如果打破了这个假设，当我们用一些第三方库时，可能会遇到麻烦。一般来说，如果我们用指针或智能指针或STL容器来管理资源，还是很容易保证`swap`不抛异常的。

如果我们能假设`Widget`不会自身赋值，那么还有一种写法，可以更简洁地实现赋值：

```cpp
Widget& Widget::operator=(Widget rhs) {
    swap(rhs);
    return *this;
}
```

这里我们利用了函数参数，省掉了一个临时变量，且因为`rhs`现在是一个local变量，不可能与`*this`相同，我们也不需要判断是否是自身，函数实现只剩下了两行代码。

## 禁止复制

有些类型并不适合被复制，比如一些用于RAII的类型。为了安全，我们需要禁止这些类型的复制。

C++98中常用的做法是声明private的复制函数但不实现：

```cpp
class Lock {
    ...
private:
    Lock(const Lock&);
    Lock& operator=(const Lock&);
};
```

这样类的外部（非友元）因访问权限问题而无法调用到复制函数，类的内部和友元则因复制函数没有定义，在链接时会报错。

在C++11中我们可以声明这些函数为deleted，从而更优雅更明显地禁止复制：

```cpp
class Lock {
public:
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    ...
}
```

更多内容参见[Effective Modern C++ Item11](/2017/07/05/effective-modern-cpp-chapter3-iterm11-14/)。

## 赋值函数可以是虚函数吗

我们知道构造函数不可以是虚函数，那么赋值函数可以是虚函数吗？比如：

```cpp
struct Base {
    virtual ~Base() {}
    virtual Base& operator=(const Base& b);
};

struct Derived: public Base {
    virtual Derived& operator=(const Derived& d);
}
```

这样下面的调用就有多态的效果了：

```cpp
int main() {
    Base* p0 = new Derived();
    Base* p1 = new Derived();
    *p0 = *p1;
}
```

然而很遗憾，错了，没有多态。编译器在判断派生类有没有改写基类的虚函数时，会判断返回值类型是否兼容，但不会判断参数类型是否兼容，因此实际上`Derived`中有两个赋值函数：

```cpp
struct Derived: public Base {
    virtual Derived& operator=(const Base&);
    virtual Derived& operator=(const Derived&);
}
```

其中第一个赋值函数继承了基类的实现。因此`*p0 = *p1`实际调用的还是基类的赋值函数，只复制了基类子对象部分。

怎么改正确？为了正确赋值派生类自己的成员，我们需要用到RTTI：

```cpp
Derived& Derived::operator=(const Base& b) {
    Base::operator=(b);
    const Derived* p = dynamic_cast<const Derived*>(&b);
    if (p) {
        // 赋值派生类的成员
    }
    return *this;
}
```

之后我们该怎么处理`Derived`“正常”的那个赋值函数呢？就是参数为`const Derived&`那个。

我们可以同样实现它，但这样`Derived`中就需要实现两个赋值函数，而派生自`Derived`的类型则需要实现三个，等等，太恐怖了。另一种做法是干脆不要这个“正常”的赋值函数，这样所有赋值都会走基类定义的那个虚函数。这样我们只需要实现一个赋值函数，但缺点是所有赋值都要用到RTTI，开销比较大。

这两种方法都不太好，看起来它们指向一个结论：赋值函数不应该是虚函数。通常我们认为重载的操作符函数都不适合作为虚函数。

那么如果我们真的要实现一种多态的复制操作，该怎么做？一种常见做法是基类定义一个虚的`Clone`函数：

```cpp
struct Base {
    ...
    Base* Clone() const = 0;
};
```

这样各个子类只要实现自己的`Clone`函数，就可以完成多态的复制了。

## 参考

* [virtual assignment operator C++](https://stackoverflow.com/questions/669818/virtual-assignment-operator-c)
* [The Assignment Operator Revisited](http://icu-project.org/docs/papers/cpp_report/the_assignment_operator_revisited.html)
* [The Anatomy of the Assignment Operator](http://icu-project.org/docs/papers/cpp_report/the_anatomy_of_the_assignment_operator.html)

# 移动函数

移动函数可以参考以下文章：

* [Effective Modern C++ Item17: 理解特殊成员函数的产生机制](/2017/07/09/effective-modern-cpp-chapter3-iterm15-17/)
* [Effective Modern C++ Chapter5 右值引用、移动语义、完美转发（Item 23-26)](/2017/08/08/effective-modern-cpp-chapter5-iterm23-26/)
* [Effective Modern C++ Chapter5 右值引用、移动语义、完美转发（Item 27-30)](/2017/08/22/effective-modern-cpp-chapter5-iterm27-30/)
* [关于函数的类型、完美转发、普适引用与引用折叠的一些尝试和解释](/2017/08/24/cpp-function-type-perfect-forward-universal-reference-collapse/)

本文就不再重复了。

# 返回值优化（RVO）

所谓RVO就是Return Value Optimization，是一种编译器优化，即当编译器返回一个local变量时，如果接收返回值的是一个相同类型的新对象（即构造，而不是赋值），编译器可能会省掉这次构造，就在这个返回值的内存位置构造这个local变量：

```cpp
vector<string> GetNameList(int n) {
    vector<string> l;
    for (int i = 0; i < n; ++i) {
        l.push_back(GetRandomName());
    }
    return l;
}

vector<string> nameList = GetNameList(100);
```

如果没有RVO，这个例子中会有两次`vector<string>`的构造，一次是默认构造，一次是复制构造。但在有RVO时，`GetNameList`中的`l`实际就是`nameList`，编译器直接用了返回值的地址来构造`l`，这样最后就不需要真正“返回”一个值了，省掉了100个`string`的复制。

这里能体现RVO的几个要求：
* 返回local变量的值，而不是指针、引用、或是local变量的成员。
* 接收变量要是新对象，不能是已有的对象，即这个表达式是用来构造它的，而不是给它赋值的。
* 接收变量的类型要与local变量的类型**完全**相同。

C++98中RVO属于编译器自己的一种优化，我们不能预期编译器真的会执行优化。因此上面的写法通常不被推荐，我们更习惯这么写：

```cpp
void GetNameList(int n, vector<string>* nameList);
```

这能保证省掉一次复制构造。但在C++11中，标准规定了满足上述要求后，编译器**必须**使用RVO，RVO成了可预期的行为，那么我们就可以放心使用前面的写法了，毕竟它更干净。

有些人会为了省掉一次复制构造，而选择返回local变量的右值引用：

```cpp
vector<string> GetNameList(int n) {
    vector<string> l;
    // ...
    return std::move(l);
}
```

引用下面链接中黄尼玛的回答：

> 此时返回的并不是一个局部对象，而是局部对象的右值引用。编译器此时无法进行rvo优化，能做的只有根据std::move(w)来移动构造一个临时对象，然后再将该临时对象赋值到最后的目标。所以，不要试图去返回一个局部对象的右值引用。

引用[Effective Modern C++ Item25](/2017/08/08/effective-modern-cpp-chapter5-iterm23-26/)：

> 如果函数的返回类型就是值类型，那么编译器可以直接将这个local对象构造在接收函数返回值的对象上，省掉中间的复制过程。换句话说，在RVO的帮助下，直接返回这个local对象要比返回它的右值还要节省。
> 
> 既然直接返回local对象不会比手动调用`std::move`差，还有很大概率更好一些，我们还有什么理由去手动move呢？

## 参考

* [什么时候应当依靠返回值优化](https://www.zhihu.com/question/27000013)
