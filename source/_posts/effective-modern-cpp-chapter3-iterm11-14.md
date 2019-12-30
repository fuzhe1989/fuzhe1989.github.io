---
title:      "Effective Modern C++ 笔记 Chapter3 现代C++（Item 11-14)"
date:       2017-07-05 22:54:15
tags:
    - C++
    - Effective Modern C++
---

## Item11: 优先用deleted函数取代private的未定义函数

有时C++会为你自动生成一些函数，但你想要阻止其他人调用这些函数。

C++98中，为了避免编译器为我们生成拷贝构造函数和赋值函数，最佳实践是：将它们声明为private函数，且不定义。

比如`basic_ios`类不希望自己被拷贝构造和赋值，在C++98中的做法是：

```cpp
template <class charT, class traits = char_traits<charT> >
class basic_ios: public ios_base {
public:
    ...
private:
    basic_ios(const basic_ios&);             // not defined
    basic_ios& operator=(const basic_ios&);  // not defined
};
```

这样对于没有权限访问它们的用户，编译期会报访问权限的错误，而对于`basic_ios`的友元这样有权限访问private函数的用户，链接期会报"undefined reference"。

C++11中我们可以将这样的函数声明为`= delete`：

```cpp
template <class charT, class traits = char_traits<charT>>
class basic_ios: public ios_base {
public:
    ...
    basic_ios(const basic_ios&) = delete;
    basic_ios& operator=(const basic_ios&) = delete;
    ...
};
```

注意到C++11中我们将这两个函数声明为public，这样友元也会在编译期报错，错误会更友好一些。

deleted函数的一个重要优势在于，它不只能用于成员函数（未定义的private函数只能是成员函数）！

例如我们有这么个函数：

```cpp
bool isLucky(int number);
```

C++的隐式转换导致非整数的基本类型也能调用这个函数：

```cpp
if (isLucky('a)) ...

if (isLucky(true)) ...

if (isLucky(3.5)) ...
```

C++11中我们可以将这些我们不想要的函数定义为deleted：

```cpp
bool isLucky(int number);

bool isLucky(char) = delete;
bool isLucky(bool) = delete;
bool isLucky(double) = delete;
```

这些deleted函数仍然会参与到重载决议中，再报错。

deleted函数的另一类用途是禁止模板的某个特化版本。例如：

```cpp
template <typename T>
void processPointer(T* ptr);
```

我们不想将其用于`void*`和`char*`，就将它们声明为deleted：

```cpp
template <>
void processPointer<void>(void*) = delete;

template <>
void processPointer<char>(char*) = delete;
```

如果要做彻底的话，我们还要将`const void*`、`const volatile void*`、`wchar_t`这样的类型一一禁止掉。

C++98中，我们没办法通过声明为private来禁止其他人调用模板成员函数的某个特化版本，因为模板成员函数的所有版本的访问权限都是一样的：

```cpp
class Widget {
public:
    ...
    template <typename T>
    void processPointer(T* ptr) {...}
private:
    template <>
    void processPointer<void>(void*);   // error!
};
```

而C++11中我们可以在类外面将这个特化版本标记为deleted：

```cpp
class Widget {
public:
    ...
    template <typename T>
    void processPointer(T* ptr) {...}
};

template <>
void Widget::processPointer<void>(void*) = delete;   // still public
```

C++98的未定义private函数的方法其实就是要达到C++11中deleted函数的效果，因此在C++11中使用deleted函数总是更好的。

## Item12: 将重写函数声明为`override`

C++的面向对象的基础就是类的继承和虚函数的重写(override)，允许把对基类的接口调用转到子类的重写函数上：

```cpp
class Base {
public:
    virtual void doWork();
    ...
};

class Derived: public Base {
public:
    virtual void doWork();
    ...
};

std::unique_ptr<Base> upb = std::make_unique<Derived>();
upb->doWork();   // call Derived::doWork
```

重写需要满足几个条件：

* 基类中的函数必须是虚函数。
* 基类与子类中的函数必须同名（析构函数除外）。
* 基类函数与子类函数的参数类型必须相同。
* 基类函数与子类函数的const性必须相同。
* 子类函数的返回类型和异常规格必须与基类函数的兼容。

以上是C++98中对重写的要求，C++11又加了一条：

* 函数的引用限制必须相同（被调用的对象是左值还是右值）。

```cpp
class Widget {
public:
    ...
    void doWork() &;   // applies only when *this is an lvalue
    void doWork() &&;  // applies only when *this is an rvalue
};
```

如果违反了这些条件，我们就会遇到一个名字差不多，但实际没有关系的概念：重载(overload)。重载函数就是名字相同，但不符合上面其它条件的函数。而且，子类中的重载函数会屏蔽基类中同名的版本，即在重载决议阶段我们只能看到子类中的各个重载版本。

```cpp
class Base {
public:
    virtual void mf1() const;
    virtual void mf2(int x);
    virtual void mf3() &;
    void mf4() const;
};

class Derived: public Base {
public:
    virtual void mf1();
    virtual void mf2(unsigned int x);
    virtual void mf3() &&;
    void mf4() const;
};
```

我们很容易就制造了一个全是重载没有重写的场景：

* `mf1`的const性不同。
* `mf2`的参数类型不同。
* `mf3`的引用性不同。
* `mf4`在基类中不同虚函数。

编译器不一定会提醒我们弄出了重载函数，但我们需要保证这点。C++11增加了`override`修饰符，可以声明一个函数是重写函数，否则就会报错。

```cpp
class Derived: public Base {
public:
    virtual void mf1() override;
    virtual void mf2(unsigned int x) override;
    virtual void mf3() && override;
    virtual void mf4() const override;
};
```

以上4个函数在编译时都会报错。

在这个基础上，把所有的重写函数都加上`override`，还能帮助我们去衡量修改一个基类接口的代价：所有标记了`override`的子类重写函数都会报错。如果没有加上`override`，我们就只能人肉去修改，再指望单元测试足够全面了。

C++11引入了两个局部关键字：`override`和`final`(阻止子类重写此函数)，它们只在函数声明的修饰符区域才是关键字，其它地方不是。你不必担心下面的代码出错：

```cpp
class Warning {
public:
    ...
    void override();
    ...
};
```

下面说一下函数的引用限制。我们在某些场景下需要知道对象是左值还是右值。

```cpp
class Widget {
public:
    using DataType = std::vector<double>;
    ...
    DataType& data() { return values; }
    ...
private:
    DataType values;
};

Widget w;
auto vals1 = w.data();
```

这里`Widget::data`返回了一个左值，因此`vals1`的初始化调用了`vector`的拷贝构造函数。

假设我们有个函数`Widget makeWidget()`，它返回一个临时的`Widget`对象，在这个临时对象上调用`data`就不太值了：

```cpp
auto vals2 = makeWidget().data();
```

如果我们能在调用`data`时知道*this是右值的话，就可以返回一个右值：

```cpp
class Widget {
public:
    using DataType = std::vector<double>;
    ...
    DataType& data() & { return values; }
    DataType&& data() && { return std::move(values); }
    ...
private:
    DataType values;
};
```

这样`vals2`的初始化就只需要调用`vector`的移动构造函数。

## Item13: 优先用const_iterator代替iterator

const_iterator指向const对象，我们当然希望尽可能用它，但C++98对它的支持很不全面，首先难以创建，其次可使用的场景很受限。

例如，下面这段代码：

```cpp
std::vector<int> values;
...
std::vector<int>::iterator it = std::find(values.begin(), values.end(), 1983);
values.insert(it, 1988);
```

这里有3个iterator：`it`、`values.begin()`、`values.end()`，最好是能把它们替换为const_iterator。但：

1. 非const的容器对象的`begin()`和`end()`只能返回iterator，不能返回const_iterator。
1. `vector::insert`的第一个参数只接受iterator，不接受const_iterator。

如果非要在C++98中把上面的iterator替换为const_iterator，我们需要使用多次`static_cast`：

```cpp
typedef std::vector<int>::iterator IterT;
typedef std::vector<int>::const_iterator ConstIterT;
std::vector<int> values;
...
ConstIterT ci = std::find(
    static_cast<ConstIterT>(values.begin()),
    static_cast<ConstIterT>(values.end()),
    1983
);
values.insert(static_cast<IterT>(ci), 1988);
```

实际上面这段代码可能都编译不了，因为const_iterator到iterator并没有一个可移植的转换方式，`static_cast`不行，`reinterpret_cast`也不行。总之C++98中用const_iterator就是会有一堆的麻烦，结果就是大家都不用const_iterator了。

C++11做了几个改变，令const_iterator重新回到人们的视野中：

1. STL中的几个容器类提供了`cbegin`和`cend`成员函数，返回非const对象的const_iterator。
1. 提供`std::cbegin`和`std::cend`函数，返回参数的const_iterator，甚至支持数组。
1. STL的几个容器类增加了多个接受const_iterator参数的成员函数重载版本，比如`insert`。

上面的第2条不太准确，实际上C++11只增加了`begin`和`end`这两个非成员函数，C++14则一口气增加了`cbegin`、`cend`、`rbegin`、`rend`、`crbegin`和`crend`这六个非成员函数。

前面的代码在C++11中是这样的：

```cpp
std::vector<int> values;
...
auto it = std::find(values.cbegin(), values.cend(), 1983);
values.insert(it, 1988);
```

如果我们想在C++11中就用到非成员版的`cbegin`，大可以自己写一个：

```cpp
template <typename C>
auto cbegin(const C& container) -> decltype(std::begin(container)) {
    return std::begin(container);
}
```

这里为什么返回的是`std::begin(container)`？为什么不返回`container.cbegin()`？

1. 注意`container`的类型是`const C&`，通常来说const对象的`begin`与`cbegin`都会返回const_iterator。而且还会有一些类只定义了`begin`，没有定义`cbegin`，这样调用`begin`可以适用于更多的类型。

2. 调用`std::begin`的话，对于定义了`begin`成员函数的类，与调用成员版本的`begin`是相同效果的；对于数组类型，它没有成员版本的`begin`，但有`std::begin`的一个特化版本，因此调用`std::begin`能适用于更多的情况。

回到正题上，C++11后，你就可以尽量多地用const_iterator了。

## Item14: 如果函数永远不会抛出异常，则声明其为`noexcept`

C++98中的异常规格是一个很难用的特性：你要总结出这个函数可能抛哪些异常，还包括它下层函数可能抛的异常，把这些异常类型写到异常规格中，一旦改了实现（或下层函数改了实现），你还要修改异常规格，由此导致函数签名发生变化，可能破坏一大堆用户代码。这其中编译器通常帮不上忙。总之大多数人都认同C++98的异常规格是一个设计失误，不值得花那么大的代价来使用它。

但人们发现，标记一个函数可能抛哪些异常通常没什么意义，还惹来一大堆麻烦，但标记一个函数**会不会**抛异常却很有意义。

因此C++11中我们可以标记一个不会抛异常的函数为`noexcept`：

```cpp
int f(int x) throw();   // C++98 style
int f(int x) noexcept;  // C++11 style
```

编译器不会在编译期检查这个限制，但在运行期，一个标记为`noexcept`的函数如果抛了异常，程序会直接终止。这里与违背异常规格的现象有些像，但有不同：违背异常规格时，程序会展开调用栈，再终止；而违背`noexcept`时，程序**可能**会展开调用栈，再终止。

这个“可能”非常重要，它允许编译器不去生成处理栈展开的代码（可能对目标代码的大小有明显的影响），不去按构造的相反顺序析构对象，甚至不去析构对象。

而且，在某些场景下，我们可以利用有没有`noexcept`来做不同的操作，从而优化代码。一个例子：

```cpp
std::vector<Widget> vw;
...
Widget w;
...
vw.push_back(w);
```

如果vw空间已经满了，再调用`push_back`就需要重新分配一块更大的空间。现在的问题就是如何把vw中旧的元素转移到新的空间上。为了保证转移过程中的异常安全性，C++98中我们只能一个一个拷贝过去，全部成功后再把旧的对象依次销毁掉。但C++11中，我们可以用到`noexcept`的信息：如果`Widget`有移动构造函数，且标记为`noexcept`，我们就可以放心地用移动构造函数去构造新元素，而不用担心抛异常导致vw被破坏。

`vector`的`push_back`就是这么做的，一些其它C++98保证了强异常安全性的函数也有着类似的行为。但它们是如何知道函数的`noexcept`信息的呢？`vector::push_back`中调用了`std::move_if_noexcept`，而`std::move_if_noexcept`调用了`std::is_nothrow_move_constructible`，后者的值是由编译器给的。

一些泛型函数可以根据它们的参数来推断是否有`noexcept`的保证。以`swap`为例：

```cpp
template <typename T, size_t N>
void swap(T (&a)[N], T (&b)[N]) noexcept(noexcept(swap(*a, *b)));

template <typename T1, typename T2>
struct pair {
    ...
    void swap(pair& p) noexcept(noexcept(swap(first, p.first)) &&
                                noexcept(swap(second, p.second)));
    ...
};
```

`noexcept`实际上有三种用法：

1. 作为函数规格的单独的`noexcept`，即不抛异常的保证。
1. 作为函数规格区域的`noexcept(bool-expression)`，如果bool-exp为`true`，则与单独的`noexcept`相同，否则与没有这个`noexcept`相同。
1. 表达式`noexcept(func-call-exp)`，如果func-call-exp为`noexcept`则返回`true`，否则返回`false`。

上面的`swap`的第一个例子，说的是如果`swap<T>`有`noexcept`的保证，则`swap<T, N>`也有，否则也没有。第二个例子是只有`swap<T1>`和`swap<T2>`都有`noexcept`，`pair::swap`才有`noexcept`的保证。

看起来很美好，但是不是所有函数都要加上`noexcept`呢？

1. 优化很重要，但正确性更重要。只有真的不应该抛异常的函数才应该加上`noexcept`。
1. `noexcept`是函数签名的一部分，所以如果一个接口当前不抛异常，但长远来看不确定会不会抛异常，那么也不建议加`noexcept`。
1. 加了`noexcept`不代表这个函数不能抛异常，而是“如果这里抛了异常，程序就应该直接挂掉”，只有这样的函数，才应该加`noexcept`。

注意第3点，C++98中我们认为内存释放函数（`operator delete`和`operator delete[]`）和析构函数抛异常是种不好的写法，而C++11中干脆默认它们都是`noexcept`的。如果你不想要这个特性，需要显式声明它们为`noexcept(false)`。但为什么要这么做呢？标准库中没有这种写法，且一个析构函数可能抛异常的类型与标准库同时使用的行为也是未定义的。

一些库作者会把函数分成“高可用性”和“低可用性”两种，“高可用性”指对参数没有要求，调用方可以任意传参数而不用担心出错，这样的函数当然可以声明为`noexcept`。剩下的函数就是低可用性的，它可能对参数有检查，或者在参数不符合要求时行为未定义。这样的函数就要好好想一想要不要声明为`noexcept`了：如果声明为`noexcept`，抛异常就会导致程序终止，那么怎么测试它对参数的要求？

最后需要注意的是一个声明为`noexcept`的函数，如果内部调用了未声明为`noexcept`的函数，编译器不会抛错，连警告都没有，原因是：

1. 作者想表达的是“正常不会抛异常，如果这里抛了异常，程序就应该直接挂掉”，编译器需要尊重这种选择。
1. 可能调用的函数是C函数，或是C++98中的确实不会抛异常的函数，这些函数显然没办法声明为`noexcept`。

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