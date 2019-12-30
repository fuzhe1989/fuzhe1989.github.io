---
title:      "Effective Modern C++ 笔记 Chapter 7: 并发API (Item38-40)"
date:       2017-10-09 22:50:11
tags:
    - C++
    - Effective Modern C++
---

## Item38: 知道线程句柄析构时的各种行为

上节介绍了可join的`std::thread`对应一个可运行的底层线程，而未推迟的future也可能对应一个OS线程。这里把它们都称为OS线程的句柄。

很有趣的是，`std::thread`和future在析构的行为上非常不同。析构一个可join的`std::thread`会导致程序终止，而析构一个future有时像是做了隐式的`join`，有时像是做了隐式的`detach`，有时两者都不是。总之它不会导致程序终止。

我们首先从这样一个发现开始：future就是执行者将结果返回给调用者的一个管道。执行者（通常是异步执行）将计算结果写到管道中（例如`std::promise`对象），调用者再通过future拿到结果。

```
----------                                          ----------
| Caller | <--- future ------------ std::promise ---| Callee |
----------                                          ----------
```

但结果保存在哪了？执行者可能在调用者调用`get`之前就结束了，所以结果一定不会在执行者对应的`std::promise`对象中，它会在执行者结束时析构掉。

结果也不可能在future对象中，因为`std::future`有可能用来创建`std::shared_future`，然后被复制很多遍。如果结果在future中，那么结果也会被复制很多遍。我们知道有些结果类型是不能复制的，因此不可能在future对象中。

那么答案就是结果保存在`std::promise`和future之外，且需要是可共享状态。C++标准中没有规定结果的类型，编译器可以自行实现。

```
----------                ------------------                    ----------
| Caller | <--- future ---| Result(Shared) |--- std::promise ---| Callee |
----------                ------------------                    ----------
```

与future关联的这个共享状态就决定了future的析构行为，尤其：

* 对于通过`std::async`启动的未推迟的task，最后一个与之关联的future在析构时会阻塞，直到这个task完成。本质上，这个析构就是对task所在的线程调用了一次join。
* 其它future的析构都只是简单的析构这个对象。这些析构就是对底层线程调用了detach。对于被推迟的task，当它关联的最后一个future析构后，这个task就永远不会被执行了。

简单来说就是有一种正常行为和一个例外。正常行为就是future的析构只析构future对象，它既不会join也不会detach。而当以下条件都满足时，应用例外规则：

* future关联着由`std::async`创建的共享状态。
* task的启动策略是`std::launch::async`，包括调用`std::async`时显式指定该策略，也包括调用者使用了默认策略，而系统选择了该策略。
* 它是最后一个关联共享状态的future。

以上条件都满足时，future的析构会对底层线程调用join。

为什么对于由`std::async`启动的未推迟的task会有这个例外？就我（Scott Meyers）所知，C++标准委员会想避免隐式detach引起的问题（见Item37），但又不想像对待可join的`std::thread`那样使用“程序终止”这么激进的策略，所以最终他们妥协了，决定隐式使用join。这个决定并不是毫无争议，一直有声音想在C++14中将这个行为废弃掉，但最终它还是保留了下来。

future的API上没办法知道它是不是关联一个共享状态，因此没办法知道随便一个future的析构会不会阻塞。这导致了一些有趣的潜在状况：

```cpp
std::vector<std::future<void>> futs; // 析构时可能阻塞
class Widget {                       // 析构时可能阻塞
public:
    ...
private:
    std::shared_future<double> fut;
};
```

当然，如果你知道某个future肯定不满足例外条件，你就能确定它的析构不会阻塞。例如，当我们使用`std::packaged_task`时，它返回的future就不与`std::async`创建的共享状态相关联，因此我们可以确定这样的future的析构是不会阻塞的。

> `std::packaged_task`与`std::function`类似，都是对某个callable的对象的包装。区别在于`std::packaged_task`会返回一个future。我们可以用`std::packaged_task`创建一个`std::thread`来运行callable对象，结果通过future得到。

当然`std::packaged_task`也可以通过`std::async`来运行，但这样就没有理由用`std::packaged_task`了，直接用`std::async`更方便。

```cpp
{
    std::packaged_task<int()> pt(calcValue);
    auto fut = pt.get_future();
    std::thread t(std::move(pt));
    ...                                        // see below
}
```

“...”中对`t`可能的三种操作：

* 没有对`t`作任何操作。这样结束时`t`还是可join的，导致程序终止。
* 对`t`调用了join。这样`fut`析构时就不需要阻塞了。
* 对`t`调用了detach。这样`fut`析构时也不需要调用detach了。

结论就是，对于由`std::packaged_task`得到的future，你不需要怎么关心它的析构行为。

## Item39: 考虑用一个void future来进行只运行一次的事件通信

当需要进行事件通信时，一种显然的方式就是通过条件变量：

```cpp
std::condition_variable cv;
std::mutex m;
```

通知方的代码很简单：

```cpp
...
cv.notify_one();
```

接收方的代码就有点复杂了：

```cpp
...
{
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk);
}
...
```

上面的代码并不正确。首先，这种方法的味道（code smell）不好，就表示可能藏bug。这里的第一个问题在于，是否有必要使用`std::mutex`。`std::mutex`是用来保护共享状态的，但很有可能通知方和接收方并没有共享什么东西，那么这个`std::mutex`就只是为了条件变量才构造的，逻辑上并不需要它。

即使用到了`std::mutex`，仍然有两个问题：

* 条件变量不能保存通知状态，因此如果通知方在接收方开始`wait`前就调用了`notify_one`，接收方就会hang在那。
* 条件变量的`wait`有可能在条件并不满足（未通知）时结束（假醒），因此需要有办法知道我们等待的条件是否真的满足了。接收方自己当然没办法知道这个事情（否则它就不需要条件变量了）。

以上两个问题的一种解法是使用一个共享状态，而不是条件变量：

```cpp
std::atmoic<bool> flag(false);
// 通知方
...
flag = true;
// 接收方
...
while (!flag) {
    ...
}
```

这种方法的好处是不需要mutex，能保存通知状态，不需要处理假醒。但它的问题在于接收方在阻塞时需要不停的查询状态，CPU开销很大。

可以把共享状态与条件变量结合起来使用：

```cpp
std::condition_variable cv;
std::mutex m;
bool flag(false);                   // not std::atomic
// 通知方
...
{
    std::lock_guard<std::mutex> g(m);
    flag = true;
}
cv.notify_one();
// 接收方
...
{
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [] { return flag; }); // use lambda to avoid spurious wakeups
    ...
}
...
```

这种方法避免了我们讲到的几个问题，但它的设计不好：很多共享变量；两套机制来做同一件事；逻辑暴露在外，容易写错。简单来说就是这种方法不够干净。

一种更干净的方法是接收方等待一个future，而通知方通过给这个future赋值来进行通知。Item38提到了`std::promise`代表了一个通信通道的发送端，而future则代表了接收端。这样的通道可以用于任何需要通信的场合。

方案很简单，通知方要持有一个`std::promise`对象，而接收方持有对应的future。通知方通过调用`std::promise`的`set_value`来写入一条消息，接收方通过future的`wait`来等待消息。

无论是`std::promise`、`std::future`还是`std::shared_future`都是模板类型，需要一个类型参数，也就是消息的类型。但我们只关心通知本身，不需要消息有类型，最合适的就是`void`。

```cpp
std::promise<void> p;
// 通知方
...
p.set_value();
// 接收方
...
p.get_future().wait();
```

没有mutex，没有条件变量，没有共享状态，没有假醒，是不是很完美？不完全是。Item38提到`std::promise`背后实际上有一个共享状态，就意味着它的构造包含着一次内存分配和释放的开销。

而且，`std::promise`只能**赋值一次**。重复对`std::promise`的赋值是没有意义的，接收方感知不到。因此这条通信通道只能使用一次，这是与前述方案最大的区别。条件变量和共享状态总是可以重复利用的。

“只有一次”的通信有时候也是很有用的。举个例子，有时候我们想创建一个被暂停的线程，需要等待一个事件后才开始工作，就可以用`std::promise`来实现：

```cpp
std::promise<void> p;

void react();
void detect() {
    std::thread t([] {
        p.get_future().wait();
        react();
    });
    ...
    p.set_value();
    ...
}
```

这里我们创建了一个暂停的线程，它会等待`p`被赋值后才执行`react`。需要一个暂停线程的地方很多，比如我们想避免线程创建的成本，比如想在线程真正工作前设置一下优先级什么的（`std::thread`没有这种接口，但我们可以通过`native_handle`获得OS线程的句柄来做这样的事情）。

Item37中提到用`ThreadRAII`类来代替直接的`std::thread`会更好，我们改写一下：

```cpp
std::promise<void> p;

void react();
void detect() {
    ThreadRAII tr(
        std::thread([] {
            p.get_future().wait();
            react();
        }),
        ThreadRAII::DtorAction::join
    );
    ...                              // 注意这里
    p.set_value();
    ...
}
```

上面的代码有个问题：第一个“...”处如果抛了异常，`p`还没有赋值，因此`tr`还在阻塞中，因此它是可join的，因此`tr`析构会导致程序终止。

这个问题有很多解法，这里就不赘述了。最后说一下如何把上面的通信过程由一对一改成一对多，即一个通知方唤醒多个接收方：把`std::future`换成`std::shared_future`即可。

```cpp
std::promise<void> p;

void react();
void detect() {
    auto sf = p.get_future().share();   // std::shard_future
    std::vector<std::thread> vt;
    for (int i = 0; i < threadForRun; ++i) {
        vt.emplace_back(
            std::thread([sf] {          // 必须值捕获
                sf.wait();
                react();
            })
        );
    }
    ...                              
    p.set_value();
    ...
    for (auto& t: vt) {
        t.join();
    }
}
```

注意各个线程持有的`std::shared_future`必须是值，不能是引用。

## Item40: 使用`std::atomic`应对并发，而用`volatile`访问特殊内存

本节内容不重复了，大家都比较熟悉。简单列一下结论：

1. `std::atomic`是真正的原子操作，用于并发，但不能用于访问特殊内存（如硬件资源）。
1. `volatile`在某些语言中可以用于并发，但在C++中不能用于并发，它不保证原子的读写，也不保证指令的先后顺序。它的用途是访问上面说的特殊内存。
1. 可以结合起来，用于需要并发访问的特殊内存：`volatile std::atomic<int> vai`。
1. 访问`std::atomic`要比访问普通变量慢得多，它的内存屏障也会限制编译器的指令重排等优化，因此不要滥用`std::atomic`。

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