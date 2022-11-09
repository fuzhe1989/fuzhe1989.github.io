---
title:      "C++：一个极简的静态反射 demo"
date:       2022-11-09 21:52:21
tags:
---

下面这个类可以静态枚举字段：

```cpp
struct A : Base {
    ADD_FIELD(int, a, 110);
    ADD_FIELD(double, b, 1.2);
    ADD_FIELD(std::string, c, "OK");
    ADD_FIELD(uint32_t, d, 27);
    std::string others;
};

int main() {
    A a;
    Helper::visit([](std::string_view name, auto &&value) {
        std::print("name: {} value: {}\n", name, value);
    }, a);

    Helper::apply([](int a, double b, std::string_view c, uint32_t d) {
        std::print("a: {} b: {} c: {} d: {}\n", a, b, c, d);
    }, a);
}
```

> 感谢某同事手把手教会我写这个 demo

<!--more-->

这里用到的主要技巧：函数重载决议的时候，如果没有完美匹配实参的函数，编译器会选择能将实参隐式转换到形参的函数。

比如形参是实参的基类：

```cpp
struct Base {};
struct Derived : Base {};

void f(Base);

f(Derived{}); // f(Base) 会被选中
```

进一步地，如果多个重载函数的形参都是实参的基类，则距离实参继承关系最近的基类版本会被选中：

```cpp
struct A {};
struct B : A {};

void f(A);
void f(B);

f(C{}); // f(B) 会被选中
```

那么如果我们维护一个继承链，对应一组重载函数，其中每个类型对应一个形参版本，我们能做到什么呢？

我们可以知道这个函数有多少个重载：

```cpp
template <size_t N>
struct Rank : Rank<N - 1> {
    static constexpr auto rank = N;
};

template <>
struct Rank<0> {
    static constexpr auto rank = 0;
};
```

假设我们人肉定义了以下重载：
- `Rank<0> f(Rank<0>)`
- `Rank<1> f(Rank<1>)`
...
- `Rank<50> f(Rank<50>)`

则我们用一个非常大的 `Rank` 就可以知道当前有多少个 `f`：

```cpp
std::cout << decltype(f(Rank<100>{}))::rank << std::endl; // 50
```

进一步地，我们还能用某个常数索引取出对应的 `Rank`：

```cpp
std::cout << decltype(f(Rank<20>{}))::rank << std::endl; // 20
```

回到文首的例子，如果我们能将每个 field 的类型作为一个重载函数的返回值，就可以用索引来得到对应 field 的类型了。

人肉写出来大约是这样：

```cpp
int f(Rank<0>);
double f(Rank<1>);
std::string f(Rank<2>);
uint32_t f(Rank<3>);
```

抽象化大概长这样：

```cpp
ADD_FIELD(T, name) T f(Rank<?>)
```

这里的问题在于 `?` 怎么生成。我们想通过某种方式，生成一个整数序列，听起来是不是很递归？但函数声明怎么递归呢？

看起来，我们需要每声明一个 `f` 时从上一个 `f` 获得帮助递归的信息：

```cpp
T f(Rank<current_max_rank + 1>);
```

那 `current_max_rank` 该怎么获取呢？从前面的例子中我们知道，我们可以用一个继承链末端的派生类来触发重载决议，从而得到当前 rank 最大的 `f` 的**返回类型**。因此我们还需要在返回类型中加上 rank 信息：

```cpp
template <typename T, size_t N>
struct TypeInfo {
    using type = T;
    static constexpr auto rank = N;
};

#define CURRENT_MAX_RANK = decltype(f(Rank<100>{}))::rank
#define NEXT_RANK (CURRENT_MAX_RANK + 1)

TypeInfo<T, NEXT_RANK> f(Rank<NEXT_RANK>);
```

这样我们每定义一个 field，就自动得到了一个具有更大 rank 的 `f`，其返回类型中就包含着我们要的信息。

接下来，我们需要为递归设置一个终点：

```cpp
TypeInfo<void, 0> f(Rank<0>);
```

合起来，就是下面的代码啦：

```cpp
struct Base {
    static TypeInfo<void, 0> f(Rank<0>);
};

#define ADD_FIELD(Type, name, ...) \
    Type name{__VA_ARGS__}; /* 用可选参数初始化 */\
    static TypeInfo<Type, NEXT_RANK> f(Rank<NEXT_RANK>)

struct A : Base {
    ...
};
```

然后我们就可以利用这些信息枚举 `A` 中的每个 field 类型了：

```cpp
template <typename T, size_t I>
using FieldType = std::decay_t<decltype(T::f(Rank<100>{}))>;

// FieldType<A, 1> -> int
// FieldType<A, 2> -> double
// FieldType<A, 3> -> std::string
// FieldType<A, 4> -> uint32_t
```

还能按顺序遍历 `A` 的每个字段：

```cpp
template <typename F, typename T, size_t I = 1>
void visit(F && f, T && t) {
    if constexpr (I <= MaxRank<T>) {
        f(FieldType<T, I>::?);
        visit<F, T, I + 1>(std::forward<F>(f), std::forward<T>(t));
    }
}
```

这里我们遇到的问题是：如何在遍历过程中拿到每个 field 的值。

我们可以在 `TypeInfo` 中增加一个 getter：

```cpp
template <typename T, size_t N, auto Getter>
struct TypeInfo {
    using type = T;
    static constexpr auto rank = N;
    static constexpr auto getter = Getter;
};

#define ADD_FIELD(Type, name, ...) \
    Type name{__VA_ARGS__}; /* 用可选参数初始化 */\
    static TypeInfo<Type, NEXT_RANK, &T::name> f(Rank<NEXT_RANK>)
```

注意这里我们获取的是成员变量指针，需要配合对象一起使用。接下来修改 `visit` 中调用 `f` 的地方：

```cpp
    using FT = FieldType<T, I>;
    f(t.*FT::getter);
```

这样我们就拿到了每个 field 的值。

接下来，我们还想拿 field name。可是字符串怎么放进 `TypeInfo` 中呢？`std::string` 和 `std::string_view` 都不能作为模板参数，那我们就将它转成字符数组：

```cpp
template <size_t N>
struct NameWrapper {
  constexpr NameWrapper(const char(&str)[N]) { std::copy_n(str, N, string); }
  constexpr operator std::string_view() const { return {string, N - 1}; }
  char string[N];
};

template <NameWrapper Name, ...>
struct TypeHelper {
  static constexpr std::string_view name = Name;
  ...
};
```

> 不太冷的冷知识：字符数组可以作为常量存在。

于是上面的宏定义还得改：

```cpp
#define ADD_FIELD(Type, name, ...) \
    Type name{__VA_ARGS__}; /* 用可选参数初始化 */\
    static TypeInfo<NameWrapper(#name), Type, NEXT_RANK, &T::name> f(Rank<NEXT_RANK>)
```

再改 `visit`：

```cpp
    f(FT::name, a.*FT::getter);
```

终于，我们完成了 `visit`，还差个 `apply`。不分析了，直接给答案：

```cpp
template <class T, size_t I>
    requires (I > 0 && I <= Size<T>)
auto getValue(T &a) {
    using Type = FieldType<T, I>;
    return a.*Type::getter;
}

template <typename F, typename T, std::size_t... I>
auto applyImpl(F&& f, T& t, std::index_sequence<I...>) {
    return f(getValue<T, I + 1>(t)...);
}

template <typename F, typename T>
auto apply(F &&f, T &t) {
    return applyImpl(std::forward<F>(f), t, std::make_index_sequence<MaxRank<T>>{});
}
```

这里用到的知识点：
1. [fold expression](https://en.cppreference.com/w/cpp/language/fold)
1. [requires](https://en.cppreference.com/w/cpp/language/requires)
1. [integer sequence](https://en.cppreference.com/w/cpp/utility/integer_sequence)

以上基本照搬 [apply](https://en.cppreference.com/w/cpp/utility/apply) 的实现。

由此，我们终于完成了这个极简的静态反射的 demo。

[Compiler Explorer](https://godbolt.org/z/qsqe3aW6b)