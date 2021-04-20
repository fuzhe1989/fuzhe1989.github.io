---
title:      "ClickHouse基础库学习：StrongTypedef"
date:      2021-04-20 17:08:52
tags:
    - ClickHouse
    - C++
---

位置：base/common/strong_typedef.h

```cpp
template <class T, class Tag>
struct StrongTypedef
{
private:
    using Self = StrongTypedef;
    T t;

public:
    using UnderlyingType = T;
    template <class Enable = typename std::is_copy_constructible<T>::type>
    explicit StrongTypedef(const T & t_) : t(t_) {}
    template <class Enable = typename std::is_move_constructible<T>::type>
    explicit StrongTypedef(T && t_) : t(std::move(t_)) {}

    template <class Enable = typename std::is_default_constructible<T>::type>
    StrongTypedef(): t() {}

    StrongTypedef(const Self &) = default;
    StrongTypedef(Self &&) = default;

    Self & operator=(const Self &) = default;
    Self & operator=(Self &&) = default;

    template <class Enable = typename std::is_copy_assignable<T>::type>
    Self & operator=(const T & rhs) { t = rhs; return *this;}

    template <class Enable = typename std::is_move_assignable<T>::type>
    Self & operator=(T && rhs) { t = std::move(rhs); return *this;}

    operator const T & () const { return t; }
    operator T & () { return t; }

    bool operator==(const Self & rhs) const { return t == rhs.t; }
    bool operator<(const Self & rhs) const { return t < rhs.t; }

    T & toUnderType() { return t; }
    const T & toUnderType() const { return t; }
};

#define STRONG_TYPEDEF(T, D) \
    struct D ## Tag {}; \
    using D = StrongTypedef<T, D ## Tag>; \
```

`StrongTypedef`提供了`T`的一种别名，它支持以下操作：
- 默认构造、复制/移动×构造/赋值。
- `T`到`StrongTypedef`：复制/移动×构造/赋值。
- `StrongTypedef`到`T`以及`T`可以隐式转换的类型`U`：隐式转换，`toUnderType`。

关键是`StrongTypedef`禁止了哪些操作：
- `T`相同但`Tag`不同的两个`StrongTypedef`，好处是可以避免语义混淆：

    ```cpp
    using Days = StrongTypedef<int32_t, Tag0>;
    using Months = StrongTypedef<int32_t, Tag1>;
    Days d(100);
    Months d1 = d; // error!
    ```

（说实话我觉得`StrongTypedef`不应该支持到`T`的隐式转换，避免当`T`是某种整数类型时无意的narrowing）