---

title:      "C++：delete不完整类型的指针"
date:       2017-05-16 22:50:29
tags:
    - C++
---

## 简单版

以下代码编译时会有warning：

```cpp
class X;

void foo(X* x) {
    delete x;
}
```

在GCC4.1.2下，编译出错信息是：

```
warning: possible problem detected in invocation of delete operator:
warning: ‘x’ has incomplete type
warning: forward declaration of ‘struct X’
note: neither the destructor nor the class-specific operator delete will be called, even if they are declared when the class is defined.
```

这是因为在`foo`里，编译器看不到`X`的完整类型，没办法确定两件事情：

1. `X`有没有自定义的析构函数（准确的说，有没有non-trivial的析构函数）。
2. `X`有没有自定义的`operator delete`函数。

在不确定这两件事情的情况下，编译器只能按最普通的方式去处理`delete x`：

1. 不调用任何析构函数。
2. 调用全局的`operator delete`，通常来说就是直接释放内存。

## 日常版

有一个我们平常会遇到的场景，就会触发上面这个问题。

以下是由三个文件组成的一个工程，其中用到了'pImpl'方法来隐藏实现，因此在接口类中放了一个`std::auto_ptr`，很简单：

```cpp
// test.h
#include <memory>

class A {
    class Impl;
public:
    A();
    void Func();
private:
    std::auto_ptr<Impl> mImpl;
};

// test.cpp
#include "test.h"
#include <iostream>

class A::Impl {
public:
    void Func() {
        std::cout << "Func" << std::endl;
    }
};

A::A(): mImpl(new Impl) {}

void A::Func() {
    mImpl->Func();
}

// main.cpp

#include "test.h"

int main() {
    A a;
    a.Func();
}
```

看起来很正常，但编译时有warning：

```
$g++ test.cpp main.cpp
In destructor ‘std::auto_ptr<_Tp>::~auto_ptr() [with _Tp = A::Impl]’:
test.h:4:   instantiated from here
warning: possible problem detected in invocation of delete operator:
warning: invalid use of undefined type ‘struct A::Impl’
test.h:5: warning: forward declaration of ‘struct A::Impl’
note: neither the destructor nor the class-specific operator delete will be called, even if they are declared when the class is defined.
```

和前面说的warning信息完全一致，看起来也是在调用`delete`时出的问题。但哪里调用了`delete`呢？

答案是`std::auto_ptr`。

上面的代码中，我们没有给`class A`手动写一个析构函数，因为编译器自动生成的析构函数就是我们要的：析构时把`mImpl`析构掉。

那么自动生成的析构函数长什么样子呢？大概是：

```cpp
A::~A() {
    mImpl.~auto_ptr();
}
```

展开了基本就是一句`delete`：

```cpp
A::~A() {
    delete mImpl._M_ptr;
}
```

这个析构函数的位置在哪呢？C++标准里说会把自动生成的类成员函数放在类的定义中，那么就是在test.h中。

问题清楚了：我们在编译main.cpp时，看不到`A::Impl`的完整定义，但却有一个自动生成的`A::~A`，其中delete了一个不完整的类对象！

## 解法

手动写一个`A`的析构函数，位置要在能看到`A::Impl`完整定义的地方，也就是test.cpp：

```cpp
```cpp
// test.h
#include <memory>

class A {
    class Impl;
public:
    A();
    ~A();
    void Func();
private:
    std::auto_ptr<Impl> mImpl;
};

// test.cpp
#include "test.h"
#include <iostream>

class A::Impl {
public:
    void Func() {
        std::cout << "Func" << std::endl;
    }
};

A::A(): mImpl(new Impl) {}
A::~A() {}

void A::Func() {
    mImpl->Func();
}
```

## 相关文献

* http://stackoverflow.com/questions/4325154/delete-objects-of-incomplete-type
* http://stackoverflow.com/questions/4023794/forward-declaration-just-wont-do