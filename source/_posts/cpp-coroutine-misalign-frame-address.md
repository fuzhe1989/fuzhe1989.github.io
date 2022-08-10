---
title:      "记：C++20 coroutine 的诡异 bug 调查过程"
date:       2022-08-09 20:48:56
tags:
---

**TL;DR**

C++20 coroutine 有一个严重的 [bug](https://github.com/llvm/llvm-project/issues/56671)，且这个 bug 本质上来源于 C++ 标准不完善：在分配 coroutine frame 时，没有严格按 alignment 要求。目前看起来 gcc 与 clang 都中招了，只有 msvc 似乎没问题。

本文记录了我是如何被这个 bug 消耗掉了~~两~~三天光明。

<!--more-->

## 起

我们项目中使用了 clang + folly::coro。我有一个 benchmark 工具大概长这个样子：

```cpp
folly::coro::Task<void> run() {
    Config config;
    // ...
    IOWorker worker;
    // ...
    Runners runners;
    // ...
    co_await runners.run();
}

int main() {
    folly::coro::blockingWait(run());
}
```

本来运行得很好。这天我需要拿它去给其他同学做个演示，就加了行输出，顺手 rebase 到 main（感恩 gitlab 的 zzzq），就坏事了：一运行就报错，说 `Config` 中的一个 `std::mutex` 默认构造时遇到了空指针。

现场大概长这样：

![](/images/2022-08/cpp-coroutine-bug-01.png)

看到这个报错位置的时候我是有点`地铁老人手机.jpg`的。

一定是 rebase 惹的祸，main 的新代码有毒！

## 承

### 怀疑 TDengine

查看了一下 main 的最新提交，只是引入了 TDengine，但我的工具没有用到它，只是被动链接了 TDengine client 的静态库。

会不会是它的静态库改变了某些编译期行为呢？

先搞清楚 `__GTHREAD_MUTEX_INIT` 是什么吧。我们虽然使用了 clang，但标准库还是用的 libstdc++，在这里 grep 找到它实际指向 `PTHREAD_MUTEX_INITIALIZER`，而后者是以宏的形式初始化一个 `pthread_mutex_t`。

恰好，我们在 TDengine 代码中找到了它重新定义了这个宏：

[#define PTHREAD_MUTEX_INITIALIZER](https://github.com/taosdata/TDengine/blob/e8a6b6a5a1e4806ce29ca9f80fe7059eb9ab0730/deps/pthread/pthread.h#L699)

会不会是这里不小心修改了标准库的行为，进而导致了进程 crash 呢？

我们随后发现不是：
1. 初始化一个 c 的 struct 不会因为值而 crash。
1. TDengine 的文件只会在非 posix 环境被用到。
1. 去掉 TDengine 的静态库仍然会 crash。

### 将 Config 移出 coroutine

无论如何，在 coroutine 中初始化带有 `std::mutex` 的对象还是有点奇怪的（至少部分观点这么认为），那我们将它移出去构造好，再将引用传给 coroutine，看看会发生什么。

```cpp
folly::coro::Task<void> run(Config &config) {
    // ...
    IOWorker worker;
    // ...
    Runners runners;
    // ...
    co_await runners.run();
}

int main() {
    Config config;
    folly::coro::blockingWait(run(config));
}
```

果然，`Config` 不 crash 了，改在构造 `IOWorker` 时 crash 了……

crash 的位置还是 `__GTHREAD_MUTEX_INIT`。

### valgrind

从最前面 crash 的 stack 来看，`this` 的值明显不对，会不会是内存写坏了？在老司机建议下，我们用 valgrind 跑了一下，一无所获。

### __builtin_return_address

重大突破（虽然事后证实是假象）：换用 gcc 之后 crash 消失了！

我们在 crash 的 stacktrace 中找到了 coroutine::resume，看起来 `folly::coro::blockingWait` 一定会先 suspend 再 resume。会不会是 clang 的 resume 有 bug，它跳到了错误的地址？

我们在 folly 代码中看到了 `__builtin_return_address`，未经证实，就觉得它是凶手。正好又搜到了这个答案：

[Why does __builtin_return_address crash in Clang?](https://stackoverflow.com/questions/65638872/why-does-builtin-return-address-crash-in-clang)

它里面说 clang 可能需要强制设置 `-fno-omit-frame-pointer` 来确保正确回溯 frame。我们的项目恰好没有显式设置这个 flag，加上试试。

还是不行：
1.  `__builtin_return_addres(0)` 不需要设置这个 flag 就可以正确工作。
1. 我们的进程并没有 crash 在 return 时，而是在 coroutine 运行时，本来就不该关注这里。

### 事情开始变得奇怪起来

陷入困境，尤其是我们甚至不知道该给谁开 bug（folly 还是 clang？）。

鉴于现场还比较复杂，我们开始着手简化现场，搞个最小化 case 出来。

于是事情开始变得奇怪起来：注释掉 `run` 中的唯一的 `co_await` 之后，crash 消失了！但 `co_await` 明明是发生在 crash 的位置之后，也就是说注释掉后面代码会影响前面代码的行为。

去掉了 `co_await` 之后 `run` 内部就不再有 suspend point 了，因此 clang 不会在内部为其产生 async stack frame（用于 resume）。这是一个非常关键的线索。

顺着这个线索，我们发现即使 `co_await` 一个 dummy function，也会引入 crash。

接下来，我们开始二分注释代码，立求将 `run` 简化到最小。

### 最小化 case v1

……最终，我们得到了这么一个 case：

```cpp
folly::coro::Task<void> dummy() { co_return; }

folly::coro::Task<void> run() {
    folly::CPUThreadPoolExecutor executor(1);
    co_await dummy();
    executor.add([] {}); // prevent `executor` from eliminated by compiler
}

TEST(Test, Normal) {
    folly::coro::blockingWait(run());
}
```

看起来已经非常明显了，一定是 folly 或者 clang 中的一个的 bug。只是我们还不知道该给谁发 bug。

### 事情变得更加奇怪了

……还没完。

我们发现，上面这个 gtest 行为非常奇怪：
1. `run --gtest_filter="Test.Normal"` 会 crash。
1. `run --gtest_filter="Test.*"` 不会 crash。
1. `run --gtest_filter="Test.Normal*"` 不会 crash。

## 转

一个周末过去了，我们觉得还是应该把这个 bug 查清楚（关系到我们还能不能继续使用 coroutine），至少这不是随机 crash 吧。

我们将 `CPUThreadPoolExecutor` 变成在堆上分配（`std::unique_ptr`）之后，crash 就消失了，进一步说明 crash 和 coroutine async frame 有关，一定是有某个东西在 coroutine 栈上分配就会导致 crash。

接下来，我们将 `CPUThreadPoolExecutor` 的所有成员显式分配到栈上，二分排除，最终找到了最小化 case v2。

### 最小化 case v2

```cpp
folly::coro::Task<void> run() {
    char padding0[88];
    folly::LifoSem sem;
    std::deque<int> queue;
    char padding1[72];

    co_await dummy();
    // ...
}
```

最小化过程中我们为被排除掉的变量都申请了同样大小的栈内存。

这已经和 `std::mutex` 没关系了，我们一开始的方向完全是错的！

### 定位到 alignment

仔细查看 `folly::LifoSem` 的实现，我们发现它是 cacheline 对齐的，但 stacktrace 显示它的地址不能被 64 整除。

Wow，amazing，unbelivable。

## 合

隐约记得之前在怀疑 clang 的时候看过它的 open issues，里面有个似乎和 alignment 有关：

[Clang misaligns variables stored in coroutine frames](https://github.com/llvm/llvm-project/issues/56671)

它大概说的是：
1. 一个 coroutine function 里，如果 `co_await` 前面有变量需要 `alignment > 8`，clang 不保证分配出来的 async stack frame 满足这个条件。
1. 这个 bug 不是 clang 自己的问题，它是严格按 std 标准实现的，是标准没有包含这项要求。
1. 2020 年已经有提案说这件事了（[wg21.link/p2014r0](https://wg21.link/p2014r0)），但被人关了，今年又 reopen，看看能不能进 C++26（f**k）。
1. 如果 clang 自己做了扩展，需要应用自己的 `promise_type` 不会重载 `operator new`，否则 clang 也没办法介入。

和我们遇到的情况，不能说一模一样吧，至少也是同一个 bug。

这样，前面种种奇怪现象也都有了合理解释：编译期的 bug 导致了运行期异常，具体到 `operator new` 返回的地址是否对齐。

终于水落石出了。但 coroutine 能不能安心继续用呢？我们知道 alignment 是非常常用的优化手段，尤其是 cacheline 对齐，coroutine 里也经常会这么定义一个变量。但这个 bug 的存在（尤其是它至少要存活到 2026 年），我们随时可能撞上诡异的 crash。

幸好我不负责这个项目，不用我去头疼。
