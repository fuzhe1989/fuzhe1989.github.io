---
title:      "C++ 变参宏的两个技巧"
date:       2022-08-01 21:22:09
tags:
    - C++
---

**TL;DR**

> 小朋友不要乱学

1. 基于参数数量重载宏函数。
1. 当 `__VA_ARGS__` 为空时，忽略多余的逗号。

<!--more-->

## 基于参数数量重载宏函数

> 参考这个回答：[Overloading Macro on Number of Arguments](https://stackoverflow.com/questions/11761703/overloading-macro-on-number-of-arguments)

大概套路就是：

```cpp
void f0();
void f1(int a);
void f2(int a, int b);
void f3(int a, int b, int c);

// define K concrete macro functions
#define FUNC_0() f0()
#define FUNC_1(a) f1(a)
#define FUNC_2(a, b) f2((a), (b))
#define FUNC_3(a, b, c) f3((a), (b), (c))

// define a chooser on arguments count
#define FUNC_CHOOSER(...) GET_4TH_ARG(__VA_ARGS__, FUNC_3, FUNC_2, FUNC_1, FUNC_0)

// define a helper macro
#define GET_4TH_ARG(a1, a2, a3, a4, ...) a4

// define the entry macro
#define FUNC(...) FUNC_CHOOSER(__VA_ARGS__)(__VA_ARGS__)
```

上面这个例子中，我们希望通过一个统一的入口（`FUNC`），根据参数数量重载几个具体的宏（`FUNC_0` 到 `FUNC_3`）。

具体做法是将宏定义分成两部分，首先通过参数数量来选择具体的宏名字，再将参数传入这个具体的宏，完成调用。

第一部分入口是：

```cpp
FUNC_CHOOSER(__VA_ARGS__)
```

我们看到它会被展开成

```cpp
GET_4TH_ARG(__VA_ARGS__, FUNC_3, FUNC_2, FUNC_1, FUNC_0)
```

假如我们传入的参数为 `FUNC(0, 1)`，则 `__VA_ARGS__` 展开成 `0, 1`，上面的表达式展开成

```
GET_4TH_ARG(0, 1, FUNC_3, FUNC_2, FUNC_1, FUNC_0)
```

`GET_4TH_ARG` 的结果是只保留第 4 个参数，恰好就是我们要的 `FUNC_2`。再之后的过程就很直接了。

这种方法的关键就是 `FUNC_CHOOSER` 中目标宏的顺序要逆序，从而实现根据参数数量选择正确的目标宏。

上面的方案有几点要注意：
1. 参数数量要连续。如果只存在 `FUNC_3` 和 `FUNC_0`，我们需要填充几个 dummy name 人为制造报错。
1. 数量数量必须是确定的。对于不定数量的调用，只能硬着头皮从 1 定义到某个超大的数（如 67）。（比如[这个例子](https://github.com/pingcap/tiflash/pull/5512)）

## 忽略多余的逗号

> 参考这个回答：[C Preprocessor Remove Trailing Comma](https://stackoverflow.com/questions/39291976/c-preprocessor-remove-trailing-comma)

[这个例子](https://godbolt.org/z/jdfbzxac6) 中我们用到了一个可变参数的宏来调用一个可变参数的函数：

```cpp
#include <fmt/format.h>

template <typename S, typename... Args>
void print(const S & fmt_str, Args &&... args) {
    fmt::print(fmt_str, std::forward<Args>(args)...);
}

#define PRINT(fmt_str, ...) print(fmt_str, __VA_ARGS__)

int main() {
    PRINT("a = {}, b = {}", 1, 2);
    PRINT("xxx");
}
```

看起来一切都 OK，直到编译时：

```
# gcc
<source>: In function 'int main()':
<source>:8:55: error: expected primary-expression before ')' token
    8 | #define PRINT(fmt_str, ...) print(fmt_str, __VA_ARGS__)
      |                                                       ^
<source>:12:5: note: in expansion of macro 'PRINT'
   12 |     PRINT("xxx");
      |     ^~~~~

# clang
<source>:12:5: error: expected expression
    PRINT("xxx");
    ^
<source>:8:55: note: expanded from macro 'PRINT'
#define PRINT(fmt_str, ...) print(fmt_str, __VA_ARGS__)
                                                      ^
```

原因是 `PRINT("xxx")` 会导致 `PRINT` 中的 `__VA_ARGS__` 为空，展开时产生了一个多余的逗号：

```cpp
print(fmt_str, )
```

这个问题看起来有两种解法：
1. 用 `__VA_OPT__(,)` 处理逗号。亲测可用，但只能用于 [gcc](https://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html)。
1. 用 `##__VA_ARGS__`，它可以在展开为空时消除掉前面的逗号。亲测 gcc 与 clang 都可用。