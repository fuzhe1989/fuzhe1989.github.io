---
title:      "Effective Modern C++ 笔记 Chapter3 现代C++（Item 7-10)"
date:       2017-05-22 23:13:03
tags:
    - C++
    - Effective Modern C++
---

## Item7: 创建对象时区分开()和{}

通常来说，C++11中我们能用`()`、`{}`和`=`来初始化一个变量：

```cpp
int x(0);
int y = 0;
int z{0};
int z = {0};  // available in many cases
```

但这几种初始化方式之间还有着区别。

`=`与另外两个不太一样，它代表着拷贝构造函数或赋值函数。

`{}`是C++11引入的新的初始化方式，它被设计为能用在各种地方，表达各种形式的值，也可以称为“统一初始化式”。

它能表达一组值，来初始化STL容器：

```cpp
std::vector<int> v{1, 3, 5};
```

它能用来给类的非static成员设定默认值（而`()`就不行）：

```cpp
class Widget {
...
private:
    int x{0};   // fine
    int y = 0;  // also fine
    int z(0);   // error!
};
```

它和`()`都能用于初始化一个uncopyable的对象（而`=`就不行）：

```cpp
std::atomic<int> ai1{0};   // fine
std::atomic<int> ai2(0);   // also fine
std::atomic<int> ai3 = 0;  // error!
```

`{}`有一个性质：它会阻止基本类型向下转换：

```cpp
double x, y, z;
...
int sum1{x + y + z};    // error! double -> int is prohibited
int sum2(x + y + z);    // ok
int sum3 = x + y + z;   // ditto
```

另一个性质是：它不会被认为是声明。

C++中规定“所有看起来像声明的语句都会被视为声明”，这导致`()`在一些场景下会被视为函数声明，而`{}`则不会：

```cpp
Widget w1(10);      // call Widget ctor with 10
Widget w2();        // NOTICE: w2 is a function declare!
Widget w3{};        // call Widget ctor with no args
```

但是`{}`也不是什么都好，在类有`std::initializer_list`参数的构造函数时，`{}`会有麻烦：`{}`总会被认为是`std::initializer_list`，即使解析出错。

我们先看一个没有`std::initializer_list`构造函数的类`Widget`：

```cpp
class Widget {
public:
    Widget(int i, bool b);
    Widget(int i, double d);
    ...
};
Widget w1(10, true);     // call first ctor
Widget w2{10, ture};     // also call first ctor
Widget w3(10, 5.0);      // call second ctor
Widget w4{10, 5.0};      // also call second ctor
```

一切都很正常，直到我们给`Widget`添加了一个新构造函数：

```cpp
class Widget {
public:
    Widget(int i, bool b);
    Widget(int i, double d);
    Widget(std::initializer_list<long double> il); // added
    ...
};
Widget w1(10, true);     // call first ctor
Widget w2{10, ture};     // NOTICE: now call third ctor(10 and true convert to long double)
Widget w3(10, 5.0);      // call second ctor
Widget w4{10, 5.0};      // NOTICE: now call third ctor(10 and 5.0 convert to long double)
```

甚至通常拷贝和移动构造函数该被调用的地方，都会被劫持到`std::initializer_list`构造函数上：

```cpp
class Widget {
public:
    Widget(int i, bool b);
    Widget(int i, double d);
    Widget(std::initializer_list<long double> il);
    operator float() const;   // added
    ...
};
Widget w5(w4);            // call copy ctor
Widget w6{w4};            // call third ctor! w4 -> float -> long double
Widget w7(std::move(w4)); // call move ctor
Widget w8{std::move(w4)}; // call third ctor! w4 -> float -> long double
```

甚至在`{}`中的内容没办法完全匹配`std::initializer_list`时：

```cpp
class Widget {
public:
    Widget(int i, bool b);
    Widget(int i, double d);
    Widget(std::initializer_list<bool> il);
    ...
};
Widget w{10, 5.0};  // error! requires narrowing conversions
```

只有当`{}`中的所有元素都没办法转换为`std::initializer_list`需要的类型时，编译器才会去选择其它构造函数。比如上面的`bool`改为`std::string`，编译器找不到`{10, 5.0}`中有能转换为`std::string`的元素，就会去匹配我们希望的前两个构造函数了。

一个有趣的地方：如果`{}`中没有元素，那么被调用的是默认构造函数，而不是一个空的`std::initializer_list`。

如果你真的想传入一个空的`std::initializer_list`，那么这样：

```cpp
Widget w4({});
Widget w5{{}};
```

`std::vector<int>`就有上面说的问题：

```cpp
std::vector<int> v1(10, 20);   // use non-std::initializer_list ctor:
                               // create 10-element std::vector, all
                               // elements are 20
std::vector<int> v2{10, 20};   // use std::initializer_list ctor:
                               // create 2-element std::vector whose
                               // values are 10 and 20
```

从这个问题中，我们能学到什么？

首先，向一个已有的类添加`std::initialzier_list`构造函数要非常谨慎，这可能会导致用户的调用被劫持。

其次，作为用户的我们，要小心选择用`()`还是`{}`。`{}`的好处上面已经说了。`()`的好处是：与C++98风格的连续性，还能避免陷入`std::initializer_list`的问题。

当你写模板代码时，如果需要创建一个对象，你会用哪种语法？

```cpp
T localObject(std::forward<Ts>(params)...);
T localObject{std::forward<Ts>(params)...};
```

如果`T`是`std::vector<int>`，参数是`{10, 20}`，哪种是对的？只有用户才知道。

`std::make_unique`和`std::make_shared`遇到了这个问题，它们选择了`()`，并在文档中说明了这个选择。

有一种方法允许用户来指定用`()`还是`{}`：[Intuitive interface](https://akrzemi1.wordpress.com/2013/06/05/intuitive-interface-part-i/)。

（我觉得只在`{}`明确有好处的地方用`{}`，其它地方还是用`()`比较好）

## Item8: 优先选用`nullptr`来替代0和`NULL`

首先是结论：

1. `NULL`是0，0是`int`，不是指针；
2. `nullptr`不是指针，但可以安全地用在需要用指针的场合；
3. 因此在需要空指针的地方，用`nullptr`。
4. 用`nullptr`还能提高代码的可读性。

`NULL`和0不是指针会带来什么问题？函数重载。当一个函数同时有`int`参数和指针参数的两个重载版本时，传入`NULL`或0（下面只说`NULL`吧，反正它们两个是完全一样的），你猜编译器会匹配哪个版本？

```cpp
void f(int);
void f(bool);
void f(void*);

f(0);     // calls f(int), not f(void*)
f(NULL);  // might not complie. typically calls f(int). Never calls f(void*)
```

`f(NULL)`能否编译过取决于`NULL`被定义成了什么：如果定义为0，就能编译过，调用`f(int)`；如果定义为`0L`，那么`NULL`的类型就是`long`，编译器会发现`f`没有直接匹配`long`的重载版本，但有两个相同优先级的转换后匹配`f(int)`和`f(void*)`，于是就报错。

C++98中我们只能尽量避免同时存在整数参数和指针参数的重载版本，但C++11中我们可以用`nullptr`。

`nullptr`不是指针，它的类型是`std::nullptr_t`，但C++11保证它能转换为任意的裸指针类型。

上面的例子中，`f(nullptr)`就会乖乖地调用`f(void*)`。

`nullptr`还能让代码更清晰，尤其是和`auto`配合使用时。假设你看到这么一段代码：

```cpp
auto result = findRecord(/* args */);
if (result == 0) {
    ...
}
```

如果你不看`findRecord`的签名，就没办法区分`result`是整数还是指针。但如果用`nullptr`就很明显的表达了`result`是指针：

```cpp
auto result = findRecord(/* args */);
if (result == nullptr) {
    ...
}
```

在有模板出现的地方，`nullptr`更加有优势。看下面的例子，我们有三个函数，都需要持有相应的锁时才能调用：

```cpp
int    f1(std::shared_ptr<Widget> spw);
double f2(std::unique_ptr<Widget> upw);
bool   f3(Widget* pw);
```

刚开始代码是这样的：

```cpp
std::mutex f1m, f2m, f3m; // mutexes for f1, f2, and f3.
using MuxGuard = std::lock_guard<std::mutex>;
...
{
    MuxGuard g(f1m);
    auto result = f1(0); // pass 0 as null ptr to f1
}
...
{
    MuxGuard g(f2m);
    auto result = f2(NULL); // pass NULL as null ptr to f2
}
...
{
    MuxGuard g(f3m);
    auto result = f3(nullptr); // pass nullptr as null ptr to f3
}
```

上面这段代码能工作，但是丑了点：三段代码的模式都是一样的，应该封装起来。鉴于f1、f2、f3的参数和返回类型都不一样，我们需要用模板来实现：

```cpp
template <typename FuncT, typename MuxT, typename PtrT>
auto lockAndCall(FuncT func, MuxT& mutex, PtrT ptr) -> decltype(func(ptr)) {
    using MuxGuard = std::lock_guard<MuxT>;
    MuxGuard g(mutex);
    return func(ptr);
}
```

C++14中我们还可以用`decltype(auto)`作为返回值类型，但不影响结论。

现在我们来试一下新代码：

```cpp
auto r1 = lockAndCall(f1, f1m, 0);         // error! PtrT is deduced to int
auto r2 = lockAndCall(f2, f2m, NULL);      // error! PtrT is deduced to int
auto r3 = lockAndCall(f3, f3m, nullptr);   // OK, PtrT is nullptr_t
```

前两个调用是错的！模板只能看到`int`，而`shared_ptr`和`unique_ptr`没有`int`参数的构造函数。第三个调用则没有任何问题，`PtrT`被推断为`std::nullptr_t`，在调用f3时隐式转换为`Widget*`。

我们再回头看一下本节的结论，在下个需要传入空指针的地方，你还会用`0`或`NULL`吗？

## Item9: 优先选用`using`来替代`typedef`

大家在用STL时，经常会搞出来`std::unique_ptr<std::unordered_map<std::string, std::string>>`这么长的类型出来。为了避免写好几次这么长的类型，我们会用`typedef`将它定义为一个新类型：

```cpp
typedef std::unique_ptr<std::unordered_map<std::string, std::string>> UPtrMapSS;
```

而在C++11中，我们多了一种选择：可以用`using`做类似的事：

```cpp
using UPtrMapSS = std::unique_ptr<std::unordered_map<std::string, std::string>>;
```

那么为什么在有`typedef`的情况下C++还要再增加一个`using`呢？

首先，`using`在表达类型时更清晰：

```cpp
typedef void (*FP)(int, const std::string&);
using FP = void (*)(int, const std::string&);
```

`typedef`来自于C，它的设计初衷是想用声明变量的方式来声明一个类型，但这导致名字和类型搅合在了一起，严重的影响了可读性。而`using`则将名字和类型分开了，我们能更容易的看出我们声明的类型到底是什么。

其次，`using`能模板化，称为"别名模板"，而`typedef`不能：

```cpp
template <typename T>
using MyAllocList = std::list<T, MyAlloc<T>>;  // OK

template <typename T>
typedef std::list<T, MyAlloc<T>> MyAllockList; // error!
```

C++98中我们可以通过struct来绕过这个问题：

```cpp
template <typename T>
struct MyAllocList {
    typedef std::list<T, MyAlloc<T>> type;
};

MyAllocList<Widget>::type lw; // client code
```

但在模板里我们不能直接使用这个类型，要在前面加上`typename`：

```cpp
template <typename T>
class Widget {
private:
    typename MyAllocList<T>::type list;
};
```

原因是编译器在第一次处理模板时，不知道`T`是什么，也没办法知道`MyAllocList<T>::type`是什么，它只能假设`type`是`MyAllocList<T>`的一个成员。只有前面加上`typename`编译器才能放心的将`type`按类型处理。

而`using`则明确表明它就是声明了一个类型，因此不需要加`typename`。

做过一些模板元编程（TMP）的人应该都有体会，在处理类型时，我们经常要用到下面的trait类：

```cpp
std::remove_const<T>::type          // const T -> T
std::remove_reference<T>::type      // T& and T&& -> T
std::add_lvalue_reference<T>::type  // T -> T&
```

但每次使用都要在前面加上`typename`，就是因为这些trait类都是通过`typedef`定义出来的。

C++14中这些类都有了一个`using`版本，直接是一个类型，使用时不需要加`typename`：

```cpp
std::remove_const<T>::type         -> std::remove_const_t<T>
std::remove_reference<T>::type     -> std::remove_reference_t<T>
std::add_lvalue_reference<T>::type -> std::add_lvalue_reference_t<T>
```

如果你还在用C++11，但很羡慕这些新类型，你也可以自己动手实现：

```cpp
template <typename T>
using remove_const_t = typename remove_const<T>::type;
```

## Item10: 优先选用有界枚举来替代无界枚举

C++的通用规则是name会从属于它所在的scope，只在这个scope内可见。但C++98的enum没有遵循这个规则，enum内定义的name，不只在这个enum内可见，而是在enum所在的整个scope内可见！

```cpp
enum Color {black, white, red};
auto white = false;             // error! white already declared in this scope
```

这种enum我们称为“无界枚举”，源自C。C++11增加了一种有界的枚举，来解决name泄漏到整个scope的问题。

```cpp
enum class Color {black, white, red};
auto white = false;             // fine, no other "white" in this scope
Color c = white;                // error!
Color c = Color::white;         // fine
auto c = Color::white;          // also find, c is 'Color'
```

`enum class`定义的枚举又被称为“枚举类”。而且它还有一个特性：无法隐式转换为整数类型（也因此无法隐式转换为浮点类型）：

```cpp
enum Color {black, white, red};
std::vector<std::size_t> primeFactors(std::size_t x);

Color c = red;
...
if (c < 14.5) {                      // compare Color to double!
    auto factors = primeFactors(c);  // compute prime factors of a Color!
    ...
}
```

上面的转换显然不是我们预期的，而用enum class就可以避免这种转换：

```cpp
enum class Color {black, white, red};
std::vector<std::size_t> primeFactors(std::size_t x);

Color c = Color::red;
...
if (c < 14.5) {                      // error! can't compare Color and double!
    auto factors = primeFactors(c);  // error! can't pass Color to primeFactors
    ...
}
```

如果实在需要做这种转换，我们可以用`static_cast`来做，比如`static_cast<std::size_t>(c)`。

enum class还有一个特性：可以前向声明。C++98中的无界enum则不能前向声明。C++11中的无界enum可以在指定底层类型后前向声明。

```cpp
enum Color;          // error!
enum class Color;    // fine
```

为什么C++98只支持enum的定义，而不支持声明？因为C++中每个enum类都对应着一个底层整数类型，但C++98中我们没办法指定这个类型，也没办法确定这个类型。编译器会在看到enum的定义时确定它底层用什么整数类型来存储。而前向声明的一个基本要求是：知道对应类型的大小。如果我们没办法确定enum是用什么类型存储的，我们也就没办法知道enum的大小，也就没办法前向声明。

```cpp
enum Status {
    good = 0,
    failed = 1,
    incomplete = 100,
    corrupt = 200,
    indeterminate = 0xFFFFFFFF
};
```

编译器在看到上面的`Status`定义时，发现它的所有的值范围都在[-1, 200]之间，最合适的类型就是`char`。如果我们增加一项`audited = 500`，值范围就变成了[-1, 500]，最合适的类型变成了`short`！

但C++98这种过于死板的规定也导致了：每当我们向`Status`中增加一项，所有引用了它的.cpp文件都需要被重新编译一次。

C++11允许我们指定enum的底层类型，尤其是enum class在不指定时默认使用int。这就保证了我们能安全的前向声明enum和enum class。

```cpp
enum class Status;                 // underlying type is int
enum class Status: std::uint32_t;  // underlying type is std::uint32_t
enum Status;                       // error!
enum Status: std::uint32_t;        // underlying type is std::uint32_t
```

注意：如果要指定底层类型，需要在enum和enum class的声明和定义处都指定相同的底层类型。

那么enum class有什么地方不如enum呢？还真有。

C++11中我们使用`std::tuple`时需要用到整数常量作为下标：

```cpp
auto val = std::get<1>(uInfo);
```

如果用enum来代替硬编码的下标，对可读性有好处：

```cpp
enum UserInfoFields {uiName, uiEmail, uiReputation};
auto val = std::get<uiEmail>(uInfo);
```

但换成enum class上面的代码就不行了：enum class没办法隐式转换为整数类型。但我们可以用`static_cast`。

```cpp
enum class UserInfoFields {uiName, uiEmail, uiReputation};
auto val = std::get<static_cast<std::size_t>(UserInfoFields::uiEmail)>(uInfo);
```

这么写太长了，我们可能需要一个辅助函数。注意：`std::get`是模板，它的参数需要是编译期常量，因此这个辅助函数必须是一个`constexpr`函数。我们来通过`std::underlying_type`实现一个将enum class的值转换为它的底层类型值的`constexpr`函数：

```cpp
template <typename E>
constexpr typename std::underlying_type<E>::type toUType(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}
```

C++14中我们可以用`std::underlying_type_t`：

```cpp
template <typename E>
constexpr std::underlying_type_t<E>::type toUType(E e) noexcept {
    return static_cast<std::underlying_type_t<E>>(e);
}
```

这样我们在使用`std::tuple`时就可以：

```cpp
auto val = std::get<toUType(UserInfoFields::uiEmail)>(uInfo);
```

还是有点长，但已经好多了，尤其是考虑到enum class相比于enum的其它好处：不污染`namespace`；不会莫名的做隐式转换。

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
