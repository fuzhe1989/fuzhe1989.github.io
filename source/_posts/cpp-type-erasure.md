---
title:      "C++: Type Erasure"
date:       2017-10-29 17:26:20
tags:
    - C++
---

Type Erasure，直译就是“类型擦除”。什么时候需要擦除类型？当我们想令一些代码具备多态性质时，我们往往没办法保留对象本身的类型，而需要用一种通用的类型去使用它们，这个时候，就需要擦除对象原有的类型。

# Type Erasure的几种形式

## `void*`

在C语言中，很多通用算法函数都会使用`void*`作为参数类型，比如`qsort`，它的原型是：

```c
void qsort (void* base, size_t num, size_t size, int (*compare)(const void*,const void*));
```

为了使`qsort`有处理多种类型的能力，它只能把参数类型设为`void*`，这样我们可以用同一个`qsort`函数，处理各种各样的类型。代价就是对象原有的类型被擦除了，我们只能看到`void*`。

这种方法的缺点是，它不能保证类型安全。当我们擦除了一个对象的类型后，总会在某个时刻需要把它再找回来的。在`qsort`中，我们总是需要能拿到对象的正确类型的，才能进行正确的排序。而这个工作是通过`compare`完成的：

```c
int int_compare(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

int str_compare(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}
```

假设我们传递了错误的`compare`，谁能知道这件事？编译器不知道，因为你把类型擦除掉了。你自己也不知道，因为代码就是你写的。测试程序可能知道，也可能不知道，因为这个时候程序的行为是未定义的。

## 继承

在面向对象语言中，继承是最常见的Type Erasure。

```java
interface Counter {
    public void Increase(int v);
    public void Decrease(int v);
    public int Value();
};
...
public class Test {
    public static void down(Counter c) {
        int count = 0;
        int oldValue = c.Value();
        while (c.Value() != 0) {
            c.Decrease(1);
            count++;
        }
        Assert.assertEqual(count, oldValue);
    }
};
```

在`Test.down`中，我们只知道`c`的类型是`Counter`，但不知道它是哪个实现类型，这里它的类型就被擦除了。

继承当然是比`void*`要好的，因为我们操作对象时调用的是对象具体的实现API，换句话说，我们只擦除了调用处对象的类型，实际上它并没有丢掉自己的类型，也保证了类型安全性。

继承的问题在于，它的侵入性，即它要求每个实现类型都继承自某个基类。在很多情况下，这是很难做到的，或者是很别扭的。

比如说`RedApple`，一个红色的苹果，当我们想使用“红色”这个泛型概念时，它需要实现`Red`这个接口；而当我们想使用“苹果”这个概念时，它又需要实现`Apple`这个接口。某天当我们想使用“类球形”这个概念时，它又要实现`RoundLike`接口吗？

当接口一个又一个的出现时，有人会说，干脆我们到处传递`Object`吧，用的时候再down_cast成具体的类型。于是我们又回到了`void*`的时代。

尤其是，有些类型我们是没有办法改的，比如三方库中定义的类型，比如内置类型。这些情况下，继承就无能为力了。

## Duck Typing和Template

如果一个东西，走路像鸭子，叫声也像鸭子，那么它就是鸭子。换句话说，如果一个东西，满足我们对鸭子的所有要求，那么它就是鸭子。如果一个`T`，满足我们对`X`的所有要求，那么它就是`X`。这就是duck typing，即鸭子类型。

Python中大量应用了duck typing：

```python
class RedApple:

    def color(self):
        return 'red'

    def round_like(self):
        return True

def map_by_color(items):
    ret = defaultdict(list)
    for item in items:
        ret[item.color()].append(item)
    return ret

color_map = map_by_color([..., RedApple(), ...])
```

在`map_by_color`中，我们对`items`有两项要求：

1. 可遍历。
1. 其中每个元素都有`color`方法。

但不要求`items`或其中每个`item`继承自哪个特定的接口。

这也是Type Erasure，但明显比继承来得更自由。当然自由都是有代价的，duck typing的代价就是它的运行时性能损失。Python中每个对象都会保留自己的类型信息，在调用时进行动态绑定。Go的`interface`有着类似的用法，也有着类似的优缺点。

C++的模板也是一种duck typing：

```cpp
template <typename C>
int CountByColor(const C& container, Color color) {
    int count = 0;
    for (const auto& item: container) {
        if (item.Color() == color) {
            ++count;
        }
    }
    return count;
}
```

这里面有个模板参数`C`，我们对它的要求是：

1. 可遍历，具体来说是支持`begin(container)`和`end(container)`两种API。
1. 遍历出来的每个元素有`T Color() const`方法，且`T`与`Color`类型有合适的`operator==`函数存在。

所有满足这个条件的`C`都可以作为`CountByColor`的参数类型。

当然C++的模板与Python的duck typing还是有很大区别的，因为它并没有真的擦除掉元素类型：`C`是`CountByColor`原型的一部分。这样我们其实一直都保留着元素的具体类型信息，好处：

1. 完整的类型安全性，没有任何环节丢掉了类型信息。
1. 因此不需要动态绑定，所有环节都是静态的，没有运行时性能损失。

但也有坏处：

1. 模板类型会作为模板函数或模板类的原型的一部分，即`vector<int>`和`vector<double>`是两个类型，没办法用一个类型来表示，也就没办法实现出上面Python例子中的`map_by_color`函数。
1. 每次用不同的参数类型来实例化模板时，都会新生成一份代码，导致编译出来的二进制文件很大。

## C++中结合继承与Template的Type Erasure

在C++中我们可以结合继承与Template，实现出一种Type Erasure，它既有duck typing的优点，又可以将不同类型用同一种类型表示。

假设我们现在要重新设计上面的`Counter`接口，首先我们定义一个内部的基类，`Counter`的每个方法都对应它的一个虚函数：

```cpp
class CounterBase {
public:
    virtual ~CounterBase {}
    virtual void Increase(int v) = 0;
    virtual void Decrease(int v) = 0;
    virtual int Count() const = 0;
}
```

接下来我们使用模板实现一个通用的子类：

```cpp
template <typename T>
class CounterImpl: public CounterBase {
public:
    explicit CounterImpl(T t): mImpl(std::move(t)) {}
    void Increase(int v) override {
        mImpl.Increase(v);
    }
    void Decrease(int v) override {
        mImpl.Decrease(v);
    }
    int Count() const override {
        return mImpl.Count();
    }
private:
    T mImpl;
};
```

最后我们还要定义一个`Counter`类型，但它不需要有任何的虚函数，也不需要作为任何类型的基类：

```cpp
class Counter {
public:
    template <typename T>
    Counter(T t): mPtr(new CounterImpl(std::forward<T>(t))) {}
    void Increase(int v) {
        mPtr->Increase(v);
    }
    void Decrease(int v) {
        mPtr->Decrease(v);
    }
    int Count() const {
        return mPtr->Count();
    }
private:
    std::unique_ptr<CounterBase> mPtr;
};
```

然后我们就可以使用`Counter`来表示所有满足条件的类型了：

```cpp
Counter c1(ClassA{});
Counter c2(ClassB{5});
Counter c3 = ClassC{3, 6};
```

对于没有`Increase`、`Decrease`、`Count`接口的类型，比如内置类型`int`，我们还可以特化模板来满足要求：

```cpp
template <>
class CounterImpl<int>: public CounterBase {
    explicit CounterImpl(int v): mValue(v) {}
    void Increase(int v) override {
        mValue += v;
    }
    void Decrease(int v) override {
        mValue -= v;
    }
    int Count() const override {
        return mValue;
    }
private:
    int mValue;
};
```

然后我们就可以写：

```cpp
Counter c = 5;
```

是不是很赞？

# C++中Type Erasure的例子

## std::shared_ptr

我们知道`std::shared_ptr`的Deleter不是`std::shared_ptr`类型的一部分（参见[为什么unique_ptr的Deleter是模板类型参数，而shared_ptr的Deleter不是](/2017/05/19/cpp-different-role-of-deleter-in-unique-ptr-and-shared-ptr/)），这给使用者带来了很多好处（相比`std::unique_ptr`）：

* 对于`std::shared_ptr<T>`，使用者不需要知道`T`的完整类型（当然创建者需要）。
* 两个`std::shared_ptr<T>`对象类型相同，可以相互赋值，即使它们的Deleter类型不同。
* 销毁`T`使用的Deleter永远是来自创建`std::shared_ptr`的编译单元，跨DLL和so使用时不会有销毁问题。

它的秘诀就是Type Erasure。参考clang的实现，`std::shared_ptr`只有两个成员变量：

```cpp
template <typename _Tp>
class shared_ptr {
private:
    _Tp* __ptr__;
    __shared_weak_count* __cntrl__;
};
```

其中`__shared_weak_count`的定义为：

```cpp
class __shared_weak_count;
```

可以看到不包含Deleter的类型。实际上构造的类型是它的子类`__shared_ptr_pointer`：

```cpp
template <class _Tp, class _Dp, class _Alloc>
class __shared_ptr_pointer: public __shared_weak_count;
```

具体的实现略。可以看到这里就使用了我们上面提到的继承与Template结合的方法。

## std::function

`std::function`中使用了一个基类`__base`：

```cpp
template<class _Rp, class ..._ArgTypes>
class __base<_Rp(_ArgTypes...)> {
public:
    ...
    virtual _Rp operator()(_ArgTypes&& ...) = 0;
};
```

同样地，具体的子类通过模板来保留类型信息，而通过基类来实现统一的存储与调用。

## boost::any

`boost::any`是非常典型的应用了Type Erasure方法的类型。它允许你用一种类型来保存任何类型的对象，且通过`type_info`方法返回具体的对象类型。这样我们可以使用一个`boost::any`的容器保存任意类型的对象。它的实现很短，只有313行，很值得看一下。

## std::any

`std::any`是C++17引入的新类型，与`boost::any`的接口几乎完全相同，区别在于，它使用了SBO(Small Buffer Optimization)方法，可以把小对象直接构造在类型内部，性能更好。

## 基于Type Erasure实现Unified Call Syntax

假设我们想实现一个接口类型`Fooable`，它有一个方法`foo`，使得`Fooable::foo`和`foo(Fooable)`都可以用来表示`T::foo`和`foo(T)`两种调用方式，即：

```cpp
struct Member {
    void foo();
};

struct NonMember{};
void foo(const NonMember&);

Fooable member_erased {Member{}};
foo(member_erased);
member_erased.foo();

Fooable non_member_erased {non_member{}};
foo(non_member_erased);
non_member_erased.foo();
```

第一步我们先定义基类：

```cpp
class Storage {
    virtual void call() = 0;
};
```

第二步定义模板子类：

```cpp
template <typename T, bool HasMemberFoo = has_member_foo<T>::value>
struct StorageImpl: Storage {
   T m_t;
    
    StorageImpl (T t): m_t {std::move(t)} {}
    
    void call() override {
        m_t.foo();
    }
};
```

其中`has_member_foo`是用来判断`T::foo`是否存在的辅助类型：

```cpp
template <typename T, typename = void>
struct has_member_foo: std::false_type{};
    
template <typename T>
struct has_member_foo<T, std::void_t<decltype(std::declval<T>().foo())>> : std::true_type{};
```

当`T::foo()`是个合法的表达式时，`has_member_foo<T>::value`就是`true`，否则就是`false`。

然后我们为`false`准备一个特化版本：

```cpp
template <typename T>
struct StorageImpl<T, false> : Storage {
    T m_t;
        
    StorageImpl (T t) : m_t {std::move(t)} {}
    
    void call() override {
        foo(m_t);
    }
};
```

最后实现`Fooable`类型：

```cpp
class Fooable {
public:
    template <typename T>
    Fooable (T t) 
        : m_storage {std::make_unique<StorageImpl<T>>(std::move(t))}
    {}
    void foo() { m_storage->call(); }
private:
    std::unique_ptr<Storage> m_storage;
};

void foo(Fooable& f) { f->foo(); }
```

# 什么时候使用Type Erasure

简单来说，如果你有下面两个需求，你可能是需要Type Erasure的：

* 你需要用同一种方式处理不同的类型。
* 你需要用同一种类型或容器保存不同类型的对象。

然而在很多情况下，你可能只需要用`std::shared_ptr`或`std::function`就能达到这个目的，这个时候就不需要自己实现Type Erasure了。

# 相关文档

- [C++ type erasure](http://www.cplusplus.com/articles/oz18T05o/)
- [More C++ Idioms/Type Erasure](https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Type_Erasure)
- [C++ 'Type Erasure' Explained](http://davekilian.com/cpp-type-erasure.html)
- [Type Erasure with Merged Concepts](http://aherrmann.github.io/programming/2014/10/19/type-erasure-with-merged-concepts/)
- [Duck Typing vs. Type Erasure](http://nullprogram.com/blog/2014/04/01/)
- [Type erasure — Part I](https://akrzemi1.wordpress.com/2013/11/18/type-erasure-part-i/)
- [Type erasure — Part II](https://akrzemi1.wordpress.com/2013/12/06/type-erasure-part-ii/)
- [Type erasure — Part III](https://akrzemi1.wordpress.com/2013/12/11/type-erasure-part-iii/)
- [Type erasure — Part IV](https://akrzemi1.wordpress.com/2014/01/13/type-erasure-part-iv/)
- [Type erasure techniques](https://stackoverflow.com/questions/5450159/type-erasure-techniques)
- [Extreme type erasure via std::function](https://a4z.bitbucket.io/blog/2017/01/11/exterm_typererasure-using-std::function.html)