---
title:      "Effective Modern C++ 笔记 Chapter5 右值引用、移动语义、完美转发（Item 27-30)"
date:       2017-08-22 23:51:00
tags:
    - C++
    - Effective Modern C++
---

## Item27: 熟悉重载普适引用的替代方法

### 放弃重载

对于Item26的第一个例子`logAndAdd`，一种做法是放弃重载，直接用两个不同的名字，比如`logAndAddName`和`logAndAddNameIdx`。当然这解不了Item26的第二个例子，即`Person`的构造函数：你总不能改构造函数的名字。

### 通过const T&传递

另一种做法是回到C++98，传递`const T&`，也意味着放弃了完美转发。这种方法在效率上是有损失的，但在完美转发和重载之间有矛盾时，损失一些效率来让设计变简单也许更有吸引力一些。

### 通过值传递

一种不损失效率，又不增加设计复杂度的方法是，直接传值，不传引用。Item41介绍了采用此建议的一种设计。这里我们只是简单看下`Person`类可以怎么实现：

```cpp
class Person {
public:
    explicit Person(std::string n)
    : name(std::move(n)) {}

    explicit Person(int idx)
    : name(nameFromIdx(idx)) {}
    ...
private:
    std::string name;
};
```

（没有效率损失的原因：如果实参是左值，那么实参到形参是一次复制，形参到`name`是一次移动，相比普适引用只多了一次移动；如果实参是右值，那么实参到形参是一次移动，形参到`name`还是一次移动，相比普适引用还是只多一次移动，可以认为没有效率损失。）

唯一要注意的是，如果实参是`0`或`NULL`，会匹配到`int`版本，原因见Item8。

### 使用标签分发(Tag dispatch)

普适引用的问题是，在重载决议中，它几乎总是完美匹配的。我们知道重载决议是在所有参数上发生的，那么如果我们人为的增加一个Tag参数，用Tag参数来匹配，就能避免普适引用带来的问题。

首先是原始版本：

```cpp
std::multiset<std::string> names;
template <typename T>
void logAndAdd(T&& name) {
    auto now = std::chrono::system_clock::now();
    log(now, "logAndAdd");
    names.emplace(std::forward<T>(name));
}
```

然后是一个接近正确的版本：

```cpp
template <typename T>
void logAndAdd(T&& name) {
    logAndAddImpl(std::forward<T>(name), std::is_integral<T>());
}
```

这里的问题在于，当实参是左值时，`T`会被推导为左值引用，即如果实参类型是`int`，那么`T`就是`int&`，`std::is_integral<T>()`就会返回false。这里我们需要把`T`可能的引用性去掉：

```cpp
template <typename T>
void logAndAdd(T&& name) {
    logAndAddImpl(
        std::forward<T>(name),
        std::is_integral<typename std::remove_reference<T>::type>()
    );
}
```

然后`logAndAddImpl`提供两个特化版本：

```cpp
template <typename T>
void logAndAddImpl(T&& name, std::false_type) {
    auto now = std::chrono::system_clock::now();
    log(now, "logAndAdd");
    names.emplace(std::forward<T>(name));
}

std::string nameFromIdx(int idx);
template <typename T>
void logAndAddImpl(T&& name, std::true_type) {
    logAndAdd(nameFromIdx(idx));
}
```

为什么用`std::true_type`/`std::false_type`而不用`true/false`？前者是编译期值，后者是运行时值。

注意这里我们都没有给`logAndAddImpl`的第二个参数起名字，说明它就是一个Tag。这种方法常用于模板元编程。

重要的是Tag dispatch如何把普适引用和重载结合起来了：通过一个新增的Tag参数，改变原本的重载决议顺序。

### 限制模板使用普适引用

Tag dispatch的主旨就是存在一个不重载的函数作为入口，它会加上一个Tag参数，再分发给实现函数。但这种方法也没办法解决Item26中`Person`的构造函数遇到的问题。编译器会自动为类生成复制和移动构造函数，因此你没办法完全控制入口。

注意这里：不是说有时候编译器生成的函数会**绕过**你的Tag dispatch，而是说它们没有**保证**经过Tag dispatch。这里你需要的是`std::enable_if`。

`std::enable_if`可以让一个模板只在条件满足时存在。在`Person`的例子中，我们希望当传入的参数类型不为`Person`时完美转发构造函数才存在。例子（注意语法）：

```cpp
class Person {
public:
    template <typename T,
              typename = typename std::enable_if<condition>::type>
    explicit Person(T&& n);
};
```

`std::enable_if`只影响模板函数的声明，不影响它的实现。这里我们不深究`std::enable_if`的细节，只要知道它应用了C++的"SFINAE"特性。

我们要的条件是`T`不是`Person`，可以用`!std::is_same<Person, T>::value`。但这还不够准确，因为由左值初始化而来的普适引用，它的类型会被推断为左值引用，即`T&`（参见Item28），而`T&`与`T`是不同的类型。

事实上我们在比较时需要去掉：

1. 引用：`Person`、`Person&`、`Person&&`都要被认为是`Person`。
1. `const`或`volatile`：`const Person`、`volatile Person`、`const volatile Person`都要被认为是`Person`。

标准库中对应的工具是`std::decay`，它会把对象身上的引用和cv特性都去掉。它在处理数组和函数类型时会把它们转为指针类型（参见Item1）。

最终结果：

```cpp
class Person {
public:
    template <
        typename T,
        typename = typename std::enable_if<
            !std::is_same<
                Person,
                typename std::decay<T>::type
            >::value>
        >::type
    >
    explicit Person(T&& n);
    ...
};
```

对于`Person`的构造函数，上面的版本已经能解决了：在传入的参数类型为`Person`时调用我们希望的复制和移动构造函数，而在其它时候调用完美转发函数。

Item26的最后一个例子是`Person`的派生类`SpecialPerson`：

```cpp
class SpecialPerson: public Persion {
public:
    SpecialPerson(const SpecialPerson& rhs)  // copy ctor: calls Person forwarding ctor!
    : Person(rhs)
    {...}

    SpecialPerson(SpecialPerson&& rhs)       // move ctor: calls Person forwarding ctor!
    : Person(std::move(rhs))
    {...}
};
```

看起来还没解决，原因是`std::is_same<Person, SpecialPerson>::value`是`false`。我们需要的是`std::is_base_of`。注意当`T`是自定义类型时，`std::is_base_of<T, T>::value`返回`true`，而如果`T`是内置类型，则返回`false`。所以我们需要做的就是把上面版本中的`std::is_same`替换为`std::is_base_of`：

```cpp
class Person {
public:
    template <
        typename T,
        typename = typename std::enable_if<
            !std::is_base_of<
                Person,
                typename std::decay<T>::type
            >::value>
        >::type
    >
    explicit Person(T&& n);
    ...
};
```

C++14中代码可以省一点：

```cpp
class Person {
public:
    template <
        typename T,
        typename = std::enable_if_t<
            !std::is_base_of<Person, std::decay_t<T>>::value>
        >
    >
    explicit Person(T&& n);
    ...
};
```

还没有结束，最后一个问题：如何区分整数类型和非整数类型。直接看最终版本：

```cpp
class Person {
public:
    template <
        typename T,
        typename = std::enable_if_t<
            !std::is_base_of<Person, std::decay_t<T>>::value> &&
            !std::is_integral<std::remove_reference_t<T>>::value
        >
    >
    explicit Person(T&& n)
    : name(std::forward<T>(n))
    {...}
    
    explicit Person(int idx)
    : name(nameFromIdx(idx))
    {...}
    ...
private:
    std::string name;
};
```

### 取舍

本节的前3种方法舍弃了重载普适引用的念头，后2种方法则另辟蹊径在重载函数中使用普适引用。这里需要一个取舍。

使用普适引用，从而使用完美转发，效率上更好。但它的缺点是：

1. 有些参数类型无法完美转发，参见Item30。
2. 如果传入参数不正确，错误信息不好理解。

对于缺点2，我们举个例子。假设我们给`Person`的构造参数传入一个`char16_t`构成的字符串：

```cpp
Person p(u"Konrad Zuse");
```

如果用前3种方法，编译器会报错说"no conversion from const char16_t[12] to int or std::string"。

如果用基于完美转发的方法，编译器在转发过程中不会报错，只有到了用转发的参数构造`std::string`时才会报错。这里的报错信息非常难理解。

有时候系统中的转发不止一次，参数可能跨越多层函数最终到达出错位置。这里我们可以用`static_assert`来提前发现这类错误：使用`std::is_constructible`来判断参数是否可以转发下去。

```cpp
class Person {
public:
    template <
        typename T,
        typename = std::enable_if_t<
            !std::is_base_of<Person, std::decay_t<T>>::value> &&
            !std::is_integral<std::remove_reference_t<T>>::value
        >
    >
    explicit Person(T&& n)
    : name(std::forward<T>(n))
    {
        static_assert(
            std::is_constructible<std::string, T>::value,
            "Parameter n can't be used to construct a std::string"
        );
        ...
    }
    ...
};
```

## Item28: 理解引用折叠

在介绍引用折叠之前，我们要先知道引用的引用在C++中是非法的：

```cpp
int x;
...
auto& && rx = x;  // error! can't declare reference to reference
```

以及，在普适引用的类型推断中，如果实参是左值，那么`T`就是左值引用；如果实参是右值，那么`T`没有引用，就是这个类型本身：

```cpp
template <typename T>
void func(T&& param);

Widget widgetFactory();     // function returning rvalue
Widget w;                   // an lvalue
func(w);                    // T deduced to be Widget&
func(widgetFactory());      // T deduced to be Widget
```

在`func(w)`中，`T`的类型是`Widget&`，那么`func`的原型就是：

```cpp
void func(Widget& && param);
```

但引用的引用不是非法的吗？普适引用是个例外，C++有单独的规则来把类型推断中出现的引用的引用转换为单个引用，称为“引用折叠”。折叠规则为：

```cpp
T& &   => T&
T& &&  => T&
T&& &  => T&
T&& && => T&&
```

引用折叠就是`std::forward`依赖的关键特性。一个简化的`std::forward`实现：

```cpp
template <typename T>
T&& forward(typename remove_reference<T>::type& param) {
    return static_cast<T&&>(param);
}
```

假设`func`的实现中调用了`std::forward`：

```cpp
template <typename T>
void f(T&& fParam) {
    ...
    someFunc(std::forward<T>(fParam));
}
```

当`f`的实参是`Widget`的左值时，`T`会被推断为`Widget&`，实例化的`std::forward`版本就是`std::forward<Widget&>`，代入进上面`std::forward`的实现得到：

```cpp
Widget& && forward(typename remove_reference<Widget&>::type param) {
    return static_cast<Widget& &&>(param);
}
```

将`std::remove_reference<Widget&>::type`代换为`Widget`，并应用引用折叠，得到：

```cpp
Widget&& forward(Widget& param) {
    return static_cast<Widget&>(param);
}
```

由此可见，如果普适引用的实参是个左值，将`std::forward`应用其上得到的还是个左值。

如果`f`的实参是右值，那么`T`就是`Widget`，对应的`std::forward`实现是：

```cpp
Widget&& forward(Widget& param) {
    return static_cast<Widget&&>(param);
}
```

这里没有引用的引用，因此也不涉及引用折叠。函数返回的右值引用会被认为是一个右值，因此最终我们得到了一个右值。

引用折叠会在四种场景中发生：

1. 模板实例化，也是最常见的场景。
1. `auto`的类型推断。
1. `typedef`和别名声明（参见Item9）。
1. `decltype`的类型推断。

回顾一下，普适引用并不是什么新东西，它就是满足以下两个条件的右值引用：

1. 类型推断中能区分开左值和右值。
1. 能发生引用折叠。

## Item29: 假设移动操作不存在、不廉价、或没被使用

移动语义可能是C++11最重要的功能，“移动一个容器就像复制一个指针”，“返回临时对象现在很高效，非要避免这么做就是过早优化”。拿C++98的老代码和C++11的STL一起编译，你会发现程序变快了！

但本节是要让你冷静下来。

首先我们可以观察到很多类型还不支持移动。整个C++11的STL做了很多工作来利用移动语义，但可能一些三方库还没有完全按C++11的建议修订完。这些没有针对C++11优化过的代码，基本也不会有性能提升。C++11的编译器只会为没有声明复制操作、移动操作、析构函数的类生成移动函数，还有些类型禁止了移动函数。对于这些没有移动函数的类型，C++11对它们不会有什么帮助。

即使是支持移动的类型，移动带来的收益也没有你想象的大。C++11 STL的所有容器都支持移动，但不是每个容器的移动都很廉价。有些是因为没有廉价的移动手段，有些是需要元素类型支持廉价的移动，容器才能实现廉价的移动。

大部分STL容器，它的数据都是分配在堆上的，例如`std::vector`，因此它的移动就很廉价：直接移动一个指针。但`std::array`的数据是直接分配在栈上的，移动时要移动每个元素。假如元素类型的移动比复制更高效，那么`std::array`的移动也就比复制更高效。

另一个例子，`std::string`提供了O(1)的移动和O(n)的复制，看起来移动要比复制更快。但很多使用了SSO（small string optimization）的`std::string`实现的移动就不一定比复制高效了。

即使对支持高效移动的类型来说，有些看起来肯定会应用移动的地方最终调用的却是复制。Item14讲到STL的一些容器为了保证强异常安全性，只有在元素类型支持noexcept的移动时才会移动，否则会复制。

以下几种情况下C++11的移动对你无益：

- 没有移动操作：会调用复制。
- 移动不够高效：不比复制高效。
- 移动不可用：需要noexcept的移动的场合。

以及：

- 源对象是左值：除了少数情况（见Item25），只有右值可以作为移动的源。

## Item30: 熟悉完美转发的失败案例

假设有一个非完美转发的函数`f`，和它对应的完美转发版本`fwd`：

```cpp
template <typename T>
void fwd(T&& param) {
    f(std::forward<T>(param));
}
```

我们希望以下两行有相同的行为：

```cpp
f(expression);
fwd(expression);
```

但在以下几种情况下，我们会遇到问题。

### 花括号初始化式

```cpp
void f(const std::vector<int>& v);
f({1, 2, 3});   // fine, "{1, 2, 3}" implicitly converted to std::vector<int>
fwd({1, 2, 3}); // error! doesn't compile
```

原因在于，编译器知道`f`的形参类型，所以它知道可以把实参类型隐式转换为形参类型。但编译器不知道`fwd`的形参类型，因此需要通过实参进行类型推断。这里完美转发会在发生以下情况时失败：

- 无法推断出`fwd`的某个参数类型。
- 推断出错误类型。这里的“错误”可以是推断出的类型无法实例化`fwd`，也可以是`fwd`的行为与`f`不同。后者的一个可能原因是`f`是重载函数的名字，推断的类型不对会导致调用错误的重载版本。

在`fwd({1, 2, 3})`这个例子中，问题在于它是一个“未推断上下文”，标准规定禁止推断作为函数参数的花括号初始化式，除非形参类型是`std::initializer_list`。

解决方案很简单，这里我们应用了Item2中提到的一个`auto`特性：会优先推断接收的表达式为`std::initializer_list`。

```cpp
auto il = {1, 2, 3};
fwd(il);
```

### 用`0`或`NULL`作为空指针

例子见Item8，结论就是不要用`0`或`NULL`作为空指针，用`nullptr`。

### 只有声明的`static const`或`constexpr`的整数成员

通常来说我们不需要给类的声明为`static const`或`constexpr`的整数成员一个定义，因为编译器会把这些成员直接替换为对应的整数值：

```cpp
class Widget {
public:
    static constexpr std::size_t MinVals = 28;
    ...
}; // no def for MinVals
...
std::vector<int> widgetData;
widgetData.reserve(Widget::MinVals);
```

如果没有任何地方取`MinVals`的地址，编译器就没有必要给它安排一块内存，就可以直接替换为整数字面值。否则我们就要给`MinVals`一个定义，不然程序会在链接阶段出错。

这里完美转发会有问题：

```cpp
void f(std::size_t val);

f(Widget::MinVals);   // fine, treated as 28
fwd(Widget::MinVals); // error! shouldn't link
```

问题在于`fwd`的参数类型是非const引用，这相当于取了`MinVals`的地址，因此我们需要给它一个定义：

```cpp
constexpr std::size_t Widget::MinVals;  // in Widget's .cpp file
```

注意这里就不用给初始值了，否则编译器会报错的。

### 重载的函数名字和模板名字

假设`f`的参数是一个函数：

```cpp
void f(int (*pf)(int));
```

或者

```cpp
void f(int pf(int));
```

以及我们有两个重载函数：

```cpp
int processVal(int value);
int processVal(int value, int priority);
```

现在我们把`processVal`传给`f`：

```cpp
f(processVal);
```

令人惊讶的是，编译器知道该把`processVal`的哪个版本传给`f`。但`fwd`就不同了：

```cpp
fwd(processVal); // error! which processVal?
```

因为`fwd`的参数没有类型，`processVal`这个名字本身也没能给出一个确定的类型。

模板函数也有这样的问题：

```cpp
template <typename T>
T workOnVal(T param) {...}

fwd(workOnVal); // error! which workOnVal instantiation?
```

解决方案就是确定下来重载函数名字或模板函数名字对应的函数类型：

```cpp
using ProcessFuncType = int (*)(int);
ProcessFuncType processValPtr = processVal;
fwd(processValPtr);
fwd(static_cast<ProcessFuncType>(workOnVal));
```

### 位域

例子：

```cpp
struct IPv4Header {
    std::uint32_t version:4,
                  IHL:4,
                  DSCP:6,
                  ECN:2,
                  totalLength:16;
    ...
};

void f(std::size_t sz);

IPv4Header h;
...
f(h.totalLength);   // fine
fwd(h.totalLength); // error!
```

问题在于`fwd`的参数是非const引用，而C++标准禁止创建位域的非const引用。实际上，位域的const引用就是引用一个临时的复制整数。解决方案很简单：把位域的值复制出来，再传入`fwd`：

```cpp
auto length = static_cast<std::uint16_t>(h.totalLength);
fwd(length);
```

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