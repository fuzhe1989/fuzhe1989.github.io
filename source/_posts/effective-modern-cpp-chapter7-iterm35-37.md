---
title:      "Effective Modern C++ 笔记 Chapter7: 并发API (Item 35-37)"
date:       2017-09-24 23:53:59
tags:
    - C++
    - Effective Modern C++
---

标准库中有两个future模板类：`std::future`和`std::shared_future`，它们的差别在很多场合中并不重要，因此下文中提到的future同时指这两个类型。

## Item35: 优先基于task编程，而不是基于thread

如果你想异步执行函数`doAsyncWork`，有两种选择。

1. 创建一个thread：

    ```cpp
    int doAsyncWork();
    std::thread t(doAsyncWork);
    ```
1. 或者创建一个task：

    ```cpp
    auto fut = std::async(doAsyncWork); // "fut" for "future"
    ```

task通常要比thread好，原因如下：

1. 基于task的代码往往更少。
1. 基于task更容易得到函数的返回值：调用future的get方法。
1. future的get方法还能拿到函数抛出的异常，而thread中如果函数抛了异常，进程就挂掉了。

它们之间更本质的差别在于，基于task的方法有着更高的抽象层次，而无需关心底层的线程管理。下面是C++中"线程"的三种不同层次的概念：

* 硬件线程：真正的运算线程，目前每个CPU核可以提供一个或多个线程。
* 软件线程（OS线程）：OS提供的线程，OS会负责管理和调度这些线程。通常OS线程可以远多于硬件线程。
* `std::thread`：C++标准库提供的线程类，底层对应一个OS线程。有些情况下`std::thread`没有对应的OS线程：刚刚构造好；已经调用过`join`；已经调用过`detach`。

OS线程数量是有上限的，超过上限时再创建会抛`std::system_error`，即使`doAsyncWork`是`noexcept`，调用`std::thread t(doAsyncWork)`也有可能抛这个异常。好的软件需要处理这样的异常，但怎么处理？
* 方法一：只在当前线程调用`doAsyncWork`，但这样就不是并发了。
* 方法二：等待其它线程结束，再创建新线程来执行`doAsyncWork`。但如果其它线程就在等待`doAsyncWork`被调用呢？

而且，即使OS线程数量没有到上限，创建过多的OS线程也会导致系统过载，大量资源消耗在线程调度和切换上。

避免系统过载不是一件容易的事情，我们很难确定OS线程与硬件线程间的合适比例，IO密集的线程与CPU密集的线程需要的比例差别很大。

但基于task来开发，把这些问题丢给task，就能简单一点。而`std::async`就是这么做的：

```cpp
auto fut = std::async(doAsyncWork);
```

用`std::async`就是把线程管理的难题交给了C++标准库，它会处理诸如out-of-threads的异常等问题。实际上`std::async`不一定会去创建线程，它允许调度器把这个函数安排在需要它结果（调用`fut.wait()`或`fut.get()`）的线程执行。

用了`std::async`后，负载均衡的问题仍然在，但现在需要处理它的不再是你了，而是调度器。调度器知道所有线程的情况，因此它处理负载均衡总会比人更好。

当然，`std::async`没办法解决前面GUI线程的问题，因为调度器不知道你的哪个线程对响应时间的要求最低。此时你可以指定`std::launch::async`来确保你的函数运行在另一个线程中。

最先进的线程调度器使用了一个系统级别的线程池来避免系统过载，它们通过work-stealing来平衡各硬件线程的负载。C++标准中没有规定要使用线程池或work-stealing，C++11的并发规格中也有一些内容令我们更难实现这样的线程调度器。但一些系统中已经包含了这些内容。当你使用这些系统时，如果你使用task，你就能轻松地用上这些技术，不用自己处理负载均衡、各种异常。

当然，还是有些场景中我们需要直接使用线程：

* 需要访问底层线程实现的API。`std::thread`允许你通过`native_handle`方法获得底层线程的句柄，而`std::future`没有这样的方法。
* 需要且能为你的应用优化线程使用，如在特定硬件平台上绕过某个已知的性能缺陷。
* 需要在C++的并发API之上实现自己的线程技术，如为特定平台实现一个线程池。

## Item36: 如果异步是必需的，就指定`std::launch::async`

`std::async`不保证你的函数一定是异步执行的，需要指定异步策略。有两个标准策略，都是`std::launch`中的枚举值。假设`f`是要通过`std::async`调用的函数：

* `std::launch::async`：`f`必须异步执行，比如在另一个线程。
* `std::launch::deferred`：`f`只在对应的future的`get`或`wait`被调用时才执行，且是同步执行。如果没有人调用对应的`get`或`wait`，`f`就不会被执行。

`std::async`的默认策略哪个都不是——是两个策略的or，即下面两个`std::async`的行为是完全一致的：

```cpp
auto fut1 = std::async(f);
auto fut2 = std::async(std::launch::async | std::launch::deferred, f);
```

默认策略允许调度器自己选择是在另一个线程执行还是在当前线程执行；是立即执行还是等到`get`或`wait`时执行。它有几个有趣的特性：

* 无法预测`f`是否与当前线程并发执行，因为调度器有可能选择`std::launch::deferred`。
* 无法预测`f`是否在调用`get`或`wait`的另一个线程执行。
* 可能无法预测`f`是否会执行。

默认策略不太适合与TLS（Thread Local Storage）一起用，因为你不知道`f`到底在哪个线程执行，因此也就不知道`f`中访问的TLS变量到底是哪个线程的TLS变量。它也会导致`wait`循环超时，因为对task调用`wait_for`或`wait_until`会导致它使用`std::launch::deferred`，进而导致下面这个看起来会结束的循环永远不结束：

```cpp
using namespace std::literals;

void f() {
    std::this_thread::sleep_for(1s);
}

auto fut = std::async(f);
while (fut.wait_for(100ms) != std::future_status::ready) {
    ...
}
```

如果`f`是在另一个线程执行的，上面的循环就没问题；但如果`f`是deferred，`fut.wait_for`就会一直返回`std::future_status::deferred`，导致循环永不结束。

这类bug很容易在开发和单元测试中被漏掉，因为调度器通常只在系统负载很高时采用deferred策略。

解决方案很简单：检查future是不是deferred，如果是，就不进循环。但我们没办法直接询问future是不是deferred，需要用`wait_for`来绕一下：

```cpp
auto fut = std::async(f);
if (fut.wait_for(0s) == std::future_status::deferred) {
    ...
} else {
    while (fut.wait_for(100ms) != std::future_status::ready) {
        ...
    }
    ...
}
```

上述场景的要点在于，当满足以下条件时，使用`std::async`的默认策略才是好的：

* task不需要与调用`get`或`wait`的线程并发执行。
* 无所谓访问哪个TLS变量。
* 要么能确保有人会调用future的`get`或`wait`，要么`f`执不执行都可以。
* 调用了`wait_for`或`wait_until`的代码要保证能处理deferred。

如果没办法保证以上几点，你需要确保你的task运行在另一个线程中，就指定`std::launch::async`：

```cpp
auto fut = std::async(std::launch::async, f);
```

我们可以自己包装一个函数，确保使用`std::launch::async`：

```cpp
template <typename F, typename... Ts>
inline std::future<typename std::result_of<F(Ts...)>::type> reallyAsync(F&& f, Ts&&... params) {
    return std::async(std::launch::async, std::forward<F>(f), std::foward<Ts>(params)...);
}
```

C++14版本可以不用写那么复杂的返回类型：

```cpp
template <typename F, typename... Ts>
inline auto reallyAsync(F&& f, Ts&&... params) {
    return std::async(std::launch::async, std::forward<F>(f), std::foward<Ts>(params)...);
}
```

## Item37: 令`std::thread`在所有路径下都不可join

每个`std::thread`对象都处于两种状态下：可join、不可join。可join的`std::thread`对应一个可运行或运行中的底层线程，例如被阻塞、未调度或已运行完成的线程都是可join的。

而其它状态的`std::thread`就是不可join的：

* 默认构造状态的`std::thread`：不对应底层线程。
* 被移动过的`std::thread`：底层线程现在由其它`std::thread`管理。
* 已调用过`join`的`std::thread`：底层线程已结束。
* 已调用过`detach`的`std::thread`：`detach`会切断`std::thread`和底层线程的联系。

重点来了，如果`std::thread`的析构函数被调用时它是可join的，程序就会终止。下面给一个例子：

```cpp
constexpr auto tenMillion = 10'000'000;
bool doWork(std::function<bool(int)> filter, int maxVal = tenMillion) {
    std::vector<int> goodVals;
    std::thread t([&filter, maxVal, &goodVals] {
        for (auto i = 0; i <= maxVal; ++i) {
            if (filter(i)) {
                goodVals.push_back(i);
            }
        }
    });
    auto nh = t.native_handle();              // use t's native handle to set t's priority
    ...
    if (conditionsAreSatisfied()) {
        t.join();
        performComputation(goodVals);         // computation was performed
        return true;
    }
    return false;                             // computation was not performed
}
```

这里我们直接构造`std::thread`而不用`std::async`的原因在于，我们需要拿到底层线程的句柄来设置优先级。

上面这段代码，如果最后走到了`false`分支，或中间抛了异常，就会遇到构造了一个可join的`std::thread`的问题，程序就会终止。可以改进的一点是在开始设置`t`为暂停状态，Item39会介绍如何做到这点。

回到这段代码中，为什么`std::thread`会有这个特性？原因在于另外两种处理方式会更糟：

* 隐式的`join`。本例中，`t`的析构会等其中的计算全部做完，可能合理，但也可能造成难以debug的性能问题。
* 隐式的`detach`。这样，`std::thread`对象和底层线程间的联系被切断，当`t`析构后，底层线程仍然在执行，可能会访问到已析构的`goodVals`，更加难以debug。

标准委员会认为析构一个可join的`std::thread`太可怕了，必须要禁止掉。因此你有责任确保所有情况下的`std::thread`都不可join。这可以通过包装一个RAII类来实现：

```cpp
class ThreadRAII {
public:
    enum class DtorAction {join, detach};
    
    ThreadRAII(std::thread&& t, DtorAction a)
        : action(a), t(std::move(t)) {}

    ~ThreadRAII() {
        if (t.joinable()) {
            if (action == DtorAction::join) {
                t.join();
            } else {
                t.detach();
            }
        }
    }

    std::thread& get() {return t;}
private:
    DtorAction action;
    std::thread t;
};
```

几个值得注意的点：

* 构造函数只接受`std::thread`的右值，因为`std::thread`只能移动不能复制。
* 构造函数的参数顺序与成员顺序相反，因为参数里把重要的放前面，不重要的放后面更符合直觉；而成员顺序里依赖少的放前面，依赖多的放后面更合理。`action`不如`t`重要，因此参数里放后面；没有任何依赖，因此成员中放前面。
* 提供一个`get`接口避免了为`ThreadRAII`实现一整套`std::thread`的接口。
* 在`ThreadRAII`的析构函数中，在调用`t.join()`或`t.detach()`前，需要先调用`t.joinable()`，因为有可能`t`已经被移动过了。这个析构函数中对`t`的访问是否有竞态？如果这里有竞态，即`ThreadRAII`析构时还有其它人在调用它底层的`std::thread`的成员函数，那么这种竞态不是`ThreadRAII`造成的，而是你的代码本身就有的。

应用`ThreadRAII`到我们前面的代码中：

```cpp
bool doWork(std::function<bool(int)> filter, int maxVal = tenMillion) {
    std::vector<int> goodVals;
    ThreadRAII t(
        std::thread([&filter, maxVal, &goodVals] {
            for (auto i = 0; i <= maxVals; ++i) {
                if (filter(i)) {
                    goodVals.push_back(i);
                }
            }
        }),
        ThreadRAII::DtorAction::join
    );
    auto nh = t.get().native_handle();
    ...
    if (conditionsAreSatisfied()) {
        t.get().join();
        performComputation(goodVals);
        return true;
    }
    return false;
}
```

当然`ThreadRAII`还是有可能阻塞的问题，也许这时候能打断这个线程会更好，但C++11没有提供这样的功能，这个主题也超出了本书的范围。

Item17解释了当一个类型定义了析构函数，编译器就不会自动为它生成移动函数了。如果你想让`ThreadRAII`可移动，就自己声明两个默认的移动函数。

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