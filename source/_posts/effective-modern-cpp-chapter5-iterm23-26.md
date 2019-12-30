---
title:      "Effective Modern C++ 笔记 Chapter5 右值引用、移动语义、完美转发（Item 23-26)"
date:       2017-08-08 17:38:37
tags:
    - C++
    - Effective Modern C++
---

初学移动语义和完美转发时，它们看起来非常直接：

* 移动语义：允许编译器用廉价的移动操作来替换昂贵的复制操作。与用复制构造函数和复制赋值函数来实现复制语义的方式类似，你也可以用移动构造函数和移动赋值函数来实现移动语义。移动语义也允许你设计出只能移动的类型，诸如`std::unique_ptr`、`std::future`、`std::thread`。
* 完美转发：允许实现一个函数模板，接受任意数量的参数并转发给其它函数，且目标函数接收到的参数恰好与转发函数收到的参数数量相等。

右值引用就是把这两种看起来截然不同的功能联系起来的纽带，它是实现这两者的基础。

你对这两个功能了解的越多，你越会发现上面说的只是它们的冰山一角。移动语义、完美转发、右值引用比它们看起来要微妙得多。例如，`std::move`不移动任何东西，完美转发也不完美。移动操作不一定比复制要廉价；即使是，也可能没有你想象的廉价；能移动的场景也不一定真的调用了移动操作。`type&&`不一定代表右值引用。

无论你对这些功能钻研多深，都有你没发现的东西。幸运的是，它们的深度总是有限的。本章会带你探寻它们的基本原理，之后你再看这些功能就会觉得更合理了。

在本章的各节中，很重要的一点是牢记参数永远是一个左值，即使它的类型是右值引用，即：

```cpp
void f(Widget&& w);
```

w是左值，即使它的类型是右值引用。

## Item23: 理解`std::move`和`std::forward`

在学习`std::move`和`std::forward`时，一种很有用的方法是知道它们不做什么：`std::move`不移动任何东西，`std::forward`也不转发任何东西。它们在运行期不做任何事情，它们不产生一丁点可执行的代码。

`std::move`和`std::forward`仅仅是进行转换的函数。`std::move`无条件地将它的参数转换为右值，而`std::forward`只在某些条件满足时进行这种转换。

下面一个接近标准库实现的`std::move`实现的例子：

```cpp
template <typename T>
typename remove_reference<T>::type&& move(T&& param) {
    using ReturnType = typename remove_reference<T>::type&&;
    return static_cast<ReturnType>(param);
}
```

如你所见，`std::move`接受一个对象的引用（具体来说，是普适引用，参见Item24）并返回这个对象的另一个引用。

"&&"表示`std::move`的返回类型是一个右值引用，但参见Item28，如果类型`T`恰好是左值引用，那么`T&&`也会变成左值引用，因此我们要在`T`上应用`std::remove_reference`来保证`std::move`一定返回右值引用。

C++14中`std::move`的实现可以更简单一些：

```cpp
tmeplate <typename T>
decltype(auto) move(T&& param) {
    using ReturnType = remove_reference_t<T>&&;
    return static_cast<ReturnType>(param);
}
```

一定要牢记`std::move`做了什么，不做什么：它做了转换，但不做移动。在一个对象上应用`std::move`就告知了编译器这个对象可以被移动，这就是它叫这个名字的原因：易于标记出可能被移动的对象。

事实上，右值只是**通常**会被移动。假设你在写一个表示注解的类，它的构造函数接受包含注解的`std::string`参数，并将其复制给一个成员变量。根据Item41，你声明了一个传值的参数：

```cpp
class Annotation {
public:
    explicit Annotation(std::string text);
    ...
};
```

但构造函数里只需要读取`text`，根据尽可能用`const`的古老传统，你给`text`加上了`const`：

```cpp
class Annotation {
public:
    explicit Annotation(const std::string text);
    ...
};
```

为了避免复制，你依从Item41的建议，在`text`上应用`std::move`，产生一个右值：

```cpp
class Annotation {
public:
    explicit Annotation(const std::string text)
        : value(std::move(text))
    {...}
    ...
private:
    std::string value;
};
```

上面的代码编译、链接、运行都没问题，只是`text`没有移动赋值给`value`，它是复制过去的。`text`的类型是`const string`，因此`std::move(text)`产生的类型为`const string&&`，因此`value`的构造没办法应用移动操作，因为`const`还在。

`std::string`定义了复制构造函数和移动构造函数：

```cpp
class string {
public:
    ...
    string(const string& rhs);
    string(string&& rhs);
    ...
};
```

显然`const string&&`没办法传给`string(string&& rhs)`，但能传给`string(const string& rhs)`。因此`value`的构造应用了复制构造函数，即使参数是右值引用！

这里我们学到两点：

1. 不要把希望移动的变量声明为`const`。
1. `std::move`不意味着移动任何东西，甚至不保证它转换的对象可移动。它只保证它的转换结果一定是右值。

`std::forward`与`std::move`很类似，只是`std::move`是无条件的转换，而`std::forward`是有条件的转换。回忆`std::forward`的典型用法，是在接受普适引用参数的函数模板中将参数转发给其它函数：

```cpp
void process(const Widget& lval);
void process(Widget&& rval);

template <typename T>
void logAndProcess(T&& param) {
    auto now = std::chrono::system_clock::now();
    makeLogEntry("Calling 'process'", now);
    process(std::forward<T>(param));
}
```

我们希望在`param`类型为左值引用时调用`process(const Widget& lval)`，在`param`为右值引用时调用`process(Widget&& rval)`。但`param`是函数参数，它本身永远是左值。因此我们需要一种方法在条件满足时将其转换为右值——`logAndProcess`的实参为右值。这就是`std::forward`要做的，有条件的转换，即**当且仅当**它的参数是通过右值初始化时进行转换。

`std::forward`为什么能知道`param`是左值引用还是右值引用？参见Item28。

既然`std::move`和`std::forward`的差别只在于发生转换的条件，为什么不能去掉`std::move`都用`std::forward`？纯技术上没问题，而且它们两个都不是必不可少的，因为我们可以在任何地方手写转换。但希望你也能认同这么做很恶心。

`std::move`的吸引力来自它的便利性、减少了发生错误的可能性、以及更清晰的意图表达。比如我们想记录一个类型的对象被移动构造了多少次，可以用一个static的计数器：

```cpp
class Widget {
public:
    Widget(Widget&& rhs)
        : s(std::move(rhs.s)) {
        ++moveCtorCalls;
    }
};
```

如果用`std::forward`来实现，代码类似于：

```cpp
class Widget {
public:
    Widget(Widget&& rhs)
        : s(std::forward<std::string>(rhs.s)) {
        ++moveCtorCalls;
    }
};
```

首先`std::move`只需要一个参数，而`std::forward`还需要一个模板参数。其次注意我们传给`std::forward`的类型不能有引用，这是在编码一个被以右值方式传递的参数时的约定（参见Item28）。合起来，这意味着我们用`std::move`可以更少的敲击键盘，减少了在传递右值参数时类型错用为右值的风险，还消除了我们用错类型的问题（如转换为`std::string&`会导致`s`的构造使用复制构造函数）。

更重要的是，`std::move`是无条件的转换，而`std::forward`只在参数为右值引用时将其转换为右值。这里有两个不同的动作，一是移动，二是将对象传递给其它函数且保持其左值性或右值性。这两个显然不同的动作就要用两个显然不同的函数来触发。

## Item24: 区分普适引用与右值引用

有句话叫“真理使你自由，但在适当的情况下，精心选择的谎言同样使你自由”。本节就是这样一个谎言，但在软件行业，我们不说“谎言”，而是说本节包含一个“抽象”。

看起来“T&&”就代表着一个右值引用，但没有这么简单：

```cpp
void f(Widget&& param);          // rref
Widget&& var1 = Widget();        // rref
auto&& var2 = var1;              // not rref
template <typename T>            // rref
void f(std::vector<T>&& param);
template <typename T>            // not rref
void f(T&& param);
```

“T&&”有两个含义，第一个就是右值引用，它的主要作用是标记一个可以移动的对象；第二个含义则既可能是右值引用也可能是左值引用，即看起来是“T&&”但实际上可能是“T&”。进一步地，“T&&”可能绑定在const或非const、volatile或非volatile对象上。理论上它可以绑定在任何对象上。我称其为“普适引用”。

普适引用发生在两个场景中，第一个是函数模板：

```cpp
template <typename T>
void f(T&& param);
```

第二个是`auto`声明：

```cpp
auto&& var2 = var1;
```

它们的共同点就是需要类型推断。如果不需要类型推断，例如`Widget&&`，这就不是普适引用，就只是一个右值引用。

普适引用的初始化式决定了它是右值引用还是左值引：如果初始化式是右值，普适引用就是右值引用；如果初始化式是左值，普适引用就是左值引用：

```cpp
template <typename T>
void f(T&& param);    // universal reference

Widget w;
f(w);                 // lvalue passed to f: Widget&
f(std::move(w));      // rvalue passed to f: Widget&&
```

光有类型推断还不足够，普适引用要求引用的声明格式必须是`T&&`，而不是`std::vector<T>&&`或`const T&&`这样的声明。

如果你在模板中看到了一个函数参数为`T&&`，也不代表它是普适引用，因为这里可能根本不需要类型推断。例如：

```cpp
template <class T, class Allocator = allocator<T>>
class vector {
public:
    void push_back(T&& x);
    ...
};
```

这里`push_back`的参数`x`不是普适引用，因为编译器会先实例化`vector`，之后你就发现`push_back`根本没有涉及到类型推断。例子：

```cpp
class vector<Widget, allocator<Widget>> {
public:
    void push_back(Widget&& x);
    ...
};
```

与之相反，`emplace_back`应用了类型推断：

```cpp
template <class T, class Allocator = allocator<T>>
class vector {
public:
    template <class... Args>
    void emplace_back(Args&&... args);
    ...
};
```

`args`就是一个普适引用，因为它满足两个条件：

1. 它的格式是`T&&`，当然这里是`Args&&`。
1. 它需要类型推断。

`auto&&`是普适引用也是相同的原因。它在C++11中出现得越来越多，在C++14中出现得更多，因为lambda表达式可以声明`auto&&`的参数了：

```cpp
auto timeFuncInvocation =
    [](auto&& func, auto&&... params) {
        start timer;
        std::forward<decltype(func)>(func)(
            std::forward<decltype(params)>(params)...
        );
        stop timer and record elapsed time;
    };
```

注意参数`func`，它是一个普适引用，因此可以绑定到任意可调用的对象上，无论左值还是右值；`params`是0或多个普适引用，可以绑定到任意数量的任意类型上。结果就是，利用`auto`普适引用，`timeFuncInvocation`可以完美地记录任何函数执行的时间。

但要记住，本节的基础，所谓的“普适引用”，只是一个谎言或“抽象”，它的底层机理被称为“引用折叠”，我们将在Item28中讲到。但真相不会减少这个抽象的价值，了解右值引用与普适引用的差别能让你更准确地阅读代码，也令你与同事交流时避免含糊不清，也能让你更好地理解Item25和Item26。

## Item25: 将`std::move`用于右值引用，`std::forward`用于普适引用

右值引用就表示对应的对象可以被移动，对于那些可以被移动的对象，我们可以用`std::move`来让其它函数也能利用上它们的右值性：

```cpp
class Widget {
public:
    Widget(Widget&& rhs)
    : name(std::move(rhs.name)), p(std::move(rhs.p))
    {...}
    ...
private:
    std::string name;
    std::shared_ptr<SomeDataStructure> p;
};
```

而普适引用则既可能代表一个左值，又可能代表一个右值，只有在它代表右值时，我们才能将它cast成右值，这就是`std::forward`做的：

```cpp
class Widget {
public:
    template <typename T>
    void SetName(T&& newName) {
        name = std::forward<T>(newName);
    }
    ...
};
```

简单来说，就是右值引用总是可以无条件转换为右值，因此用`std::move`，但普适引用不一定是右值，因此要用`std::forward`做有条件的右值转换。

参见Item23，在右值上可以应用`std::forward`，但这样的代码啰嗦，容易错，也不地道。更糟糕的是在普适引用上应用`std::move`，因为它会无意间修改左值：

```cpp
class Widget {
public:
    template <typename T>
    void setName(T&& newName) {
        name = std::move(newName); // compiles, but is bad, bad, bad!
    }
    ...
private:
    std::string name;
    std::shared_ptr<SomeDataStructure> p;
};

std::string n{"name"};
Widget w;
w.setName(n);     // moves n into w! n's value now unknown
```

看起来`Widget::setName`是个只读操作，但因为里面调用了`std::move`，无条件地把`newName`转换为了右值，导致`n`变成一个不可预期的值。

有人会说`setName`不应该声明一个普适引用参数，因为普适引用不能带const。我们可以声明两个重载函数来代替上面的版本：

```cpp
class Widget {
public:
    void setName(const std::string& newName) {
        name = newName;
    }
    void setName(std::string&& newName) {
        name = std::move(newName);
    }
};
```

这样的代码能工作，但有缺点。

首先，需要维护的代码量变多。

其次，可能有运行时性能损失。考虑下面的调用：

```cpp
w.setName("Adela Novak");
```

在普适引用版本的`setName`中，"Adela Novak"会被传到`setName`中，直接用于构造`name`，中间没有临时`std::string`产生。而在重载版本的`setName`中，"Adela Novak"会先用于构造一个临时的`std::string`，再传给右值版本的`setName`，再通过`std::move`赋值给`name`，然后临时`std::string`析构，整个过程多了一次`std::string`的构造和析构。

在不同的场景下这种性能差异可能有很大区别，但总的来说普适引用版本有机会比重载版本有更小的开销。

重载版本的最大问题，是代码的扩展性太差。`setName`只有一个参数，只需要两个重载版本，那如果有N个普适引用参数的函数呢？我们需要2<sup>N</sup>个重载版本，这显然不现实。更不用说变长参数了。

有些场景中，你可能会用到右值引用或普适引用的一个特性：它本身是个左值。这样我们在不想移动它时，直接使用这个引用本身，而在最终想要移动它们时，再用`std::move`（对于右值引用）或`std::forward`（对于普适引用）去移动它们。

```cpp
template <typename T>
void setSignText(T&& text) {
    sign.setText(text);                          // use text, but don't modify it
    auto now = std::chrono::system_clock::now(); 
    signHistory.add(now, std::forward<T>(text)); // conditionally cast text to rvalue
}
```

如果上面的`text`类型是右值引用，就可以用`std::move`。参见[Item14](/2017/07/05/effective-modern-cpp-chapter3-iterm11-14/)，有些时候我们可能需要用`std::move_if_noexcept`来替代`std::move`。

如果有一个按值返回的函数，其返回的对象是右值引用或普适引用，那么也可以用`std::move`或`std::forward`来获得更好的性能：

```cpp
Matrix operator+(Matrix&& lhs, const Matrix& rhs) {
    lhs += rhs;
    return std::move(lhs);
}
```

如果上面我们写的是`return lhs;`，编译器发现`lhs`是个左值，返回类型是`Matrix`，好，来个复制……

如果`Matrix`不支持移动，用`std::move`也不会有副作用。等到`Matrix`支持移动了，上面的代码马上就能享受到性能的提升。

`std::forward`也有类似的用法：

```cpp
template <typename T>
Fraction reduceAndCopy(T&& frac) {
    frac.reduce();
    return std::forward<T>(frac);
}
```

对于按值返回的函数，如果返回的对象是个local对象，有些人可能会想到用`std::move`来避免复制：

```cpp
// original version
Widget makeWidget() {
    Widget w;
    ...
    return w;
}

// some smart version
Widget makeWidget() {
    Widget w;
    ...
    return std::move(w);
}
```

但这么做是错的！

有个概念叫"RVO"，即“返回值优化”，即编译器会在返回一个local对象时，如果函数的返回类型就是值类型，那么编译器可以直接将这个local对象构造在接收函数返回值的对象上，省掉中间的复制过程。换句话说，在RVO的帮助下，直接返回这个local对象要比返回它的右值还要节省。

C++98中RVO只是一种优化，编译器可做可不做，我们不能有太高的预期。但C++11标准规定了这种场景下，编译器要么应用RVO优化，彻底省掉这次复制，要么返回这个local对象的右值。因此在C++11后，如果编译器没有进行RVO，上面的第一种写法和第二种写法是等效的。

既然直接返回local对象不会比手动调用`std::move`差，还有很大概率更好一些，我们还有什么理由去手动move呢？

## Item26: 避免重载普适引用

整节都在讲重载普适引用带来的麻烦。麻烦的根源在于：根据C++的重载决议规则，普适引用版本总会被优先匹配。

```cpp
template <typename T>
void logAndAdd(T&& name) {
    auto now = std::chrono::system_clock::now();
    log(now, "logAndAdd");
    names.emplace(std::forward<T>(name));
}

std::string petName("Darla");
logAndAdd(petName);
logAndAdd(std::string("Persephone"));
logAndAdd("Patty Dog");
```

为了性能上的考虑，`logAndAdd`采用了普适引用作为参数类型，看起来还不错。然后我们添加一个重载版本：

```cpp
std::string nameFromIdx(int idx);
void logAndAdd(int idx) {
    auto now = std::chrono::system_clock::now();
    log(now, "logAndAdd");
    names.emplace(nameFromIdx(idx));    
}

std::string petName("Darla");
logAndAdd(petName);
logAndAdd(std::string("Persephone"));
logAndAdd("Patty Dog");

logAndAdd(22);
```

还是正常的。

```cpp
short nameIdx;
...
logAndAdd(nameIdx);  // error!
```

这次`logAndAdd`匹配到了普适引用版本，而不是`int`版本！

在这次重载决议中，`short`到普适引用是一次完美匹配，而`short`到`int`却是一次提升匹配，因此普适引用版本更优先。

通常来说普适引用版本在重载决议中的顺序都非常靠前，它们几乎能完美匹配所有类型（少数不能匹配的情况参见Item30）。这就是为什么重载普适引用大概率不好的原因。

在类的构造函数这里，情况变得更糟了：

```cpp
class Person {
public:
    template <typename T>
    explicit Person(T&& n)
    : name(std::forward<T>(n)) {}

    explicit Person(int idx)
    : name(nameFromIdx(idx)) {}
    ...
private:
    std::string name;
};
```

上面`logAndAdd`出现的问题在`Person`的构造函数中同样会出现。另外，根据[Item17](/2017/07/09/effective-modern-cpp-chapter3-iterm15-17/)，某个类有模板构造函数不会阻止编译器为它生成复制和移动构造函数，即使这个模板构造函数可以实例化为与复制或移动构造函数相同的样子。因此`Person`中的构造函数实际上有4个：

```cpp
class Person {
public:
    template <typename T>
    explicit Person(T&& n)
    : name(std::forward<T>(n)) {}

    explicit Person(int idx)
    : name(nameFromIdx(idx)) {}

    Person(const Person& rhs);
    Person(Person&& rhs);
    ...
private:
    std::string name;
};
```

这其中的匹配规则对正常人来说都很反直觉。比如：

```cpp
Person p("Nancy");
auto cloneOfP(p); // won't compile!
```

在`cloneOfP`的构造中，我们直觉上会认为调用的是`Person`的复制构造函数，但实际上匹配到的却是普适引用版本。

编译器的理由如下：`cloneOfP`的构造参数是一个非const左值`p`，这会实例化出一个非const左值参数的版本：

```cpp
class Person {
public:
    explicit Person(Person& n)
    : name(std::forward<Person&>(n)) {}

    explicit Person(int idx);

    Person(const Person& rhs);
    ...
};
```

`p`到复制构造函数的参数需要加一个const，而到`Person&`版本则是完美匹配。

假如我们将`p`改为const对象，即`const Person p("Nancy")`，那么情况又不一样了，这回模板参数变为`const Person&`：

```cpp
class Person {
public:
    explicit Person(const Person& n);

    Person(const Person& rhs);
    ...
};
```

我们得到了两个完全相同的完美匹配的重载版本，编译器无法决定用哪个，因此还是会报错。

在有继承的时候，情况更糟了：

```cpp
class SpecialPerson: public Persion {
public:
    SpecialPerson(const SpecialPerson& rhs)  // copy ctor: calls Person forwarding ctor!
    : Person(rhs)
    {...}

    SpecialPerson(SpecialPerson&& rhs)       // move ctor: calls Person forwarding ctor!
    : Person(rhs)
    {...}
};
```

`SpecialPerson`的两个构造函数都调用了`Person`的普适引用版本构造函数。原因是`rhs`的类型是`const SpecialPerson&`和`SpecialPerson&&`，到`const Persion&`和`Persion&&`总是要进行一次转换的，而到普适引用版本则还是完美匹配。

如果你真的想处理一些普适引用参数的特殊情况，该怎么办？看下节，有很多方法。

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