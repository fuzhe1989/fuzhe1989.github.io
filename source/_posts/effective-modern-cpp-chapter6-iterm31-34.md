---
title:      "Effective Modern C++ 笔记 Chapter 6: Lamba表达式 (Item 31-34)"
date:       2017-09-06 23:50:01
tags:
    - C++
    - Effective Modern C++
---

虽然lambda表达式只是C++11中的语法糖，但它对C++编程的影响是巨大的。没有lambda，STL中的"_if"算法（诸如`std::find_if`、`std::remove_if`、`std::count_if`等）通常局限于最平凡的谓语；但有了lambda，我们就可以方便地写出复杂的谓语来配合这些算法了。类似的例子也发生在需要比较函数的STL算法上，例如`std::sort`、`std::nth_element`、`std::lower_bound`等。STL之外，我们可以通过lambda快速地为`std::unique_ptr`和`std::shared_ptr`写出自定义的销毁器，为线程API的条件变量写出条件谓语。标准库之外，lambda也允许我们快速完成一个回调函数、接口适配函数，以及只在一处调用的上下文相关函数。

澄清两个名词：

* “lambda表达式”就是一个表达式，是下面代码中的加粗部分：

    > std::find_if(container.begin(), container.end(), **[](int val) { return 0 < val && val < 10; }**);
* closure(闭包)是通过lambda创建的一个运行时对象。根据不同的捕获模式，closure持有被捕获数据的拷贝或引用。在上面的例子中，在运行时我们通过lambda表达式创建了一个closure并作为第三个参数传给了`std::find_if`。
* closure class(闭包类)是一个closure的实现类。编译器会为每个lambda表达式生成一个唯一的closure class，lambda表达式中的代码会成为这个类的成员函数的可执行代码。

lambda通常用于一次使用的场景。但closure通常是可复制的，因此一个lambda表达式可能会对应着多个closure，这些closure的类型是相同的：

```cpp
int x;
...
auto c1 = [x](int y) { return x * y > 55; };
auto c2 = c1;
auto c3 = c2;
...
```

## Item31: 避免用默认捕获模式

C++11中有两种默认捕获模式：引用模式和值模式。默认的引用模式会导致孤悬引用。默认的值模式会让你以为自己可以避免这个问题（实际上没有），以为你的closure是自包含的（不一定）。

引用模式下closure会包含它所在作用域的局部变量和参数的引用，但如果这个closure的生命期长过这些局部变量和参数，它包含的这些引用就成了孤悬引用。一个例子：

```cpp
using FilterContainer = std::vector<std::function<bool(int)>>;
FilterContainer filters;

void addDivisorFilter() {
    auto calc1 = computeSomeValue1();
    auto calc2 = computeSomeValue2();
    auto divisor = computeDivisor(calc1, calc2);
    filters.emplace_back([&](int value) { return value % divisor == 0; }); // danger! ref to divisor will dangle!
}
```

如果显式捕获`divisor`的引用，问题仍然存在：

```cpp
filters.emplace_back([&divisor](int value) { return value % divisor == 0; });
```

但相比默认捕获模式，现在我们更容易发现这里的问题。

有时候我们知道一个closure只在当前作用域范围内使用，不会传播出去，是不是用默认捕获模式就是安全的呢？例如：

```cpp
template <typename C>
void workWithContainer(const C& container) {
    auto calc1 = computeSomeValue1();
    auto calc2 = computeSomeValue2();
    auto divisor = computeDivisor(calc1, calc2);

    using ContElemT = typename C::value_type;
    using std::begin;
    using std::end;

    if (std::all_of(
        begin(container), end(container),
        [&](const ContElemT& value) { return value % divisor == 0; })) {
        ...
    ) else {
        ...
    }
}
```

这段代码本身没什么问题，但你没办法保证不会有人把这段代码拷贝到其它地方，没注意这里有个默认的引用捕获，结果出现孤悬引用。

长期来看，显式列出引用捕获的变量更好。

题外话，C++14允许我们用`auto`来修饰lambda的参数，令代码更简洁：

```cpp
if (std::all_of(
    begin(container), end(container),
    [&](const auto& value) { return value % divisor == 0; }))
```

应对上面问题的一种方法是用默认的值捕获模式：

```cpp
filters.emplace_back([=](int value) { return value % divisor == 0; });
```

但这并不是解决孤悬引用的万能良药。如果你值模式捕获了一个指针，结果还是一样的。

有人会认为用智能指针就能避免这个问题。看这个例子：

```cpp
class Widget {
public:
    ...
    void addFilter() const;
private:
    int divisor;
};

void Widget::addFilter() const {
    filters.emplace_back([=](int value) { return value % divisor == 0; });
}
```

这段代码只能说大错特错。

捕获只会发生在lambda所在的作用域的非static的局部变量上（包括参数）。在`Widget::addFilter`中，`divisor`不是局部变量，它不能被捕获。如果把默认捕获去掉，直接用`[]`，代码就编译不过去了。如果我们显式写`[divisor]`，仍然编译不过去。

但上面这段代码为什么可以编译成功？因为它捕获了`this`。下面是它的等价代码：

```cpp
void Widget::addFilter() const {
    auto currentObjectPtr = this;
    filters.emplace_back(
        [currentObjectPtr](int value) {
            return value % currentObjectPtr->divisor == 0;
        }
    );
}
```

现在我们回过头来看智能指针的情况：

```cpp
using FilterContainer = std::vector<std::function<bool(int)>>;
FilterContainer filters;

void doSomeWork() {
    auto pw = std::make_unique<Widget>();
    pw->addFilter();
    ...
}
```

`std::unique_ptr`也改变不了我们捕获了一个孤悬的`this`指针的结局。

正确做法是什么？将成员变量拷贝一份为局部变量，再捕获进去：

```cpp
void Widget::addFilter() const {
    auto divisorCopy = divisor;
    filters.emplace_back(
        [divisorCopy] (int value) {
            return value % divisorCopy == 0;
        }
    );
}
```

在此基础上，如果你真的想用默认的值捕获模式，也可以接受。但为什么要冒这个险呢？如果不用默认捕获，我们早就可以发现`divisor`是成员变量不可捕获了。

C++14中，更好的方式是用泛型lambda捕获（见Item32）：

```cpp
void Widget::addFilter() const {
    filters.emplace_back(
        [divisor = divisor] (int value) {
            return value % divisor == 0;
        }
    );
}
```

默认值捕获模式的另一个缺点是它让我们以为closure是自包含的，但它却不能确保这点。因为closure不光依赖于局部变量，还会依赖静态存储区的对象。这些对象可以在lambda中使用，但无法被捕获：

```cpp
void addDivisorFilter() {
    static auto calc1 = computeSomeValue1();
    static auto calc2 = computeSomeValue2();
    static auto divisor = computeDivisor(calc1, calc2);
    filters.emplace_back([=](int value) { return value % divisor == 0; }); // captures nothing! refers to above static
    ++divisor;
}
```

粗心的读者会被`[=]`误导，以为所有变量都被捕获了。但实际上什么都没有被捕获。当调用`++divisor`时，`addDivisorFilter`创建的所有closure中的`divisor`都增加了。

这些问题通过显式捕获都可以提前发现，而用了默认捕获模式，却被藏了起来，等到上线时再boom。

## Item32: 使用初始化捕获来将对象移动到closure中

有时候我们想把一个对象移动到closure中，比如一个只能移动的对象（`std::unique_ptr`或`std::future`），或是移动的代价远小于复制的对象（比如大多数的STL容器），这个时候默认的引用捕获和值捕获都无法做到。C++14提供了一种方式，叫“初始化捕获”，能满足这一需求。C++11无法直接实现，但后面会介绍一种间接实现的方式。

C++标准委员会没有选择添加一种默认的移动捕获模式，而是增加“初始化捕获”，是因为后者的使用方式非常灵活，移动捕获只是它能做到的一件事情，事实上它几乎能做到其它捕获模式能做的所有事情。

初始化捕获能让你指定：

1. （closure class中）数据成员的名字。
2. 这个数据成员的初始化表达式。

一个例子：

```cpp
class Widget;
...
auto pw = std::make_unique<Widget>();
...                                     // confiture *pw
auto func = [pw = std::move(pw)] { return pw->isValidated() && pw->isArchived(); };
```

`pw = std::move(pw)`中，`=`左边的是数据成员的名字，它的作用域就是这个closure；右边是它的初始化式，它的作用域就是closure所在的作用域。

注意看有注释那行，如果在lambda前不需要修改`*pw`，就可以省掉这个变量，直接放到初始化捕获式中：

```cpp
auto func = [pw = std::make_unique<Widget>()] {...};
```

如果你想在C++11中实现移动捕获，该怎么做？

一种做法是自己把closure class写出来：

```cpp
class IsValAndArch {
public:
    using DataType = std::unique_ptr<Widget>;
    explicit IsValAndArch(DataType&& ptr): pw(std::move(ptr)) {}
    bool operator() const {
        return pw->isValidataed() && pw->isArchived();
    }
private:
    DataType pw;
};
auto func = IsValAndArch(std::make_unique<Widget>());
```

但这种方法需要写的代码太多了，有没有简单一点的呢？还真有，就是使用`std::bind`结合lambda，我们需要做的是：

1. 将对象移动的结果放到`std::bind`创建的函数对象中。
2. 令lambda接受上面这个“被捕获”的对象的引用。

初始化捕获版本：

```cpp
std::vector<double> data;
...
auto func = [data = std::move(data)] {...};
```

`std::bind`+lambda版本：

```cpp
std::vector<double> data;
...
auto func = std::bind(
    [](const std::vector<double>& data) {...},
    std::move(data)
);
```

函数对象中会保存每个参数的拷贝，我们使用了`std::move`，因此是移动生成了`data`的拷贝。之后lambda接受这个拷贝的const引用，就可以达到类似初始化捕获的效果了。

默认情况下closure class的`operator()`会被认为是const，因此我们在lambda中无法修改捕获的对象。这时我们可以给lambda添加上`mutable`标识符，令它可以修改捕获的对象：

```cpp
auto func = std::bind(
    [](std::vector<double>& data) mutable {...},
    std::move(data)
);
```

上面的第二个例子同样可以用`std::bind`+lambda实现：

```cpp
auto func = std::bind(
    [](const std::unique_ptr<Widget>& pw) {...},
    std::make_unique<Widget>()
);
```

## Item33: 在`auto&&`类型的参数上使用`decltype`从而进行完美转发

C++14的一项引入注目的新功能就是泛型lambda，即lambda的参数可以用`auto`来修饰。它的实现很直接：closure class的`operator()`是个模板函数。给定下面的lambda：

```cpp
auto f = [](auto x) {return normalize(x);};
```

对应的closure class的`operator()`为：

```cpp
class SomeClosureClass {
public:
    template <typename T>
    auto operator()(T x) const {
        return normalize(x);
    }
    ...
};
```

上面的例子中，如果`normalize`处理左值参数和右值参数的方式上有区别，那么我们写的还不算对，应该用上完美转发。这么需要对代码做两处修改：

1. `x`需要是一个普适引用。
2. `normalize`的实参要使用`std::forward`。

大致上代码需要改成这个样子：

```cpp
auto f = [](auto&& x) { return normalize(std::forward<???>(x)); };
```

这里的问题就是`std::forward`的实例化类型是什么。通常的完美转发我们能有一个模板参数`T`，但在泛型lambda中我们只有`auto`。closure class的模板函数中有这个`T`，但我们没办法用上它。

Item28解释了左值参数传给普适引用后变成左值引用，而右值参数传给普适引用后变成右值引用。我们要的就是这个效果，而这就是`decltype`能给我们的（参见Item3）。

Item28中同样解释了当右值参数传给普适引用后，我们得到的`T`是无引用的，而`delctype(x)`是带右值引用的，这会影响`std::forward`吗？

看`std::forward`的实现：

```cpp
template <typename T>
T&& forward(remove_reference_T<T>& param) {
    return static_cast<T&&>(param);
}
```

将`T`替换为`Widget`，得到：

```cpp
Widget&& forward(Widget& param) {
    return static_cast<Widget&&>(param);
}
```

将`T`替换为`Widget&&`，得到：

```cpp
Widget&& && forward(Widget& param) {
    return static_cast<Widget&& &&>(param);
}
```

应用引用折叠，得到：

```cpp
Widget&& forward(Widget& param) {
    return static_cast<Widget&&>(param);
}
```

与`T`为`Widget`的版本完全一样！这说明`decltype`就是我们想要的。

因此我们的完美转发版本的最终代码为：

```cpp
auto f = [](auto&& x) {
    return normalize(std::forward<decltype(x)(x)>);
};
```

C++14同样支持变长的泛型lambda：

```cpp
auto f = [](auto&&... xs) {
    return normalize(std::forward<decltype(xs)>(xs)...);
};
```

## Item34: 优先使用lambda而不是`std::bind`

`std::bind`实际上早在TR1时已经进入C++标准库，那时候它还是`std::tr1::bind`。总之很多人已经用了它很多年了，要放弃它并不容易。但C++11中，lambda在绝大多数场景中都要比`std::bind`好，而在C++14k，这种优势还在变大。

倾向于用lambda的最主要原因是lambda更易读。首先是背景代码：

```cpp
using Time = std::chrono::steady_clock::time_point;
enum class Sound {Beep, Siren, Whistle};
using Duration = std::chrono::steady_clock::duration;
void setAlarm(Time t, Sound s, Duration d);
```

lambda版本：

```cpp
auto setSoundL = [](Sound s) {
    using namespace std::chrono;
    setAlarm(steady_clock::now() + hours(1), s, seconds(30));
};
```

看起来就像是个非常正常的函数，其中我们很清晰的看到了`setAlarm`是如何被调用的。C++14中我们可以用一些字面值让代码更易懂：

```cpp
auto setSoundL = [](Sound s) {
    using namespace std::chrono;
    using namespace std::literals;
    setAlarm(steady_clock::now() + 1h, s, 30s);
};
```

`std::bind`版本：

```cpp
using namespace std::chrono;
using namespace std::literals;
using namespace std::placeholders; // needed for use of "_1"
auto setSoundB = std::bind(setAlarm, steady_clock::now() + 1h, _1, 30s);
```

一个不熟悉`std::bind`的用户可能很难发现`setAlarm`是在哪被调用的，`_1`看起来也很奇怪，更不好理解`std::bind`的第二个参数为什么是`setAlarm`的第一个参数。

上面说的都是可读性上的问题，但在正确性上`std::bind`版本也有些问题。在lambda版本中，我们知道`steady_clock::now() + 1h`是`setAlarm`的参数，它会在调用`setAlarm`时被求值。但在`std::bind`中，这个表达式是`std::bind`的参数，它是在我们生成bind对象时就被求值了，而此时我们还不知道什么时候才会调用`setAlarm`！

Fix方案就是把`std::bind`中的表达式继续用`std::bind`拆开，直到这个表达式的每项操作都是用`std::bind`表示的：

```cpp
auto setSoundB = std::bind(
    setAlarm,
    std::bind(
        std::plus<>(),
        std::bind(steady_clock::now),
        1h),
    _1,
    30s
);
```

如你所见，有点丑。题外话，这里的`std::plus<>`是C++14新增的语法，即标准操作符模板的模板类型可以省略。在C++11中，不支持这种语法，必须写成`std::plus<steady_clock::time_point>`。

如果`setAlarm`还有重载版本，新问题又产生了。假设另一个版本是：

```cpp
void setAlarm(Time t, Sound s, Duration d, Volume v);
```

对于lambda版本来说，工作正常，因为重载决议会选出正确的版本。而对于`std::bind`版本来说，编译会失败，编译器不知道该用哪个版本，它得到的只有一个函数名字，而这个名字本身是二义的。为了让`std::bind`能使用正确的版本，我们需要显式转换：

```cpp
using SetAlarm3ParamType = void (*)(Time, Sound, Duration);
auto setSoundB = std::bind(
    static_cast<SetAlarm3ParamType>(setAlarm),
    std::bind(
        std::plus<>(),
        std::bind(steady_clock::now),
        1h
    ),
    _1,
    30s
);
```

接下来是性能问题。`setSoundL`的背后是一个closure class，lambda函数体就是它的`operator()`函数，因此我们调用`setSoundL`就是在调用一个对象的定义体的函数，而我们知道这种函数是可以内联的。而`std::bind`的内部保存了`setAlarm`的函数指针，后面会用这个函数指针来调用`setAlarm`，这种调用方式很难有机会内联。这就产生了一些性能差异。（不知道这里为什么没有提lambda和`std::bind`在保存捕获的变量时的方式不同对性能的影响，如果捕获的都是栈上结构，lambda可以不涉及内存分配，而`std::bind`一定会有内存分配）

下一个差异在于我们用lambda可以很轻松写出临时用的短函数，而用`std::bind`就很困难：

```cpp
auto betweenL = [lowVal, highVal](const auto& val) {
    return lowVal <= val && val <= highVal;
};

auto betweenB = std::bind(
    std::logical_and<>(),
    std::bind(std::less_equal<>(), lowVal, _1),
    std::bind(std::less_equal<>(), _1, highVal)
);
```

C++11下两个版本要长一点：

```cpp
auto betweenL = [lowVal, highVal](int val) {
    return lowVal <= val && val <= highVal;
};

auto betweenB = std::bind(
    std::logical_and<bool>(),
    std::bind(std::less_equal<int>(), lowVal, _1),
    std::bind(std::less_equal<int>(), _1, highVal)
);
```

怎么看起来都是lambda版本更清爽。

接下来的差异是，我们很难搞清楚`std::bind`中参数是如何传递的。

```cpp
enum class CompLevel {Low, Normal, High};
Widget compress(const Widget& w, CompLevel lev);

Widget w;
using namespace std::placeholders;
auto compressRateB = std::bind(compress, w, _1);
```

上面这段代码中，为了把`w`传给`compress`，我们要把`w`保存到bind对象中，但它是怎么保存的？值还是引用？答案是值（可以用`std::ref`和`std::cref`来传引用），但知道答案的唯一方式就是熟悉`std::bind`是如何工作的，而在lambda中，变量的捕获方式是明明白白写在那的。

另一个问题是当我们调用bind对象时，它的参数是如何传给底层函数的？即`_1`是传值还是传引用？答案是传引用，因为`std::bind`会使用完美转发。

以上几种差异说明了C++14下lambda在各方面几乎完爆`std::bind`。但在C++11中，`std::bind`有两项本领是lambda做不到的：

* 移动捕获：参见Item32。
* 多态函数对象：`std::bind`会完美转发它的参数，因此它可以接受任意类型的参数，因此它可以绑定一个模板函数：

    ```cpp
    class PolyWidget {
    public:
        template<typename T>
        void operator()(const T& param) const;
        ...
    };

    PolyWidget pw;
    auto boundPW = std::bind(pw, _1);

    boundPW(1930); 
    boundPW(nullptr); 
    boundPW("Rosebud");
    ```

    C++11的lambda无法做到这点。但C++14的可以：

    ```cpp
    auto boundPW = [pw](const auto& param) {pw(param);};
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