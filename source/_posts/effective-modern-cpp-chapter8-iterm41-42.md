---
title:      "Effective Modern C++ 笔记 Chapter8: 杂项 (Item 41-42)"
date:       2017-10-26 23:24:18
tags:
    - C++
    - Effective Modern C++
---

本章介绍一种通用技术（传值调用）和一种通用特性（原地构造），它们都受到多种因素的影响，作者能给的建议只是“考虑用一下”，实践上要根据具体情况来定。

## Item41：对于可复制的、移动非常廉价、总是复制的参数，考虑调用时传值

有些函数参数就是要被复制的：

```cpp
class Widget {
public:
    void addName(const std::string& newName) {
        names.push_back(newName);
    }

    void addName(std::string&& newName) {
        names.push_back(std::move(newName));
    }
    ...
private:
    std::vector<std::string> names;
};
```

上面的两个函数一个处理左值，一个处理右值，但实际上它们的逻辑都是一样的，但我们写了两个函数，两个实现。

假如你想用普适引用来代替上面两个函数：

```cpp
class Widget {
public:
    template <typename T>
    void addName(T&& newName) {
        names.push_back(std::forward<T>(newName));
    }
    ...
};
```

代码省掉了一份，但又导致了其它问题。作为模板函数，`addName`需要放到头文件里。而且它不一定只有两个实例化版本（左值和右值），所有可以用于构造`std::string`的类型都可能会实例化一个版本（参见Item25）。同时，还有一些参数类型没办法使用普适引用（参见Item30）。如果调用方传递错类型，编译错误信息会非常恐怖（参见Item27）。

一种方法可以让我们只写一个函数，且没有普适引用的各种问题：参数直接传值，不使用引用：

```cpp
class Widget {
public:
    void addname(std::string newName) {
        names.push_back(std::move(newName));
    }
};
```

这个版本中，我们知道：

1. `newName`与实参没有关系，因此如何修改`newName`都不会影响到实参。
1. 这是`newName`最后一次被使用，因此移动它不会影响到后面程序的运行。

我们只需要写一个函数，因此：

1. 避免了代码重复，包括源代码和目标代码。
1. 没用普适引用，因此不会污染头文件，不会导致奇怪的运行失败或编译错误。

但它的开销如何？

当实参是左值时，实参到形参`newName`会有一次复制。但当实参是右值时，`newName`的构造会使用移动构造函数，因此它的构造开销是一次移动。后面构造`names`中的元素时，无论实参是左值还是右值，都是一次移动。

因此上面的方法中，当实参是左值时，开销是一次复制+一次移动；当实参是右值时，开销是两次移动。对比原来的重载版本，当实参是左值时，开销是一次复制；当实参是右值时，开销是一次移动。因此传值方法会比重载方法多一次移动的开销。

对于普适引用版本，情况有点复杂。当`T`是可以用于构造`std::string`时，普适引用在实参到形参中不会有对象构造，而是直接使用实参去构造`names`中的元素。本节我们不考虑这种情况，只假设实参是`std::string`，那么普适引用版本的开销与重载版本相同。

回头看一下标题，“对于可复制的、移动非常廉价、总是复制的参数，考虑调用时传值”：

1. 你只能是考虑要不要用传值方法。它确实有很多优点，但它也确实比其它版本多一次移动的开销。一些场景下（后面会讨论），这次开销不可忽视。
1. 只能对可复制的参数使用传值方法。对于只能移动的类型，我们只能移动构造形参，就不存在需要写两个重载版本的问题，也就不需要使用传值方法了：直接传右值引用多简单。
1. 传值方法只适用于“移动非常廉价”的类型。
1. 只有当参数的复制不可避免时，才需要考虑传值方法。假如有某个分支下我们不需要复制参数，那么重载版本就不需要复制参数，而传值版本在调用那一刻已经复制完了，没办法省掉。

    ```cpp
    class Widget {
    public:
        void addName(std::string newName) {
            if ((newName.length() >= minLen) && (newName.length() <= maxLen)) {
                names.push_back(std::move(newName));
            }
        }
        ...
    };
    ```

即使上面三个条件都满足，也有场景不适用于传值方法。我们说复制时，不光是复制构造，还有复制赋值。考虑到这点，开销分析就更复杂了。

```cpp
class Password {
public:
    explicit Password(std::string pwd)
        : text(std::move(pwd)) {}

    void changeTo(std::string newPwd) {
        text = std::move(newPwd);
    }
};
```

构造`Password`显然是可以用传值方法的：

```cpp
std::string initPwd("Supercalifragilisticexpialidocious");
Password p(initPwd);
```

但在调用`changeTo`时：

```cpp
std::string newPassword = "Beware the Jabberwock";
p.changeTo(newPassword);
```

`newPassword`是左值，因此`newPwd`要进行复制构造，这里会分配内存。之后`newPwd`移动赋值给`text`时，`text`会释放自己原有的内存块，转而使用`newPwd`持有的内存块。因此`changeTo`有两次内存操作，一次分配，一次释放。

但我们这个例子中，旧密码比新密码长，因此如果我们使用重载方法，就不会有内存分配或释放（直接复制到旧密码的内存块上）：

```cpp
void Password::ChangeTo(const std::string& newPwd) {
    text = newPwd;
}
```

因此在这个例子中，传值版本比重载版本的开销多了两次内存操作，很可能比字符串的移动开销大一个数量级。

但在旧密码比新密码短的例子中，重载版本也没办法避免掉两次内存操作，这时传值方法的优势又回来了。

以上分析只针对实参为左值的情况，当实参为右值时，移动总是更好的。

由此可以看出，当有赋值时，可能影响结论的因素太多了，比如`Password`这个例子中`std::string`是否使用了SSO优化也会影响我们的结论。

实践中通常采用“有罪推定”原则，即优先使用重载方法或普适引用方法，直到传值方法显示出它的优势。对于那些对性能有极致要求的软件，传值方法就不太合适了。首先，多出的一次移动的开销可能很重要；其次，很难确定到底多了几次移动。假设我们构造链路上有N层，有可能每层的构造都使用了传值方法，看起来简单的一次构造实际上多了N次移动的开销。而且我们还很难发现这件事。

传值方法的另一个问题，是当有继承的时候，传值可能引发“切片问题”。当形参是基类而实参是派生类型时，实参到形参的构造会丢掉派生类型多出的部分，最终只得到一个基类对象。而传引用就不会有这个问题。这也是C++98中传值不被接受的一个原因。

## Item42: 考虑用原地构造替代插入

假设我们有一个容器，元素类型是`std::string`。当我们向这个容器插入一个新元素时，新元素的类型是什么？直觉告诉我们，新元素的类型就是`std::string`。

但直觉不总是对的。看下面的代码：

```cpp
std::vector<std::string> vs;
vs.push_back("xyzzy");
```

这里，我们插入的新元素类型不是`std::string`，而是`char[6]`或`char*`。`std::vector`有两个版本的`push_back`：

```cpp
template <typename T, class Allocator = allocator<T>>
class vector {
public:
    ...
    void push_back(const T& x);
    void push_back(T&& x);
    ...
};
```

当编译器发现实参类型与形参类型不匹配时，它会生成一些代码，构造一个临时的`std::string`对象，效果类似于：

```cpp
vs.push_back(std::string("xyzzy"));
```

整个过程为：

1. 构造临时对象。
1. `std::vector`分配空间给新元素。
1. 将临时对象复制到新的空间上。
1. 析构临时对象。

于是这里偷偷的多了一次对象的构造和析构。另外还有一次临时对象的复制。当我们很关心性能时，这些额外开销是不可忽视的。

C++11新增的`emplace_back`方法就可以避免这个问题：

```cpp
vs.emplace_back("xyzzy");
vs.emplace_back(50, 'x');
```

它会先分配空间，再在新空间上使用传入参数直接构造出`std::string`。每个支持`push_back`的容器也都支持`emplace_back`，支持`push_front`的容器也都支持`emplace_front`，支持`insert`的容器也都支持`emplace`。

一般来说insert和emplace的效果是完全相同的，同时`emplace`还省掉了临时对象的构造和析构，那么还有什么情况下我们不用emplace呢？

目前的C++标准库实现中，既有emplace比insert快的场景，也有emplace比insert慢的场景。这些场景很难列举，取决于传入的参数类型、使用的容器、新元素所处的位置、元素的构造函数的异常安全性，以及对于map和set类容器，要插入的元素是否已经存在等因素。因此在决定使用insert还是emplace前，先测一下性能。

当然也有些启发式的方法来判断emplace适用于哪些场景。以下条件如果为真，emplace就很可能比insert性能更好：

* 新元素在容器内直接构造，而不是先构造再赋值。

    在前面的例子中，我们要在`vs`的尾部新增一个元素，显然这里之前不存在对象，我们只能构造一个对象。emplace此时就比较有优势。但下面这个例子中：

    ```cpp
    std::vector<std::string> vs;
    ...
    vs.emplace(vs.begin(), "xyzzy");
    ```

    我们要在`vs`的头部新增一个对象。大多数实现会先用`""xyzzy"`构造出一个临时的`std::string`，再移动赋值给目标对象。这样emplace相比insert的优势就没有了。

    当然，这取决于我们用的实现。但此时启发式方法还是有用的。理论上基于节点的容器都会构造新元素，而大多数STL容器都是基于节点的。只有几个容器不基于节点：`std::vector`、`std::deque`、`std::string`（`std::array`基于节点，但它没有emplace和insert类的方法）。当你明确知道新元素会被构造出来时，就可以毫不犹豫的使用emplace。这三个容器的`emplace_back`都是推荐用的，对于`std::deque`来说，`emplace_front`也推荐使用。
* 实参类型与容器的元素类型不同。（解释略）
* 容器不会因重复元素而拒绝插入。这里说的是对于`std::set`或`std::map`这样的容器，在插入时需要比较，那么就需要把实参先构造为一个临时对象。这样emplace的优势就没有了。

下面两次调用就满足上面的条件：

```cpp
vs.emplace_back("xyzzy");
vs.emplace_back(50, 'x');
```

在决定使用emplace后，有两个问题值得考虑。第一个是资源管理的问题。假设你有一个容器：

```cpp
std::list<std::shared_ptr<Widget>> ptrs;
```

`Widget`需要的自定义销毁函数是：

```cpp
void killWidget(Widget* pWidget);
```

根据Item21，这种情况下我们没办法用`std::make_shared`了。insert版本是：

```cpp
ptrs.push_back(std::shared_ptr<Widget>(new Widget, killWidget));
```

或：

```cpp
ptrs.push_back({new Widget, killWidget});
```

无论哪种情况，都要构造出一个临时对象。这不就是emplace能避免的吗？

```cpp
ptrs.emplace_back(new Widget, killWidget);
```

但注意，临时对象带来的好处远比它的构造和析构成本要大得多。考虑一种情况：

1. 我们构造了一个临时对象`temp`，持有`new Widget`的结果。
1. 容器扩张时抛了个异常。
1. 异常传播到外层，`temp`被销毁，`Widget*`被释放。

而emplace版本则是：

1. `new Widget`的结果，一个裸指针，传进了`emplace_back`函数内。
1. 容器扩张时抛了个异常。
1. 没有智能指针持有前面的裸指针，内存泄漏。

类似的问题也会出现在每个RAII类中。将裸指针（或其它未受保护的资源）通过完美转发的方式传递进emplace函数后，在RAII对象构造之前，有个窗口期。正确的方式是：

```cpp
std::shared_ptr<Widget> spw(new Widget, killWidget); 
ptrs.push_back(std::move(spw));
```

或：

```cpp
std::shared_ptr<Widget> spw(new Widget, killWidget);
ptrs.emplace_back(std::move(spw));
```

无论哪种方式都要先构造对象，此时emplace和insert就没什么区别了。

第二个问题是emplace与`explicit`构造函数的相互作用。想象你有一个正则表达式的容器：

```cpp
std::vector<std::regex> regexes;
```

有一天你写了这么一行代码：

```cpp
regexes.emplace_back(nullptr);
```

然后编译器居然不报错！`nullptr`怎么可能是正则表达式呢？

`std::regex r = nullptr`是没办法编译通过的。而`regexes.push_back(nullptr)`也是非法的。

问题在于`std::regex`有一个接受`const char*`的析构函数：

```cpp
std::regex upperCaseWord("[A-Z]+");
```

但它是`explicit`的，因此下面这么用会报错：

```cpp
std::regex r = nullptr;
regexes.push_back(nullptr);
```

但显式调用构造函数是可以的：

```cpp
std::regex r(nullptr);
```

不幸的是emplace函数中就是这么构造对象的，能编译，但运行结果未定义。

下面两种很类似的构造方式，但结果不同：

```cpp
std::regex r1 = nullptr;          // Error
std::regex r2(nullptr);           // OK
```

第一种是复制初始化，第二种是直接初始化。复制初始化不允许使用`explicit`构造函数，而直接初始化则可以。emplace函数中执行的是对象的直接初始化，而insert函数中则是复制初始化。

因此当你使用emplace的时候，注意看一下你传递的类型对不对，因为它会在你没注意到的时候绕开`explicit`的限制，然后制造一个大新闻。

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