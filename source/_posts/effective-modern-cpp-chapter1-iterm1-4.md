---
title:      "Effective Modern C++ 笔记 Chapter1 类型推断 (Item 1-4)"
date:       2017-05-14 15:22:42
tags:
    - C++
    - Effective Modern C++
---

C++98只有一组类型推断的规则：函数模板。C++11增加了两组：`auto`和`decltype`。C++14则扩展了`auto`和`decltype`的适用范围。

我们需要理解类型推断的规则，避免出了问题后脑海中一片白茫茫，Google都不知道该搜什么东西。

## Item1: 理解模板类型推断

应用于函数模板的那套类型推断的规则很成功，数以百万的开发者都在用，即使大多数人都说不清楚这些规则。

但理解类型推断的规则非常重要，因为它是`auto`基础。但`auto`的有些规则反直觉，因此要好好学习下面的规则。

一个函数模板的例子：

```cpp
template <typename T>
void f(ParamType param);
```

以及对它的调用：

```cpp
f(expr);
```

`expr`要用来推断两个类型：`T`和`ParamType`。`ParamType`通常与`T`不同，比如带个`const`或`&`：

```cpp
template <typename T>
void f(const T& param);
```

如果调用是：

```cpp
int x = 0;
f(x); // ParamType is const T&
```

那么`T`被推断为`int`，而`ParamType`则是`const int&`。

这里`T`就是`expr`的类型，符合大家的期望。但不总是这样。`T`的类型不仅与`expr`有关，也与`ParamType`的形式有关：

* `ParamType`是指针或引用（不是`universal reference`即普适引用）。
* `ParamType`是普适引用。
* `ParamType`不是指针也不是引用。

### Case1: ParamType是引用或指针，但不是普适引用

规则：

1. 如果`expr`是引用，就忽略引用。
2. 再用`ParamType`去模式匹配`expr`，来确定`T`。

例如：

```cpp
template <typename T>
void f(T& param); // param is a reference

int x = 27;         // x is an int
const int cx = x;   // cx is a const int
const int& rx = x;  // rx is a reference to x as a const int

f(x);               // T is int, param is int&
f(cx);              // T is const int, param is const int&
f(rx);              // T is const int, param is const int&
```

注意`cx`和`rx`都令`T`被推断为`const int`，`ParamType`为`const int&`。这说明把const对象传给`T&`参数是安全的，不会令const对象被修改。

第三个调用中`rx`是引用，但`T`不是，是因为`rx`的引用属性在推断中被忽略了。

`ParamType`如果是`const T&`，那么三个调用中`T`都会被推断为`int`，而不再保留`expr`的`const`了。

如果`ParamType`是指针，那么类似：

```cpp
template <typename T>
void f(T* param);

int x = 27;
const int* px = &x;

f(&x);  // T is int, param is int*
f(px);  // T is const int, param is const int*
```

总结起来就是，如果`ParamType`是引用或指针：

1. `expr`的引用或指针性会被忽略。
2. 如果`expr`带`const`，那么`T`和`ParamType`中需要且只需要有一个带`const`，来保证`expr`的`const`。

### Case2: ParamType是普适引用

如果`ParamType`是`T&&`（即普适引用），但`expr`是左值引用，那么规则比较复杂：

* 如果`expr`是左值，那么`T`和`ParamType`都被推断为左值引用。两个不寻常处：
    1. 这是模板类型推断中唯一的`T`被推断为引用的场景。
    2. 如果`expr`是右值，那么同Case1。

例如：

```cpp
template <typename T>
void f(T&& param);

int x = 27;         // x is an int
const int cx = x;   // cx is a const int
const int& rx = x;  // rx is a reference to x as a const int

f(x);               // x is lvalue, so T is int&, param is also int&
f(cx);              // cx is lvalue, so T is const int&, param is also const int&
f(rx);              // rx is lvalue, so T is const int&, param is also const int&
f(27);              // 27 is rvalue, so T is int, param is int&&
```

### Case3: ParamType既不是指针也不是引用

如果`ParamType`既不是指针也不是引用，那么`f`就是传值调用，那么`param`就是一个全新的对象，规则：

1. 同上，如果`expr`是引用，就忽略引用性。
2. 如果`expr`带`const`或`volatile`，也忽略。

例子：

```cpp
template <typename T>
void f(T param);

int x = 27;         // x is an int
const int cx = x;   // cx is a const int
const int& rx = x;  // rx is a reference to x as a const int

f(x);               // T and param are both int
f(cx);              // T and param are both int
f(rx);              // T and param are both int
```

注意`expr`如果是指向`const`对象的指针，那么这个`const`不能被忽略掉。

### 数组参数

注意：数组作为参数时与指针的行为不同。

传值调用中，数组会被推断为指针：

```cpp
template <typename T>
void f(T param);

const char name[] = "J. P. Briggs"; // name is const char[13]

f(name); // T is const char*
```

但如果传引用，数组会被推断为数组：

```cpp
template <typename T>
void f(T& param);

f(name); // T is const char [13] and ParamType is const char (&)[13]
```

我们可以利用这点在编译期拿到一个数组的长度：

```cpp
template <typename T, size_t N>
constexpr size_t arraySize(T (&)[N]) noexcept
{
    return N;
}
```

所以我们可以这么定义数组：

```cpp
int keyVals[] = {1, 3, 7, 9, 11, 22, 35}; // keyVals has 7 elements
int mappedVals[arraySize(keyVals)]; // so does mappedVals
```

或者用更现代的方式：

```cpp
std::array<int, arraySize(keyVals)> mappedVals;
```

### 函数参数

另一个会退化为指针的类型是函数。函数会退化为函数指针，且规则与数组相同。例如：

```cpp
void someFunc(int, double);    // someFunc's type is void(int, double)

template <typename T>
void f1(T param);

template <typename T>
void f2(T& param);

f1(someFunc);                  // T and ParamType are both void (*)(int, double)
f2(someFunc);                  // T is void(int, double), ParamType is void (&)(int, double)
```

当然在实践上函数和函数指针几乎没有区别。

## Item2: 理解`auto`类型推断规则

`auto`使用的类型推断规则与模板的规则几乎一样。

```cpp
auto x = 27;
const auto cx = x;
const auto& rx = x;
```

`auto`就相当于上节中的`T`，而`x`、`cx`、`rx`的类型则是`ParamType`。

回忆一下上节介绍的`ParamType`的三种情况，同样可以应用在`auto`上：

* Case1: `auto`类型是指针或引用，但不是普适引用。
* Case2: `auto`类型是普适引用。
* Case3: `auto`类型既不是指针也不是引用。

```cpp
auto x = 27;          // case3: int
const auto cx = x;    // case3: const int
const auto&& rx = x;  // case1: const int&&
auto&& uref1 = x;     // case2: int&. x is lvalue
auto&& uref2 = cx;    // case2: const int&. cx is lvalue
auto&& uref3 = 27;    // case2: int&&. 27 is rvalue
```

以及针对数组和函数的规则：

```cpp
const char name[] = "R. N. Briggs";    // name is const char[13]

auto arr1 = name;                      // arr1 is const char*
auto& arr2 = name;                     // arr2 is const char(&)[13]

void someFunc(int, double);            // someFunc is void(int, double)

auto func1 = someFunc;                 // func1 is void (*)(int, double)
auto& func2 = someFunc;                 // func2 is void (&)(int, double)
```

例外：`auto`会把所有的统一初始化式（花括号初始化式）当作`std::initializer_list<T>`对待。

在`int`的初始化中：

```cpp
int x1 = 27;
int x2(27);
int x3 = {27};
int x4{27};
```

以上四种形式得到的`x1`到`x4`都是一个值为`27`的`int`。

但如果换成`auto`，后两者的类型就有些出乎意料了：

```cpp
auto x1 = 27;      // x1 is int
auto x2(27);       // ditto
auto x3 = {27};    // x3 is std::initializer_list<int>
auto x4{27};       // ditto
```

即使这个初始化式根本没办法匹配成`std::initializer_list<T>`：

```cpp
auto x5 = {1, 2, 3.0}; // error! 
```

如果把`std::initializer_list<T>`传给一个函数模板，行为则不一样：

* 如果`param`的类型为`T`，则报错：

    ```cpp
    template <typename T>
    void f(T param);
    f({11, 23, 9}); // error! can't deduce T
    ```

* 如果`param`的类型为`std::initializer_list<T>`，则可以：

    ```cpp
    template <typename T>
    void f(std::initializer_list<T> param);
    f({11, 23, 9}); // T is int and param is std::initializer_list<int>
    ```

C++14允许`auto`作为函数的返回类型，及lambda函数的参数类型，但这两种情况下的`auto`实际应用的是模板的类型推断规则，而不是上面说的`auto`规则！

```cpp
auto createInitList()
{
    return {1, 2, 3}; // error! can't deduce type for {1, 2, 3}
}

std::vector<int> v;
...
auto resetV = [&v](const auto& newValue) { v = newValue; }
...
resetV({1, 2, 3}); // error! can't deduce type for {1, 2, 3}
```

## Item3: 理解`decltype`

通常`decltype`返回表达式的精确类型（注意，与模板和`auto`不同）：

```cpp
const int i = 0;             // decltype(i) is const int
bool f(const Widget& w);     // decltype(w) is const Widget&
                             // decltype(f) is bool(const Widget&)
struct Point {
    int x, y;                // decltype(Point::x) is int
};                           // decltype(Point::y) is int
Widget w;                    // decltype(w) is Widget
if (f(w)) ...                // decltype(f(w)) is bool
template <typename T>
class vector {
public:
    ...
    T& operator[](std::size_t index);
    ...
};
vector<int> v;               // decltype(v) is vector<int>
...
if (v[0] == 0) ...           // decltype(v[0]) is int&
```

C++11中，`decltype`还可以用来表示一个需要推断出来的类型返回类型：

```cpp
template <typename Container, typename Index>   // requires refinement
auto authAndAccess(Container& c, Index i) -> decltype(c[i]) {
    authenticateUser();
    return c[i];
}
```

一般容器的`operator[]`都会返回`T&`，但像`vector<bool>`就会返回一个中间类型，而用上面的`decltype`就能处理这种情况。

C++14中我们可以省掉尾部的返回类型：

```cpp
template <typename Container, typename Index>   // C++14, not quite correct
auto authAndAccess(Container& c, Index i) {
    authenticateUser();
    return c[i];
}
```

上面的形式的问题在于：`auto`会抹去类型中的引用，导致我们想返回`T&`，但实际却返回了`T`。

所以实际上C++14我们需要写成`decltype(auto)`：

```cpp
template <typename Container, typename Index>   // C++14, requires refinement
decltype(auto) authAndAccess(Container& c, Index i) {
    authenticateUser();
    return c[i];
}
```

我们可以把`decltype(auto)`中的`auto`替换成`return`后面的表达式，即`decltype(c[i])`，这样就能精确的表达我们的意图了。

`decltype(auto)`不仅能用于模板，同样能用于想拿到表达式精确类型的场合：

```cpp
Widget w;
const Widget& cw = w;
auto myWidget1 = cw;           // myWidget1 is Widget
decltype(auto) myWidget2 = cw; // myWidget2 is const Widget&
```

再回头看上面C++14版本的`authAndAccess`，它的问题是参数类型为左值引用，因此无法接受右值参数（`const Container&`可以，但返回类型带`const`）。

什么情况下我们会传入一个容器的右值引用？也许是想拿到一个临时容器的成员的拷贝吧。

我们当然可以用重载来实现同时支持左值和右值引用的目的。如果不用重载的话，就需要把参数类型改成普适引用了：`Container&& c`。为了能同时处理左值和右值引用，我们要引入`std::forward<T>`，它在传入右值时返回右值，传入左值时返回左值。

```cpp
template <typename Container, typename Index>
decltype(auto) authAndAccess(Container&& c, Index i) {
    autherticateUser();
    return std::forward<Container>(c)[i];
}
```

C++11中我们需要使用尾部返回类型：

```cpp
template <typename Container, typename Index>
auto authAndAccess(Container&& c, Index i)
    -> decltype(std::forward<Container>(c)[i])
{
    autherticateUser();
    return std::forward<Container>(c)[i];
}
```

`decltype`一个容易忽视的特性是：如果它的括号中的表达式是左值，且不是一个变量的名字，那么`decltype`会保留引用。

因此`decltype(x)`返回`int`，但`decltype((x))`返回`int&`。

因此在使用`decltype(auto)`时，改变`return`后面的表达式形式，可能会改变返回的类型：

```cpp
decltype(auto) f1() {
    int x = 0;
    ...
    return x;          // return int
}
decltype(auto) f2() {
    int x = 0;
    ...
    return (x);        // return int& to a local variable!
}
```

## Item4: 如何显示推断出来的类型

### IDE

略

### 编译器诊断消息

可以通过编译器在出错时给出的诊断消息来显示推断出的类型。

首先声明一个模板类，但不给出任何定义：

```cpp
template <typename T>
class TD;
```

然后用你想显示的类型来实例化这个模板：

```cpp
TD<decltype(x)> xType;
TD<decltype(y)> yType;
```

显然编译会出错，错误消息中就有我们想看到的类型：

```
error: aggregate 'TD<int> xType' has incomplete type and cannot be defined
error: aggregate 'TD<const int *> yType' has incomplete type and cannot be defined
```

不同的编译器可能会给出不同格式的诊断消息，但基本上都是能帮到我们的。

### 运行时输出

#### 方法1: std::type_info::name

```cpp
std::cout << typeid(x).name() << std::endl;
std::cout << typeid(y).name() << std::endl;
```

这种方法输出的类型可能不太好懂，比如在GNU和Clang中`int`会缩写为`i`，而`const int*`会缩写为`PKi`。

但有的时候`std::type_info::name`的输出靠不住：

```cpp
template <typename T>
void f(const T& param) {
    std::cout << typeid(T).name() << std::endl;
    std::cout << typeid(param).name() << std::endl;
}

std::vector<Widget> createVec();
const auto vw = createVec();

if (!vw.empty()) {
    f(&vw[0]);
}
```

在GNU下输出是：

```
PK6Widget
PK6Widget
```

意思是`const Widget*`。

问题来了：根据Item1的规则，这里`T`应该是`const Widget*`，而`param`应该是`const Widget* const&`啊！

不幸的是，这就是`std::type_info::name`的要求：它要像传值调用一个模板函数一样对待类型，即“去掉引用、const、volatile”。

所以`const Widget* const&`最终变成了`const Widget*`。

### 方法2: boost::typeindex::type_id_with_cvr

在方法1不奏效的时候，可以用`boost::typeindex::type_id_with_cvr`来查看类型。

```cpp
#include <boost/type_index.hpp>

template <typename T>
void f(const T& param) {
    std::cout << boost::typeindex::type_id_with_cvr<T>().pretty_name() << std::endl;
    std::cout << boost::typeindex::type_id_with_cvr<decltype(param)>().pretty_name() << std::endl;
}
```

GNU输出为：

```
Widget const*
Widget const* const&
```

而且，因为`Boost`是一个跨平台的开源库，因此在各个平台的各个编译器下，我们都能得到可用且正确的信息，尽管输出不完全一致。

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