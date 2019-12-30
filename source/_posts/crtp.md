---
title:      "Curiously Recurring Template Pattern(CRTP)"
date:       2018-04-21 00:53:59
tags:
    - C++
---

## CRTP

C++中有一种很特别的模式，称为Curiously Recurring Template Pattern，缩写是CRTP。从它的名字看，前三个词都是关键字。Curiously，意思是奇特的。Recurring，说明它是递归的。Template，说明它与模板有关。

最常见的CRTP形式就很符合这三个关键字：

```cpp
template <typename T>
class Base {
    ...
};

class Derived : public Base<Derived> {
    ...
};
```

猛一看这段代码，确实挺奇特的：派生类继承自一个用派生类特化的基类，相当于自己特化了自己。

这里面应用到了C++模板的一个特性：与模板参数有关的代码的编译会推迟到模板实例化时进行。

## 静态多态

CRTP的第一个用途就是实现静态多态。

传统的C++中我们想要实现多态首先要有继承和虚函数：

```cpp
class Base {
public:
    ...
    virtual int Foo() = 0;
};

class Derived : public Base {
public:
    ...
    int Foo() override;
};
```

并通过基类的指针或引用来触发多态：

```cpp
void Func(Base& b) {
    cout << b.Foo() << endl;
}
```

但这套方案有两个问题：
1. 虚函数会影响类型的内存布局，空间上增加一个虚表指针。
1. 虚函数调用需要增加一次跳转，增加了运行时开销。

而用CRTP，我们可以实现编译时的静态多态。在这个方案中，基类负责定义接口，而派生类则负责实现接口：

```cpp
template <typename T>
class Base {
public:
    ...
    int Foo() {
        return static_cast<T*>(this)->Foo();
    }
};

class Derived : public Base<Derived> {
public:
    ...
    int Foo();
};
```

这个方案中，基类的`Foo()`会去调用派生类的`Foo()`，相当于前者是接口，而后者是实现。

注意在`Base::Foo`中，我们为了调用`Derived::Foo`，需要通过`static_cast`来显式转换`this`的类型。为什么这里用`static_cast`而不是`dynamic_cast`呢？因为`Base`自己是不知道`T`是它的派生类的，因此这里也不应该用`dynamic_cast`，而因为这里我们没有虚函数，用`static_cast`也是安全的。

CRTP方案的优点：
1. 没有虚函数，不会改变派生类的内存布局，空间上开销更小。
1. `b.Foo()`不是虚函数调用，不会增加一次跳转，运行时开销更小。
1. `Base::Foo()`甚至可以内联掉，进一步降低了运行时开销。
1. 模板对接口的要求是“Duck Typing”，比虚函数的要求更低。这个例子中，只要派生类满足有一个public的，名字为`Foo`，接受0个参数，返回类型可隐式转换为`int`的函数，就满足了`Base`的接口要求。

当然静态多态就导致了`Base`的不同的派生类实际继承自不同的基类，因此没有办法把它们的指针或引用放到某个容器中。另外，这样每个派生类都会实例化一个基类类型，会导致目标代码多于普通的继承。

## mixin

CRTP的第二个用途就是为其它类型增加功能，此时CRTP的基类就是一种mixin类型。

当CRTP用于mixin时，它的写法与静态多态很类似，只不过此时我们要的不是多态，而是新的功能，因此基类与派生类的方法名要不同：

```cpp
template <typename T>
class Repeatable {
public:
    void Repeat(int n) const {
        for (int i = 0; i < n; ++i) {
            static_cast<const T*>(this)->Foo();
        }
    }
};

class ZeroPrinter : public Repeatable<ZeroPrinter> {
public:
    void Foo() const {
        cout << "0";
    }
};
```

我们用CRTP为`ZeroPrinter`增加了一个`Repeat`功能，此时`Repeatable`就是一种mixin。而在这个方案中，我们不需要让`ZeroPrinter`去实现某个接口，去把自己已有的函数改成虚函数。

而且我们还可以为已经存在的类型增加功能。假如`ZeroPrinter`是第三方库提供的类型，我们没办法让它继承自`Repeatable`，那么我们可以增加一种新类型，同时继承`ZeroPrinter`和`Repeatable`：

```cpp
class RepeatableZeroPrinter: public ZeroPrinter, public Repeatable<RepeatableZeroPrinter> {
};
```

注意，当我们用CRTP来实现mixin时，要注意派生类与基类的函数名不能相同，因为派生类会屏蔽掉基类的名字，而导致我们想增加的功能无法被使用。

另一个mixin的例子是`Counter`，我们可以利用`Base<T>`和`Base<R>`不是一个类型的特性，为不同的类型增加实例个数的Counter统计的功能。

```cpp
template <typename T>
struct Counter {
    static int mObjectsCreated;
    static int mObjectsAlive;

    Counter() {
        ++mObjectsCreated;
        ++mObjectsAlive;
    }
    
    Counter(const Counter&) {
        ++mObjectsCreated;
        ++mObjectsAlive;
    }
protected:
    // objects should never be removed through pointers of this type
    ~Counter() {
        --mObjectsAlive;
    }
};
template <typename T> int Counter<T>::mObjectsCreated(0);
template <typename T> int Counter<T>::mObjectsAlive(0);

class X : Counter<X> {
    // ...
};

class Y : Counter<Y> {
    // ...
};
```

这个例子中，`X`和`Y`各自通过`Counter<X>`和`Counter<Y>`来实现统计功能。

## 链式多态

假设有基类：

```cpp
class Printer {
public:
    explicit Printer(ostream& pstream) : mStream(pstream) {}

    template <typename T>
    Printer& Print(T&& t) {
        mStream << t;
        return *this;
    }

    template <typename T>
    Printer& Println(T&& t) {
        mStream << t << endl;
        return *this;
    }
private:
    ostream& mStream;
};
```

我们可以链式调用：

```cpp
Printer{myStream}.Println("hello").Println(500);
```

但派生类就不行：

```cpp
class CoutPrinter : public Printer {
public:
    CoutPrinter() : Printer(cout) {}

    CoutPrinter& SetConsoleColor(Color c) { ... return *this; }
};
```

```
                             v-- we have a 'Printer' here, not a 'CoutPrinter'
CoutPrinter().Print("Hello ").SetConsoleColor(Color.red).Println("Printer!"); // compile error
```

因为`print`只会返回`Printer&`。

用CRTP就可以解决这个问题：

```cpp
// Base class
template <typename ConcretePrinter>
class Printer {
public:
    explicit Printer(ostream& pstream) : mStream(pstream) {}

    template <typename T>
    ConcretePrinter& Print(T&& t)
    {
        mStream << t;
        return static_cast<ConcretePrinter&>(*this);
    }

    template <typename T>
    ConcretePrinter& Println(T&& t)
    {
        mStream << t << endl;
        return static_cast<ConcretePrinter&>(*this);
    }
private:
    ostream& mStream;
};
 
// Derived class
class CoutPrinter : public Printer<CoutPrinter> {
public:
    CoutPrinter() : Printer(cout) {}

    CoutPrinter& SetConsoleColor(Color c) { ... return *this; }
};
 
// usage
CoutPrinter().Print("Hello ").SetConsoleColor(Color.red).Println("Printer!");
```

## 利用CRTP提供默认Clone

当要通过基类指针获得对象的拷贝时，通常做法是加个虚的`Clone`函数，而用CRTP可以避免在每个派生类中重复这个函数，只要派生类允许复制构造即可：

```cpp
// Base class has a pure virtual function for cloning
class Shape {
public:
    virtual ~Shape() {}
    virtual Shape* Clone() const = 0;
};

// This CRTP class implements clone() for Derived
template <typename Derived>
class Shape_CRTP : public Shape {
public:
    virtual Shape* Clone() const {
        return new Derived(static_cast<Derived const&>(*this));
    }
};

class Square : public Shape_CRTP<Square> {
    ...
};

class Circle : public Shape_CRTP<Circle> {
    ...
};
```

## 摆脱`static_cast`

上面每个CRTP例子中都有`static_cast`，我们可以通过一个辅助类来避免每次都直接调用`static_cast`：

```cpp
template <typename T>
struct CRTP {
    T& Underlying() { return static_cast<T&>(*this); }
    T const& Underlying() const { return static_cast<T const&>(*this); }
};
```

这样前面的例子就可以写成：

```cpp
emplate <typename T>
class Base : private CRTP<T>{
public:
    ...
    int Foo() {
        return this->Underlying().Foo();
    }
};
```

注意1：这里要private继承，是因为我们不想把`Underlying`函数暴露出去。

注意2：这里为什么要用`this->Underlying()`而不是直接使用`Underlying()`？参见[模板类中如何调用其模板基类中的函数](/2017/09/05/how-to-call-method-of-base-class-in-template/)。

## 避免继承错误的基类

当我们写多个CRTP类型时，可能会因为copy/paste而不小心继承错基类：

```cpp
class Derived1 : public Base<Derived1> {
    ...
};
 
class Derived2 : public Base<Derived1> { // bug in this line of code
    ...
};
```

解法很简单，将`Base`的构造函数声明为private，并将`T`设置为友元，这样`Derived2`根本就没办法调用`Base<Derived1>`的构造函数，从而制造编译错误：

```cpp
template <typename T>
class Base {
public:
    // ...
private:
    Base(){};
    friend T;
};
```

## 避免菱形继承

想象我们有两个mixin类型，都使用了CRTP来实现：

```cpp
template <typename T>
struct Scalable : private CRTP<T> {
    void Scale(double multiplicator) {
        this->Underlying().SetValue(this->Underlying().GetValue() * multiplicator);
    }
};
 
template <typename T>
struct Squarable : private CRTP<T> {
    void Square() {
        auto v = this->Underlying().GetValue();
        this->Underlying().SetValue(v * v);
    }
};
```

现在我们把这两个功能加到一个类型上：

```cpp
class Sensitivity : public Scalable<Sensitivity>, public Squarable<Sensitivity> {
public:
    double GetValue() const { return mValue; }
    void SetValue(double value) { mValue = value; }
 
private:
    double mValue;
};
```

BOOM！编译错误：

```
error: 'CRTP<Sensitivity>' is an ambiguous base of 'Sensitivity'
```

问题出在我们不小心搞出来菱形继承了！
* `Sensitivity` -> `Scalable<Sensitivity>` -> `CRTP<Sensitivity>`
* `Sensitivity` -> `Squarable<Sensitivity>` -> `CRTP<Sensitivity>`

一种解法是将mixin的类型也加到`CRTP`的模板参数中：

```cpp
template <typename T, template <typename> class CrtpType>
struct CRTP {
    T& Underlying() { return static_cast<T&>(*this); }
    T const& Underlying() const { return static_cast<T const&>(*this); }
private:
    CRTP() {}
    friend CrtpType<T>;
};
```

注意这里的`CrtpType`不是普通的模板参数类型，它前面的`template`说明它本身也是一个模板类型。我们没有直接用到`CrtpType`，只是用它保证同样的`T`加上不同的mixin会产生不同的`CRTP`类型。

新的`Sensitivity`的继承关系：
* `Sensitivity` -> `Scalable<Sensitivity>` -> `CRTP<Sensitivity, Scalable>`
* `Sensitivity` -> `Squarable<Sensitivity>` -> `CRTP<Sensitivity, Squarable>`

这样我们只要保证一个类型不要多次集成了同一个mixin，就没问题了。

# 相关链接

* [Curiously recurring template pattern](https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern)
* [The Curiously Recurring Template Pattern (CRTP)](https://www.fluentcpp.com/2017/05/12/curiously-recurring-template-pattern/)
* [What the Curiously Recurring Template Pattern can bring to your code](https://www.fluentcpp.com/2017/05/16/what-the-crtp-brings-to-code/)
* [An Implementation Helper For The Curiously Recurring Template Pattern](https://www.fluentcpp.com/2017/05/19/crtp-helper/)
