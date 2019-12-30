---
title:      "C++：模板类中如何调用其模板基类中的函数"
date:       2017-09-05 14:32:37
tags:
    - C++
---

## 问题

下面这段代码：

```cpp
#include <iostream>
#include <string>

template <typename T>
class Base {
public:
    virtual const std::string& Method() const = 0;
};

template <typename T>
class Base2: public Base<T> {
public:
    void Func() const {
        std::cout << "Func:" << Method() << std::endl;
    }
};

class Derived: public Base2<double> {
public:
    const std::string& Method() const {
        static const std::string s("Derived");
        return s;
    }
};

int main() {
    Derived d;
    d.Func();
}
```

在gcc4.1.2下编译会有这样的错误：

```
test.cpp: In member function ‘void Base2<T>::Func() const’:
test.cpp:14: error: there are no arguments to ‘Method’ that depend on a template parameter, so a declaration of ‘Method’ must be available
test.cpp:14: error: (if you use ‘-fpermissive’, G++ will accept your code, but allowing the use of an undeclared name is deprecated)
```

## 原因

C++在对模板类和模板函数进行名字查找时，会分成两次进行：

1. 对于与模板参数无关的名字，或称无依赖名字，编译器会在看到这个模板的定义时查找名字。
2. 对于与模板参数有关的名字，或称有依赖名字，编译器会推迟检查，直到模板实例化时再查找名字。

在我们的例子中，`Method`与模板参数无关，因此是无依赖名字，编译器会在看到`Base2`定义时查找名字。因为`Base`是个模板类，在这次查找时还没有实例化，因此编译器不会去`Base`中查找`Method`，只会在`Base2`的定义体中及外围作用域查找`Method`。

上面的例子中，如果`Base`不是模板类，而是普通类：

```cpp
class Base {
public:
    virtual const std::string& Method() const = 0;
};
```

你会发现编译就正常了。

## 错误解法1：指定基类类型调用

假如我们调用`Method`时指定基类类型，这样`Method`就变成有依赖名字了，是否可行？

```cpp
template <typename T>
class Base2: public Base<T> {
public:
    void Func() const {
        std::cout << "Func:" << Base<T>::Method() << std::endl;
    }
};
```

编译正常，运行一下：

```
/tmp/ccgToaEX.o: In function `Base2<double>::Func() const':
test.cpp:(.text._ZNK5Base2IdE4FuncEv[Base2<double>::Func() const]+0x12): undefined reference to `Base<double>::Method() const'
collect2: ld returned 1 exit status
```

为什么？因为我们指定了`Method`的类型，对`Method`的调用就是静态绑定，没有了动态绑定的效果，我们运行的`Method`就是基类版本，也就是没有定义的那个版本。

## 正确解法1：using基类中的名字

我们可以手动using基类中的名字，让名字查找时能看到`Method`，这样还表明`Method`是`Base<T>`中的成员，也就意味着`Method`依赖`T`，它就是一个有依赖名字，会推迟到实例化时再查找。这样我们仍然能让`Method`的调用是动态绑定：

```cpp
template <typename T>
class Base2: public Base<T> {
public:
    using Base<T>::Method;
    void Func() const {
        std::cout << "Func:" << Method() << std::endl;
    }
};
```

编译正常，运行结果：

```
Func:Derived
```

完成！

## 正确解法2：使用`this`调用

另一种解法是显式使用`this`，这样也可以将`Method`变成有依赖名字：

```cpp
template <typename T>
class Base2: public Base<T> {
public:
    void Func() const {
        std::cout << "Func:" << this->Method() << std::endl;
    }
};
```

编译正常，运行结果：

```
Func:Derived
```

完成！
