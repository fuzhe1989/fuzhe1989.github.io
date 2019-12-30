---
title:      "C++：关于函数的类型、完美转发、普适引用与引用折叠的一些尝试和解释"
date:       2017-08-24 23:48:02
tags:
    - C++
---

## 起因

上个月我做了一个实验，[对比了C++中几种Callable实现方式的性能](/2017/07/21/cpp-callable-implementations-benchmark/)。今天我想继续这个实验，给`Worker`类加一个接受右值的构造函数。

下文中的编译环境都是Apple LLVM version 8.1.0 (clang-802.0.42)。

## 第一次尝试

我的第一版代码长这样（精简后）：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};

template <typename HandlerT>
void Test(HandlerT&& h) {
    Worker<HandlerT> worker{std::forward<HandlerT>(h)};
}

void Func(int x) {}

int main(int argc, char** argv) {
    Test(Func);
}
```

编译居然出错：

```
➜ > g++ -std=c++11 func_pointer_ctor.cpp
func_pointer_ctor.cpp:12:14: error: multiple overloads of 'Worker' instantiate to the same signature 'void (void (&&)(int))'
    explicit Worker(HandlerT&& handler) {}
             ^
func_pointer_ctor.cpp:17:22: note: in instantiation of template class 'Worker<void (&)(int)>' requested here
    Worker<HandlerT> worker{std::forward<HandlerT>(h)};
                     ^
func_pointer_ctor.cpp:23:5: note: in instantiation of function template specialization 'Test<void (&)(int)>' requested here
    Test(Func);
    ^
func_pointer_ctor.cpp:11:14: note: previous declaration is here
    explicit Worker(const HandlerT& handler) {}
             ^
1 error generated.
```

但是把代码中的`Test(Func)`换成`Test([](int) {})`就没有问题。看出错信息是说当我传入一个函数时，它实例化出来的`Worker`的两个构造函数是一样的。

## 第二次尝试

那么我专门为`Func`特化一个版本好了：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};

using FuncT = void (int);
template <>
struct Worker<FuncT> {
    explicit Worker(FuncT func) {}
};
...
```

这里我专门为`void (int)`特化了一个`Worker`版本，只有一个构造函数，这回应该没问题了吧。

编译还是出错：

```
➜ > g++ -std=c++11 func_pointer_ctor.cpp
func_pointer_ctor.cpp:12:14: error: multiple overloads of 'Worker' instantiate to the same signature 'void (void (&&)(int))'
    explicit Worker(HandlerT&& handler) {}
             ^
func_pointer_ctor.cpp:23:22: note: in instantiation of template class 'Worker<void (&)(int)>' requested here
    Worker<HandlerT> worker{std::forward<HandlerT>(h)};
                     ^
func_pointer_ctor.cpp:29:5: note: in instantiation of function template specialization 'Test<void (&)(int)>' requested here
    Test(Func);
    ^
func_pointer_ctor.cpp:11:14: note: previous declaration is here
    explicit Worker(const HandlerT& handler) {}
             ^
1 error generated.
```

看出错信息，编译器并没有实例化我们的特化版本，这说明我们特化的类型有问题。仔细看错误信息，其中有`Worker<void (&)(int)>`，与我们上面用的`void (int)`不同。

## 第三次尝试

这回我们把`FuncT`的类型改为`void (&)(int)`再试一下：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};

using FuncT = void (&)(int);
template <>
struct Worker<FuncT> {
    explicit Worker(FuncT func) {}
};
...
```

这回编译终于没有问题了。现在我想改一下调用`Test`时传入的参数：

```cpp
...
int main(int argc, char** argv) {
    auto f = Func;
    Test(f);
}
```

编译居然又出错了：

```
➜ > g++ -std=c++11 func_pointer_ctor.cpp
func_pointer_ctor.cpp:12:14: error: multiple overloads of 'Worker' instantiate to the same signature 'void (void (*&&)(int))'
    explicit Worker(HandlerT&& handler) {}
             ^
func_pointer_ctor.cpp:23:22: note: in instantiation of template class 'Worker<void (*&)(int)>' requested here
    Worker<HandlerT> worker{std::forward<HandlerT>(h)};
                     ^
func_pointer_ctor.cpp:30:5: note: in instantiation of function template specialization 'Test<void (*&)(int)>' requested here
    Test(f);
    ^
func_pointer_ctor.cpp:11:14: note: previous declaration is here
    explicit Worker(const HandlerT& handler) {}
             ^
1 error generated.
```

仔细看出错信息，里面的类型和之前的不一样了！现在是`Worker<void (*&)(int)>`了。

## 第四次尝试

这回我们再增加一种针对`void (*&)(int)`的特化：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};

using RefFuncT = void (&)(int);
template <>
struct Worker<RefFuncT> {
    explicit Worker(RefFuncT func) {}
};

using PRefFuncT = void (*&)(int);
template <>
struct Worker<PRefFuncT> {
    explicit Worker(PRefFuncT func) {}
};

template <typename HandlerT>
void Test(HandlerT&& h) {
    Worker<HandlerT> worker{std::forward<HandlerT>(h)};
}

void Func(int x) {}

int main(int argc, char** argv) {
    auto f = Func;
    Test(f);
    Test(Func);
}
```

编译成功，两种形式都支持。

## 函数的类型

`Func`的类型是`void (int)`，但在模板的类型推断中，传入的`Func`会退化为它对应的函数指针或函数引用。

在[Effective Modern C++ Item1](/2017/05/14/effective-modern-cpp-chapter1-iterm1-4/)中讲到：

> 另一个会退化为指针的类型是函数。函数会退化为函数指针，且规则与数组相同。例如：
> 
> ```cpp
> void someFunc(int, double);    // someFunc's type is void(int, double)
> 
> template <typename T>
> void f1(T param);
> 
> template <typename T>
> void f2(T& param);
> 
> f1(someFunc);                  // T and ParamType are both void (*)(int, double)
> f2(someFunc);                  // T is void(int, double), ParamType is void (&)(int, double)
> ```
> 

`Test`的形参类型是带引用的，因此我们在直接传入`Func`时得到的`HandlerT`就是`void (&)(int)`。

而`auto f = Func`中，根据`auto`的类型推断规则，`Func`会退化为函数指针，即`f`的类型是`void (*)(int)`，传入`Test`后得到的类型是`void (*&)(int)`。为什么这里多了个引用？见下节。

## 普适引用、引用折叠与完美转发

为了避免下面的代码太蛋疼，本小节假设有`using FuncT = void (int)`。

什么是引用折叠？参见[Effective Modern C++ Item28](/2017/08/22/effective-modern-cpp-chapter5-iterm27-30/)：

> 普适引用是个例外，C++有单独的规则来把类型推断中出现的引用的引用转换为单个引用，称为“引用折叠”。折叠规则为：
> 
> ```cpp
> T& &   => T&
> T& &&  => T&
> T&& &  => T&
> T&& && => T&&
> ```

说来你可能不信，上面的例子里出现了引用折叠。还记得我们一直在处理的编译错误是什么吗？`Worker`的两个构造函数的原型相同。我们再看一下这两个构造函数：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};
```

你可能会觉得奇怪，它们明明是不同的引用类型，为什么最终会变成相同的样子？

当我们调用`Test(Func)`时，编译器首先要实例化`Test`。`Func`的类型在模板类型推断时退化为函数指针，即`FuncT*`，而在普适引用的类型推断中，有以下规则：

> 在普适引用的类型推断中，如果实参是左值，那么`T`就是左值引用；如果实参是右值，那么`T`没有引用，就是这个类型本身。

因此`HandlerT`就是`FuncT*&`。因此我们实例化了`Worker<FuncT*&>`。代入到上面的两个构造函数，我们得到：

```cpp
struct Worker<FuncT*&> {
    explicit Worker(const FuncT*& & handler);
    explicit Worker(FuncT*& && handler);
};
```

再应用引用折叠，我们得到：

```cpp
struct Worker<FuncT*&> {
    explicit Worker(const FuncT*& handler);
    explicit Worker(FuncT*& handler);
};
```

一个是const引用，一个是非const引用。我们用于构造`Worker`的参数`h`的类型是`FuncT*&`，不带const，因此编译器不知道该选择哪个函数。

### 如果实参是const引用

那么`h`的类型就是`const FuncT*&`，那么两个构造函数会变成：

```cpp
struct Worker<const FuncT*&> {
    explicit Worker(const FuncT*& handler);
    explicit Worker(const FuncT*& handler);
};
```

完全一样了！

### 如果实参是右值引用

根据上面的普适引用类型推断规则，你可能会认为`HandlerT`是`FuncT`，两个构造函数的参数类型一个是`const FuncT&`，一个是`FuncT&&`，就不会出错了。可是又错了（错误就不列出来了）。原因在于，**一个右值引用变量本身是左值**，因此`HandlerT`的类型是`FuncT&& &`，即`FuncT&`。

注意看上面的规则，“如果实参是右值”而不是“如果实参是右值引用”。

### 如果实参是右值

这回我们终于得到了一个正确的程序，代码如下：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};

template <typename HandlerT>
void Test(HandlerT&& h) {
    Worker<HandlerT> worker{std::forward<HandlerT>(h)};
}

using FuncT = void (int);

void Func(int x) {}

FuncT* Make() {
    return Func;
}

int main(int argc, char** argv) {
    Test(Make());
}
```

### 如果不使用完美转发

如果我们不使用完美转发，即在代码中去掉`std::forward`，直接使用`Worker<HandlerT> worker{h}`，会发生什么？

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};

template <typename HandlerT>
void Test(HandlerT&& h) {
    Worker<HandlerT> worker{h};
}

using FuncT = void (int);

void Func(int x) {}

FuncT* Make() {
    return Func;
}

int main(int argc, char** argv) {
    Test(Make());
}
```

```
➜ > g++ -std=c++11 func_pointer_ctor.cpp
➜ >
```

编译成功了。但是不是与完美转发版本有相同的效果呢？我们运行两个版本，看一下结果：

```
➜ > ./perfect_forward
rvalue
➜ > ./right_ref
lvalue
```

有没有很惊讶？去掉了完美转发，我们调用的居然是const引用版本的构造函数。原因很简单，还是“右值引用本身是左值”。

## 所以结论是什么？

总结一下我们遇到的问题。

我们有一个模板类型`Worker`，它有两个构造函数，一个接受左值引用，一个接受右值引用：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {}
    explicit Worker(HandlerT&& handler) {}
};
```

然而，当模板参数为一个左值引用类型时，这两个构造函数的函数原型会产生冲突：

```
Handlert        => T&
const HandlerT& => const T& & => const T&
HandlerT&&      => T& &&      => T&
```

通常我们不会这么实例化模板类，但如果这次实例化是发生在一个完美转发函数中，众所周知，完美转发是要应用在普适引用上的，而普适引用的特性就是，如果实参是左值，形参就会被推断成左值引用。这样我们就很不幸的用左值引用类型来实例化`Worker`，导致出现上面的问题。

看起来问题出在我们并不希望用一个带引用的类型来实例化`Worker`，那就把引用去掉。我们可以用`std::remove_reference`来实现：

```cpp
template <typename HandlerT>
struct Worker {
    explicit Worker(const HandlerT& handler) {
        cout << "lvalue" << endl;
    }
    explicit Worker(HandlerT&& handler) {
        cout << "rvalue" << endl;
    }
};

template <typename HandlerT>
void Test(HandlerT&& h) {
    using T = typename std::remove_reference<HandlerT>::type;
    Worker<T> worker{std::forward<T>(h)};
}

using FuncT = void (int);

void Func(int x) {}

FuncT* Make() {
    return Func;
}

int main(int argc, char** argv) {
    Test(Func);
    Test(Make());
}
```

编译成功，运行一下：

```
➜ > g++ -std=c++11 func_pointer_ctor.cpp
➜ > ./a.out
lvalue
rvalue
```

非常完美！所以看起来我们得到以下几个结论：

1. 在用普适引用参数的类型构造一个模板类时，用`std::remove_reference`去掉它的引用。
2. 普适引用不是右值引用（参见[区分普适引用与右值引用](/2017/08/08/effective-modern-cpp-chapter5-iterm23-26/)），如果要实现完美转发，记得用`std::forward`。
3. 谨慎重载普适引用，如果要重载，[参考这里](/2017/08/22/effective-modern-cpp-chapter5-iterm27-30/#item27-%E7%86%9F%E6%82%89%E9%87%8D%E8%BD%BD%E6%99%AE%E9%80%82%E5%BC%95%E7%94%A8%E7%9A%84%E6%9B%BF%E4%BB%A3%E6%96%B9%E6%B3%95)，以及，确认你真的调用了预期的重载版本。
