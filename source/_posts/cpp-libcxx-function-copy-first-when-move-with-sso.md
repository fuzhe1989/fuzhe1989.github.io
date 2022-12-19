---
title:      "记：不能依赖 std::function 的 move 函数清空 source"
date:       2022-08-14 11:23:45
tags:
---

**TL;DR**

分享某位不愿透露姓名的耿老板发现的 libc++ 的某个奇怪行为：`std::function` 当内部成员体积足够小，且其 copy 函数标记为 `noexcept` 时，move ctor 或 assign 函数会优先调用内部成员的 copy 函数，而不是 move 函数。

这不是 bug，但很反直觉。

<!--more-->

## 问题

看下面这段代码，你觉得它的输出该是什么

```cpp
auto holder = std::make_shared<T>(...); // holder holds some resource
// ...
std::function<void()> f1 = [holder = std::move(holder)] {};
auto f2 = std::move(f1);
std::cout << "f1 is " << (f1? "non-empty" : "empty") << std::endl;
```

直觉告诉我应该是 empty，但这是真的吗？

从 [Compiler Explorer](https://godbolt.org/z/6E71GoK3x) 我们看到，在不同编译器下，有不同结果：
- clang + libc++：non-empty
- clang + libstdc++：empty
- gcc：empty
- msvc：empty

说明问题出在 libc++ 的实现上。

## 影响

下面是为什么耿老板突然对这个行为产生了兴趣。

这个问题的影响是：如果我们依赖 `std::function` 来控制某个对象的生命期，则在后续 move 这个 `std::function` 之后，必须要手动 clear 或者析构旧的 `std::function`，不能依赖 move 本身的行为。

显然，某些代码不是这么写的。

## 不是 bug

虽然非常反直觉（毕竟 `std::shared_ptr<T>` 是 non-trivial 的），但这并不是 bug，因为标准没有规定 move 一个 `std::function` 之后，旧对象该如何处理：

> - `function( function&& other );`(since C++11)(until C++20) (4)
> - `function( function&& other ) noexcept;` (since C++20) (4)
>
> 3-4) Copies (3) or moves (4) the target of other to the target of *this. If other is empty, *this will be empty after the call too. For (4), other is in a valid but unspecified state after the call. [cppreference](https://en.cppreference.com/w/cpp/utility/functional/function/function)

"other is in a valid but unspecified state after the call."

但只有 libc++ 这么做，仍然很让人难受。

## libc++

libc++ 里对应的代码在[这里](https://github.com/llvm/llvm-project/blob/main/libcxx/include/__functional/function.h#L414-L420)。

```cpp
if (sizeof(_Fun) <= sizeof(__buf_) &&
    is_nothrow_copy_constructible<_Fp>::value &&
    is_nothrow_copy_constructible<_FunAlloc>::value)
{
    __f_ = ::new ((void*)&__buf_) _Fun(
        _VSTD::move(__f), _Alloc(__af));
}
```

可以看到，当初始化一个 `__value_func` 时，如果对应的 `_Fp` 足够小，且它和它对应的 allocator 的 copy ctor 都是 `noexcept`，`__value_func` 会将 `__f_` 直接分配在内部 buffer 中。

[这里](https://github.com/llvm-mirror/libcxx/blob/master/include/functional#L1810-L1814)则说的是 `__value_func` 的 move 函数对于 `__f_` 直接分配在内部 buffer 的这种情况，直接调用了实际 functor 的 `__clone`，但在之后没有对被 move 的对象做任何清理。

```cpp
if ((void*)__f.__f_ == &__f.__buf_)
{
    __f_ = __as_base(&__buf_);
    __f.__f_->__clone(__f_);
}
```

这其实是 libc++ 的一种 SOO（small object optimization），或称 SSO（small string optimization）或 SBO（small buffer optimization）。

[std::function copies movable objects when is SOO is used](https://github.com/llvm/llvm-project/issues/32472) 解释了 libc++ 不想改掉这个行为是因为需要增加 `__clone_move` 而破坏 ABI 兼容性。

## 进一步测试

下面这个例子（[Compiler Explorer](https://gcc.godbolt.org/z/YM3csqPKz) ）验证了我们的观点：

```cpp
struct Test {
    Test() {}
    ~Test() {}

    Test(const Test&) {}
    Test(Test && l) noexcept {}
};

struct TestNoExcept {
    TestNoExcept() {}
    ~TestNoExcept() {}

    TestNoExcept(const Test&) noexcept {}
    TestNoExcept(Test && l) noexcept {}
};

template <typename T>
void test(const std::string &name) {
    T t1;
    std::function<void()> f1 = [t = std::move(t1)]() -> void{ printf("lambda\n"); };
    auto f2 = std::move(f1);
    fmt::print("{} move {}\n", name, f1 == nullptr);
}

int main() {
    test<Test>("Test");
    test<TestNoExcept>("TestNoExcept");
}
```

输出为：

```
Test move true
TestNoExcept move false
```

`Test` 和 `TestNoExcept` 唯一的区别就在于它们 copy ctor 是不是 `noexcept`。而这就使得后续的 `std::function` 的行为产生了区别。真是神奇。

接下来，我们给 `TestNoExcept` 增加一些体积，使得它不满足 SOO：

```cpp
struct TestNoExcept {
    TestNoExcept() {}
    ~TestNoExcept() {}

    TestNoExcept(const Test&) noexcept {}
    TestNoExcept(Test && l) noexcept {}

    char padding[32];
};
```

输出就变成了：

```
Test move true
TestNoExcept move true
```

done。