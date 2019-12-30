---
title:      "The Interface Principle in C++"
date:       2017-09-28 09:56:49
tags:
    - C++
---

# The Interface Principle in C++

[原文地址](https://www.fluentcpp.com/2017/06/20/interface-principle-cpp/)

## 背景

C++的接口原则对于写出既有表现力，又保证了封装性的代码是非常重要的，当前即有语言特性与之相关，且未来还可能有更多特性来增强这一原则，值得我们注意。

本文用到的名词：

* 方法：类的成员函数。
* 函数：非成员函数。

## 非成员（非友元）函数

Effective C++的Item23中，Scott Meyers鼓励我们将只需要类的公开方法就能实现的函数移到类外面。下面是一个例子：

```cpp
class Circle
{
public:
    explicit Circle(double radius) : m_radius(radius) {}
 
    double getRadius() const {return m_radius;}
    double getPerimeter() const {return 2 * Pi * m_radius;}
    double getArea() const {return Pi * m_radius * m_radius;}
 
private:
    double m_radius;
};
```

注意下面两个方法，都只用到了`Circle`的其它公开方法：

```cpp
double getPerimeter() const {return 2 * Pi * getRadius();}
double getArea() const {return Pi * getRadius() * getRadius();}
```

那么把它们移到`Circle`外面作为非成员函数，就遵守了Meyers原则，增强了`Circle`类的封装性：

```cpp
class Circle
{
public:
    explicit Circle(double radius) : m_radius(radius) {}
 
    double getRadius() const {return m_radius;}
 
private:
    double m_radius;
};
 
double getPerimeter(Circle const& circle) {return 2 * Pi * circle.getRadius();}
double getArea(Circle const& circle) {return Pi * circle.getRadius() * circle.getRadius();
```

另一方面，这样也减少了`Circle`本身的代码量，重构时涉及的代码更少，更稳定。

以下是应用这一原则的步骤：

* 确认指定方法是否只依赖类的其它公开方法（或改起来也比较容易）。
* 创建一个同名的非成员函数。
* 将该类型作为函数的**第一个参数**：
    * 如果原方法不是const方法，参数类型就是非const引用。
    * 如果原方法是const方法，参数类型就是const引用。
* 将实现代码复制过来，并在每个调用类公开方法的地方加上参数的名字。

注意，要保证新函数与旧方法**同名**。有时候我们会不喜欢给一个非成员函数起名为`getPerimeter`，更愿意起名为`getCirclePerimeter`，这样会显得更具体一些。但这是错的：“Circle”已经出现在第一个参数的类型中了，不管是对人还是对编译器，都不需要在函数名上再加一个“Circle”了。`getPerimeter(circle)`看起来也要比`getCirclePerimeter(circle)`更自然。

## 接口原则

新的`Circle`类有点令人不安：它有在类外面的功能。这是我们在上一节有意做的，但通常来说类的功能不就是它的接口吗？

上面说对了一半，类的功能就应该是它的接口。但接口也不仅仅包括类的公开方法。这就是“接口原则”要说的。Herb Sutter在Exceptional C++的Item31-34中详细解释了这一原则（见相关文档1和2）。

满足以下条件的非成员函数也是类接口的一部分：

* 它的某个参数是该类的对象。
* 它与该类在**相同的命名空间**。
* 它与该类一同发布，即它们声明在**相同的头文件**。

上节中的`getPerimeter`和`getArea`就满足这些条件，因此它们也是`Circle`接口的一部分。换句话说，下面两种调用方式，差别只在于语法：

```cpp
getPerimeter(circle);
```

VS

```cpp
circle.getPerimeter();
```

根据接口原则，这两种表达方式都是在调用`Circle`类的`getPerimeter`功能。

## ADL（参数依赖查找）：接口原则与命名空间配合良好

当引入命名空间之后，接口原则可能会有问题：调用函数时要加命名空间，而方法则不用。也就是函数与方法开始有不一致了：

```cpp
namespace geometry
{
 
class Circle
{
public:
    explicit Circle(double radius) : m_radius(radius) {}
 
    double getRadius() const {return m_radius;}
 
private:
    double m_radius;
};
 
double getPerimeter(Circle const& circle) {return 2 * Pi * circle.getRadius();}
double getArea(Circle const& circle) {return Pi * m_radius * circle.getRadius();}
 
} // end of namespace geometry
```

现在函数的调用方式：

```cpp
geometry::getArea(circle);
```

而如果是方法的话，调用方式：

```cpp
circle.getArea();
```

这两者的不一致对接口原则而言是一种挑战，因为接口原则需要函数与方法间只有语法上的区别，不应该有其它信息上的区别。

幸好C++有参数依赖查找（ADL），又称Koenig查找：将参数类型所在的命名空间的所有函数声明带到当前作用域，来解决名字查找的问题。上例中，在查找`getArea`时，`circle`触发了ADL，从而`geometry`的所有声明被带到当前作用域，其中也包括了`getArea`，因此不加命名空间编译器仍然能找到这个函数：

```cpp
getArea(circle);
```

## 泛型代码

泛型代码中，非成员函数能发挥更大的作用。

前文中我们说过不建议在函数名字中嵌入参数类型的名字。事实上名字起的通用一些还有助于将其用于泛型代码。假设你还有一个`Rectangle`类也能计算周长，因此你会为其实现一个`getPerimeter`：

```cpp
double getPerimeter(Rectangle const& rectangle);
```

之后我们就可以把`getPerimeter`用到泛型代码中了：

```cpp
template <typename Shape>
void operateOnShape(Shape const& shape)
{
    double perimeter = getPerimeter(shape);
    ....
}
```

而且，有些类型不是类（比如内置类型）或你没办法给它加方法（比如三方库代码），这时候想为其增加一个泛型代码中可以用的功能，唯一可行的方法就是通过非成员函数。例如C++11增加的`std::begin`和`std::end`，设计为非成员函数的一大因素就是为了处理内置数组类型。

（实际上这就是另一种实现OO和多态的思路：Traits，很多人觉得它是比继承更好的OO方案，比如Rust就只有Traits没有继承。C++中用Traits还能减少类型间的耦合。）

## C++的统一函数调用语法？

C++已经有一些语言特性在支持接口原则了，ADL就是其中最显眼的一个。未来还可能会有更多语言特性与接口原则相关。

`std::invoke`（C++17）允许你用一套语法同时处理方法和函数：

```cpp
std::invoke(f, x, x1, ..., xn);
```

* 如果`f`是方法，则调用`x.f(x1, ..., xn)`。
* 如果`f`是函数，则调用`f(x, x1, ..., xn)`。

已经有提案（见相关文档3）建议语言中直接支持以下两种语法的等价性：

```cpp
f(x, x1, ..., xn);
```

如果`f`是`x`的一个方法，则等价于`x.f(x1, ..., xn)`。而：

```cpp
x.f(x1, ..., xn);
```

如果`f`是函数，则等价于`f(x, x1, ..., xn)`。

## 相关文档

- [What's In a Class? - The Interface Principle](http://www.gotw.ca/publications/mill02.htm)
- [Namespaces and the Interface Principle](http://www.gotw.ca/publications/mill08.htm)
- [Unified Call Syntax: x.f(y) and f(x,y)](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4474.pdf)

# What's In a Class

本节主要介绍下这篇文章没有被前面内容覆盖到的东西。

## C风格的OO

接口原则实际上起源于C风格的OO。例子：

```c
struct _iobuf { /*...data goes here...*/ };
typedef struct _iobuf FILE; 

FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* stream);
int fseek (FILE* stream, long offset, int origin);
long ftell (FILE* stream);
```

这里`FILE`就是一个类型，而`fopen`、`fclose`、`fseek`、`ftell`是它的公开方法，也就是它的接口。它的C++形式为：

```cpp
class FILE {
public:
    FILE(const char* filename, const char* mode);
    ~FILE();
    int seek(long offset, int origin);
    long tell();
    ...
};
```

从接口的角度，这两种形式没有什么区别。

## 类依赖什么

假设我们为类`X`实现`operator<<`，有两种方式：

```cpp
class X {
    ...
};
ostream& operator<<(ostream& o, const X& x) {
    ...
    return o;
}
```

以及

```cpp
class X {
public:
    virtual ostream& print(ostream& o) {
        ...
        return o;   
    }
    ...
};
ostream& operator<<(ostream& o, const X& x) {
    return x.print();
}
```

传统上我们会认为第一种方式更好，因为`X`没有依赖`ostream`。但实际上对吗？

1. 根据接口原则，`operator<<`参数中有`X`，且和`X`一同被引入，它就是`X`的一部分。
1. `operator<<`参数中有`ostream`，因此`operator<<`依赖于`ostream`。
1. 因此`X`也依赖于`ostream`。

所以第一种方式根本没有减少`X`的依赖。

## 一些有意思的结果

如果`A`和`B`是类，而`f(A, B)`是一个非成员函数：

* 如果`A`与`f`在一起，那么`f`就是`A`的一部分，那么`A`就依赖`B`。
* 如果`B`与 `f`在一起，那么`B`就依赖`A`。
* 如果三个都在一起，那么`A`与`B`就相互依赖。

下面更有意思。假设有类`A`和`B`，且`A`有方法`A::g(B)`，那么有：

* 显然`A`依赖`B`。
* 假设`A`和`B`在一起，`A::g(B)`的参数中有`B`，且和`B`在一起，那么`A::g(B)`也是`B`的一部分。显然`A::g(B)`的参数中也有`A`，即`B`的一部分依赖`A`，那么`B`也依赖`A`。因此`A`和`B`是相互依赖的关系。

## “PartOf”的关系到底有多强

接口原则一直在强调非成员函数也可以是类接口的“一部分”，那么这个关系到底有多强？

答案是比成员函数低一些。
