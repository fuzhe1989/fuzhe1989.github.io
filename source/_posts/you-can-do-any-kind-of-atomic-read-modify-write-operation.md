---
title:      "翻译：You Can Do Any Kind of Atomic Read-Modify-Write Operation"
date:       2015-05-06 14:31:05
tags:
    - C++
    - atomic
    - 翻译
---

原文地址：[You Can Do Any Kind of Atomic Read-Modify-Write Operation](http://preshing.com/20150402/you-can-do-any-kind-of-atomic-read-modify-write-operation/)

<!--more-->

原子的read-modify-write操作——简称RMW——要比[原子的load/store](http://preshing.com/20130618/atomic-vs-non-atomic-operations)更复杂。RMW允许读一块共享数据的同时还原地修改它的值。C++11的atomic库中的下列函数都属于RMW操作：

```cpp
std::atomic<>::fetch_add()
std::atomic<>::fetch_sub()
std::atomic<>::fetch_and()
std::atomic<>::fetch_or()
std::atomic<>::fetch_xor()
std::atomic<>::exchange()
std::atomic<>::compare_exchange_strong()
std::atomic<>::compare_exchange_weak()
```

以`fetch_add`为例，它首先读取一个共享变量的当前值，对这个值做加法，再将新的值写回去——这3步是原子完成的。你可以用锁来完成同样的操作，但这就不是[无锁](http://preshing.com/20120612/an-introduction-to-lock-free-programming)的了。而RMW操作则天生就被设计为是无锁的。RMW操作可以受益于任何CPU的无锁指令，如ARMv7的`ldrex`/`strex`。

初学者看完上面的函数列表后可能会问：“为什么C++11只提供了这么少的RMW操作？为什么有`fetch_add`却没有`fetch_multiply`、`fetch_divide`或`fetch_shift_left`？”

原因有两点：

1. 实践中对这几个RMW操作的需求非常少。不要对RMW的使用方式产生错误的印象，将一个单线程算法中每一步都换成RMW操作是得不到多线程安全的代码的。
1. 这些操作实现起来很简单，需要的时候自己实现一下就好。就像本文标题说的，你可以做任何RMW操作！

## Compare-and-Swap：所有RMW操作的基础
C++11所有可用的RMW操作之中，最基础的就是`compare_exchange_weak`，其它的RMW操作都是用它实现的。`compare_exchange_weak `最少需要两个参数：

```cpp
shared.compare_exchange_weak(T& expected, T desired, ...);
```

它会在`shared`的当前值与`expected`相等时将`shared`替换为`desired`。如果替换成功，返回`true`；如果失败，它会将`shared`的当前值保存到`expected`中——无视`expected`的名字，它其实是一个in/out参数。

以上过程被称为**compare-and-swap**操作，整个过程就是一次原子操作。

![](http://7xipsa.com1.z0.glb.clouddn.com/compare-exchange.png)

假设你确实需要原子的`fetch_multiply`操作，下面是一种实现方法：

```cpp
uint32_t fetch_multiply(std::atomic<uint32_t>& shared, uint32_t multiplier)
{
    uint32_t oldValue = shared.load();
    while (!shared.compare_exchange_weak(oldValue, oldValue * multiplier))
    {
    }
    return oldValue;
}
```

上面这个循环被称为compare-and-swap循环，或**CAS循环**。函数里反复尝试将`oldValue`替换为`oldValue * multiplier`，直至替换成功。如果没有其它线程同时也在做修改，`compare_exchange_weak`一般第一次尝试就会成功。另一方面，如果有其它线程同时在修改`shared`，它的值就有可能在`load`和`compare_exchange_weak`之间被改变，就会导致CAS操作失败。这种情况下`oldValue`会更新为`shared`的最新值，循环继续。

![](http://7xipsa.com1.z0.glb.clouddn.com/fetch-multiply-timeline.png)

上面的`fetch_multiply`既是原子的，又是无锁的。原子性是因为：尽管CAS循环可能会重复不确定次，但最后的修改动作是原子的。无锁是因为：如果CAS循环的某次迭代失败了，往往是由于另一个线程修改成功了。最后这个结论取决于一个假设：`compare_exchange_weak`会被编译为无锁的机器码——细节见下文。它也忽略了一个事实：在某些平台上`compare_exchange_weak`可能会[假失败](http://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)，但这种情况很罕见，不需要考虑使用锁的算法。

## 可以将多个步骤组合为一次RMW
`fetch_modify`只是将`shared`的值替换为它的倍数。如何实现更复杂的RMW操作？还能保证新操作是原子的**且**无锁吗？当然能。我们举一个有点绕的例子。有一个函数，它会读一个变量的值，如果是奇数就减1，偶数就除以2，如果新值大于等于10则写回去，整个过程是一个原子且无锁的操作：

```cpp
uint32_t atomicDecrementOrHalveWithLimit(std::atomic<uint32_t>& shared)
{
    uint32_t oldValue = shared.load();
    uint32_t newValue;
    do
    {
        if (oldValue % 2 == 1)
            newValue = oldValue - 1;
        else
            newValue = oldValue / 2;
        if (newValue < 10)
            break;
    }
    while (!shared.compare_exchange_weak(oldValue, newValue));
    return oldValue;
}
```

思路和上节一样：如果`compare_exchange_weak`失败——通常是因为其它线程的并发修改——就将`oldValue`更新为最新值，再重试。如果中间某步我们发现`newValue`小于10，就提前结束循环。

重点是你可以在CAS循环中放任何操作。想像CAS循环体就是一段临界区。通常我们用mutex来保护临界区，而在CAS循环中我们只是简单的重试整个事务，直到成功。

上面只是一个生造的例子。一个更实际的例子见我在[关于信号量的早期文章](http://preshing.com/20150316/semaphores-are-surprisingly-versatile)中描述的`AutoResetEvent`类。它用了包含多步的CAS循环将一个共享变量自增至1。

## 可以将多个变量组合为一次RMW
说了这么多，我们只看到了如何对一个共享变量进行原子操作。怎么用一个原子操作修改多个变量？一般我们都用mutex来保护：

```cpp
std::mutex mutex;
uint32_t x;
uint32_t y;

void atomicFibonacciStep()
{
    std::lock_guard<std::mutex> lock(mutex);
    int t = y;
    y = x + y;
    x = t;
}
```

这种基于mutex的方法是原子的，但显然不是无锁的。当然这[可能足够好了](http://preshing.com/20111118/locks-arent-slow-lock-contention-is)，但出于演示的目的，我们再进一步，将它改写为一个CAS循环。`std::atomic<>`是一个模板，所以我们可以将两个变量打包为一个`struct`，再应用之前的模式：

```cpp
struct Terms
{
    uint32_t x;
    uint32_t y;
};

std::atomic<Terms> terms;

void atomicFibonacciStep()
{
    Terms oldTerms = terms.load();
    Terms newTerms;
    do
    {
        newTerms.x = oldTerms.y;
        newTerms.y = oldTerms.x + oldTerms.y;
    }
    while (!terms.compare_exchange_weak(oldTerms, newTerms));
}
```

对`Terms`的操作是无锁的吗？接下来我们要进入不确定的领域了。在我开始写这篇文章时，C++11的原子操作被设计为**尽可能**使用无锁的CPU指令——明显是一个很宽泛的定义。在我们的场景中，我们包装在`std::atomic<>`中的是一个结构体，`Term`。我们来看看GCC4.9.2是如何将它编译成x64代码的：

![](http://preshing.com/images/atomic-terms-rmw.png)

很幸运，编译器很聪明，知道`Terms`刚好可以放到一个64位寄存器中，于是使用`lock cmpxchg`实现的`compare_exchange_weak`。编译出来的代码是无锁的。

这里引出了一件有意思的事：一般而言，C++11标准**没有**承诺原子操作是无锁的。编译器要支持太多的CPU架构，而且有太多实例化`std::atomic<>`模板的方法了。你需要[检查你的编译器](http://en.cppreference.com/w/cpp/atomic/atomic/is_lock_free)来确认这件事。但在实践中如果下面的条件都满足，你可以放心假设原子操作就是无锁的：

1. 编译器是最新版的MSVC、GCC或Clang。
1. 目标处理器是x86、x64或ARMv7（等等）。
1. 模板参数是`uint32_t`或`uint64_t`或`T*`。

我个人喜欢保证第3点，限制自己只用整型或指针类型去特化`std::atomic<>`。使用我之前文章提到的[安全位域技术](http://preshing.com/20150324/safe-bitfields-in-cpp)可以用`std::atomic<uint64_t>`方便的改写上面的函数：

```cpp
BEGIN_BITFIELD_TYPE(Terms, uint64_t)
    ADD_BITFIELD_MEMBER(x, 0, 32)
    ADD_BITFIELD_MEMBER(y, 32, 32)
END_BITFIELD_TYPE()

std::atomic<uint64_t> terms;

void atomicFibonacciStep()
{
    Terms oldTerms = terms.load();
    Terms newTerms;
    do
    {
        newTerms.x = oldTerms.y;
        newTerms.y = (uint32_t) (oldTerms.x + oldTerms.y);
    }
    while (!terms.compare_exchange_weak(oldTerms, newTerms));
}
```

一些将多个值打包为一个原子位域的真实例子：

* 实现带tag的指针来[绕过ABA问题](http://en.wikipedia.org/wiki/ABA_problem#Tagged_state_reference)。
* 实现一种[轻量的读写锁](http://preshing.com/20150316/semaphores-are-surprisingly-versatile)。

一般来说，如果你需要用mutex保护少量的几个变量，且这几个变量可以打包成32或64位整数，你就可以将基于mutex的代码改写成基于无锁的RMW操作，无论这些RMW操作底层是如何实现的！我之前在[信号量是如此的通用](http://preshing.com/20150316/semaphores-are-surprisingly-versatile)中为了实现一组轻量的同步原语，曾充分利用了这一原则。

当然这种技术不只适用于C++11的atomic库。上文的例子都使用C++11的atomic是因为它已经被广泛使用了，而且编译器的支持也非常好。你可以基于任何提供了CAS的库实现自己的RMW操作，例如[Win32](https://msdn.microsoft.com/en-us/library/ttk2z1ws.aspx)、[March kernel API](https://developer.apple.com/library/mac/documentation/Darwin/Reference/ManPages/man3/OSAtomicCompareAndSwap32.3.html)、[Linux kernel API](http://lxr.free-electrons.com/ident?i=atomic_cmpxchg)、[GCC atomic扩展](https://gcc.gnu.org/onlinedocs/gcc-4.9.2/gcc/_005f_005fatomic-Builtins.html)或[Mintomic](http://mintomic.github.io/lock-free/atomics/)。简洁起见，我没有讨论内存顺序，但一定要去考虑你的atomic库提供了怎样的顺序保证。尤其是，如果你自己写的RMW操作是要在线程间传递非atomic的信息，那至少你要保证它与某种[同步关系](http://preshing.com/20130823/the-synchronizes-with-relation)是等价的。