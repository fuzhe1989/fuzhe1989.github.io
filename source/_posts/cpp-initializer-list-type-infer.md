---
title:      "C++: std::initializer_list 的类型推导"
date:       2022-08-26 17:21:29
tags:
---

[Compiler Explorer](https://godbolt.org/z/nh57noeM3) 一个例子胜千言。

更准确地说明看[这里](https://en.cppreference.com/w/cpp/utility/initializer_list)。

<!--more-->

```cpp
auto l = {1, 2, 3};
static_assert(std::is_same_v<decltype(l)::value_type, int>);
```

直接用 `auto` 接收一个 `std::initializer_list<T>`，可以，且能正确推导类型。

```cpp
std::vector<int> v1 = {1, 2, 3};
```

直接拿它初始化确定的类型，可以。

```cpp
auto v2 = std::vector({1, 2, 3});
static_assert(std::is_same_v<decltype(v2), std::vector<int>>);
```

直接用 `auto` 接收一个推导 `std::initializer_list<T>` 进而初始化的 `std::vector<T>`，可以，且能正确推导类型。

```cpp
template <typename T>
void g(std::initializer_list<T> input) {
    std::vector<T> v = input;
    std::cout << v.size() <<std::endl;
}

g({1, 2, 3});
```

直接匹配模板函数的类型参数，可以。

```cpp
template <typename T>
void f(std::vector<T> input) {
    std::cout << input.size() <<std::endl;
}

f({1, 2, 3});
```

直接匹配 `std::vector<T>` 参数的模板，不行！