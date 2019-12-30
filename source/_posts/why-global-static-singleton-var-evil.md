---
title:      "为什么全局变量、静态变量、单例不好"
date:       2017-09-30 14:38:34
tags:
    - 编程
---

## 全局变量

很多人都知道代码中要尽量避免使用全局变量，那么全局变量有什么问题，为什么大家觉得它不好？因为全局变量是一种全局状态，而可变的全局状态破坏了理想的程序。

<!--more-->

### 理想的程序

理想情况下：

1. 整个程序是由一个个相互独立的组件（模块或函数或对象）组成，组件的设计遵循“高内聚低耦合”，不同的组件之间仅在必要时通过公开接口交互，任意两个组件间的通信链路是清晰明确的。组件间的依赖关系因此也是清晰的。
1. 我们可以替换掉任意组件而不影响其它组件的运行，从而注入我们的逻辑。尤其是在测试中，我们可以mock掉任意组件，从而控制程序的行为，达到我们的测试目的。
1. 程序不存在未定义行为，任意组件的行为都是可预测的，尤其是组件的初始化顺序是确定的。
1. 从代码角度，组件的逻辑要有局部性，即逻辑相关的代码是在一起的，尽量减少阅读时的跳跃次数。

### 全局状态的问题

1. 全局状态是暴露在外面的，任何地方都可以访问，你没有办法约束这种行为，破坏了封装性。另一方面，可以认为全局状态也侵入到了访问它的组件中，也破坏了这些组件的封装性。尤其是如果一个const方法访问了全局变量，它可能就算不上const方法了。
1. 两个组件如果访问了相同的全局状态，它们实际上就耦合在一起了，独立性被破坏了。
1. 组件交互的链路不再清晰，理想链路上的任意两个组件都可能通过某个全局状态交互。
1. 全局状态往往会被并发访问，这就要求全局状态一定要具备线程安全性。但这一点不是总能被很好的保证的。尤其是两个分别保证了线程安全性的全局状态，不代表一起使用时还是线程安全的，还需要额外的同步手段。某种程序上，这种同步也会影响程序的性能。
1. 全局状态最讨厌的是，它导致测试很难写。首先，很难控制所有的全局状态，因此很难把环境弄干净，测试结果可能被任何一个漏掉的全局状态所影响。其次，全局状态是难以替换的，也就意味着mock和逻辑注入非常困难，甚至导致一些测试无法实现。

C++中全局变量还有一个问题：不同编译单元的全局变量不保证初始化顺序，当你在一个全局变量的构造过程中访问另一个全局变量，很可能会进入未定义状态。

有一个笑话说：C++中全局变量的名字前缀用什么最好？答案是//。

### 哪些全局状态是有价值的

当然全局变量也有一些场合是有价值的：

1. 常量。如果一个全局状态在运行期不可变，且我们不需要替换它的逻辑，它就是相对安全的。但不可忽视的是，它仍然可能破坏了组件间的依赖关系。对于非编译期常量来说，初始化顺序的问题依然存在。
1. 某些模块使用全局状态会极大降低实现复杂度，以至于可以忽略上面说的这些缺点。比如Logger或Trace系统，它们往往不需要替换，同时所有组件都依赖它们，如果不实现为全局状态的话实现上会很复杂。
1. 写生命期极短的脚本。对此我持怀疑态度，因为有太多预期很快弃用的脚本最终却在线上跑了很久。

即使是这些场合下，仍然可能有不用全局变量的方法。

### 全局状态的替代方法

一种比较好的方式就是ContextObject，即我们在程序的最上层（比如main函数）构造一个持有变量、资源或配置的对象，它会作为下级模块的环境被传递下去。好处：

1. 保证了封装性，ContextObject的传递是通过正常的函数调用或对象构造。
1. 保证了变量的初始化顺序，ContextObject持有的所有成员的初始化都是在它的函数里进行的，因此初始化顺序是确定的。
1. 容易mock，现在我们只要传递一个不同的ContextObject，就可以改变下级模块依赖的所有组件的行为，很容易构造出不同的测试场景。

## 静态变量

对C++而言，这里说的静态变量指三种静态变量：

1. 匿名命名空间中的变量。
1. 类的static成员变量。
1. 函数中的局部静态变量。

### 匿名命名空间中的变量

本质上，这种变量仍然是全局变量，只不过范围更小，更可控。相比全局变量，它对封装性、独立性、代码局部性的危害比较小。但它对可测性的危害仍然在，甚至比全局变量还大：你没办法从外面访问到它，连修改的机会都没有。

### 类的static成员变量

对于类的private静态成员而言，其它组件没有办法访问到它，因此它与匿名命名空间中的变量/函数类似，对封装性、独立性、代码局部性都有一定保证，而缺点也是不利于测试。如果是static const成员变量，不需要测试，就没有这个缺点了，是可以放心使用的。

对于类的public静态成员而言，无论是成员变量还是方法，都属于全局状态，全局状态的缺点它一个都不少。

### 函数中的局部静态变量

C++还有一种静态变量是定义在函数中的，某种角度讲它是更危险的全局变量：

1. 一个函数返回了局部静态变量的指针或引用，接收处很容易忽略这一点，错误的析构这个变量，导致未定义行为发生。
1. 包含局部静态变量的函数容易被误认为是无状态的、线程安全的、幂等的，实际上不是（C标准库中的一大堆函数）。
1. 局部静态变量的构造发生在第一次访问时，因此其构造顺序是不确定的，因此其析构顺序也是不确定的。
1. C++11标准保证了“局部静态变量的构造是串行的”，因此是线程安全的，但C++98没有这种保证。GCC自从4.3才有这种保证，而我们大面积使用的4.1.2并没有这种保证。

    ```cpp
    Logger& GetLogger() {
        static Logger logger;
        return logger;
    }
    ```

    上面这段代码，在C++11之前不保证线程安全。而基于这种方法实现的Singleton也是不对的。

### 静态变量的替代方法

对于匿名命名空间的变量，替代方法就是将其从匿名命名空间移出来。在此之后我们就可以使用ContextObject来控制和mock它了。

对于类的private静态成员，如果不需要修改它的行为，就不需要替代。对于public静态变量，考虑使用ContextObject来替代。对于public静态方法，可以将其逻辑独立成一个类，运行期注入到原来的类中，这样也转化为ContextObject方法能解决的问题。

对于函数中的局部静态变量，同样可以使用ContextObject方法来替代。

## 单例

很多人喜欢用Singleton，但它也没有避免全局变量的各种问题。一个全局唯一、全局（或模块内）可访问的对象，同样是一种全局状态。

Singleton的主要目的是为了限制全局只有一个实例，但这违背了“Single Responsibility Principle”，你把两种责任加到了同一个类上。一个类本身不应该关心它自己是不是Singleton。应该负起“限制全局唯一”的应该是最上层模块，如果它只创建了一个该类的实例，那么这个实例就是单例。某人说过，理论上就不存在Singleton。

Singleton的另一个问题是想正确实现Singleton并不是那么容易的。上面`GetLogger`实际就是C++中常用的一种Singleton实现，被称为“Meyers Singleton”，但在C++11前不保证线程安全性，也就不保证只初始化一次。而另一种常用的double lock的方法也不容易实现对。

下面是我见过的一种Singleton实现：

```cpp
Object& Object::Instance() {
    static Object* obj = NULL;
    static Mutex lock;
    if (obj != NULL) {
        return *obj;
    }
    LockGuard guard(lock);
    if (obj != NULL) {
        return *obj;
    }
    obj = new Object;
    return *obj;
}
```

它有哪些问题？

1. 在C++11之前，`lock`的构造不保证线程安全，即有可能两个线程用到两个`Mutex`对象。
1. 内存泄漏，`obj`是不析构的，如果`obj`里面还管理着一些需要释放的资源就麻烦了。

C++中的一些Singleton方法返回的是对象指针，当有人不小心在其上调用`delete`后什么事情都可能发生。

与全局变量类似，Singleton也可以被ContextObject替代。

当然，作为一种全局状态，在一些非常common的模块，用Singleton会令代码非常简洁，例子仍然是Logger或Trace。另一种Singleton的适用场合是当你用它去封装一种硬件资源时。不像软件资源，硬件资源就是只有一份，此时不用Singleton而用其它抽象方式都是背离了它的本质。

## 相关链接

- [Global Variables Are Bad](http://wiki.c2.com/?GlobalVariablesAreBad)
- [Why are global variables evil?](https://stackoverflow.com/questions/19158339/why-are-global-variables-evil)
- [Why is Global State so Evil?](https://softwareengineering.stackexchange.com/questions/148108/why-is-global-state-so-evil)
- [Why Static is Bad and How to Avoid It](https://dzone.com/articles/why-static-bad-and-how-avoid)
- [Why are static variables considered evil?](https://stackoverflow.com/questions/7026507/why-are-static-variables-considered-evil)
- [Why Singletons are Evil](https://blogs.msdn.microsoft.com/scottdensmore/2004/05/25/why-singletons-are-evil/)
- [Singletons Are Evil](http://wiki.c2.com/?SingletonsAreEvil)
- [What is so bad about singletons?](https://stackoverflow.com/questions/137975/what-is-so-bad-about-singletons)
- [Why The Singleton Pattern Sucks and You Should Avoid It](https://whydoesitsuck.com/why-the-singleton-pattern-sucks-and-you-should-avoid-it/)
- [Single responsibility principle](https://en.wikipedia.org/wiki/Single_responsibility_principle)
- [Thread-safe static variables without mutexing?](https://stackoverflow.com/questions/1052168/thread-safe-static-variables-without-mutexing)
- [Is Meyers' implementation of the Singleton pattern thread safe?](https://stackoverflow.com/questions/1661529/is-meyers-implementation-of-the-singleton-pattern-thread-safe)
