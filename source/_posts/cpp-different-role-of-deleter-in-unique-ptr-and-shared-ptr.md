---
title:      "C++：为什么unique_ptr的Deleter是模板类型参数，而shared_ptr的Deleter不是？"
date:       2017-05-19 23:26:51
tags:
    - C++
---

## 为什么`unique_ptr`的Deleter是模板类型参数，而`shared_ptr`的Deleter不是？

```cpp
template <class T, class D = default_delete<T>>
class unique_ptr {
public:
    ...
    unique_ptr (pointer p,
        typename conditional<is_reference<D>::value,D,const D&> del) noexcept;
    ...
};

template <class T> 
class shared_ptr {
public:
    ...
    template <class U, class D> 
    shared_ptr (U* p, D del);
    ...
};
```

上面的代码中能看到`unique_ptr`的第二个模板类型参数是Deleter，而`shared_ptr`的Delete则只是构造函数参数的一部分，并不是`shared_ptr`的类型的一部分。

为什么会有这个区别呢？

答案是效率。`unique_ptr`的设计目标之一是尽可能的高效，如果用户不指定Deleter，就要像原生指针一样高效。

Deleter作为对象的成员一般会有哪些额外开销？

1. 通常要存起来，多占用空间。
2. 调用时可能会有一次额外的跳转（相比`delete`或`delete[]`）。

`shared_ptr`总是要分配一个ControlBlock的，多加一个Deleter的空间开销也不大，第一条pass；`shared_ptr`在析构时要先原子减RefCount，如果WeakCount也为0还要再析构ControlBlock，那么调用Deleter析构持有的对象时多一次跳转也不算什么，第二条pass。

既然`shared_ptr`并不担心Deleter带来的额外开销，同时把Deleter作为模板类型的一部分还会导致使用上变复杂，那么它只把Deleter作为构造函数的类型就是显然的事情了。

而`unique_ptr`采用了“空基类”的技巧，将Deleter作为基类，在用户不指定Deleter时根本不占空间，第一条pass；用户不指定Deleter时默认的Deleter会是`default_delete`，它的`operator()`在类的定义内，会被inline掉，这样调用Deleter时也就没有额外的开销了，第二条pass。

因此`unique_ptr`通过上面两个技巧，成功的消除了默认Deleter可能带来的额外开销，保证了与原生指针完全相同的性能。代价就是Deleter需要是模板类型的一部分。

### 相关文档

* [Why does unique_ptr take two template parameters when shared_ptr only takes one?](http://stackoverflow.com/questions/21355037/why-does-unique-ptr-take-two-template-parameters-when-shared-ptr-only-takes-one)
* [Why does unique_ptr have the deleter as a type parameter while shared_ptr doesn't?](http://stackoverflow.com/questions/6829576/why-does-unique-ptr-have-the-deleter-as-a-type-parameter-while-shared-ptr-doesn)

## `unique_ptr`是如何使用空基类技巧的

我们参考clang的实现来学习一下`unique_ptr`使用的技巧。

```cpp
template <class _Tp, class _Dp = default_delete<_Tp> >
class unique_ptr
{
public:
    typedef _Tp element_type;
    typedef _Dp deleter_type;
    typedef typename __pointer_type<_Tp, deleter_type>::type pointer;
private:
    __compressed_pair<pointer, deleter_type> __ptr_;
    ...
};
```

忽略掉`unique_ptr`中的各种成员函数，我们看到它只有一个成员变量`__ptr__`，类型是`__compressed_pair<pointer, deleter_type>`。我们看看它是什么，是怎么省掉了Deleter的空间的。

```cpp
template <class _T1, class _T2>
class __compressed_pair
    : private __libcpp_compressed_pair_imp<_T1, _T2> {
    ...
};
```
`__compressed_pair`没有任何的成员变量，就说明它的秘密藏在了它的基类中，我们继续看。

```cpp
template <class _T1, class _T2, unsigned = __libcpp_compressed_pair_switch<_T1, _T2>::value>
class __libcpp_compressed_pair_imp;
```

`__libcpp_compressed_pair_imp`有三个模板类型参数，前两个是传入的`_T1`和`_T2`，第三个参数是一个无符号整数，它是什么？我们往下看，看到了它的若干个特化版本：

```cpp
template <class _T1, class _T2>
class __libcpp_compressed_pair_imp<_T1, _T2, 0>
{
private:
    _T1 __first_;
    _T2 __second_;
    ...
};

template <class _T1, class _T2>
class __libcpp_compressed_pair_imp<_T1, _T2, 1>
    : private _T1
{
private:
    _T2 __second_;
    ...
};

template <class _T1, class _T2>
class __libcpp_compressed_pair_imp<_T1, _T2, 2>
    : private _T2
{
private:
    _T1 __first_;
    ...
};

template <class _T1, class _T2>
class __libcpp_compressed_pair_imp<_T1, _T2, 3>
    : private _T1,
      private _T2
{
    ...
};
```

看起来第三个参数有4种取值，分别是：

* 0: 没有基类，两个成员变量。
* 1: 有一个基类`_T1`，和一个`_T2`类型的成员变量。
* 2: 有一个基类`_T2`，和一个`_T1`类型的成员变量。
* 3: 有两个基类`_T1`和`_T2`，没有成员变量。

`__compressed_pair`继承自`__libcpp_compressed_pair_imp<_T1, _T2>`，没有指定第三个参数的值，那么这个值应该来自`__libcpp_compressed_pair_switch<_T1, _T2>::value`。我们看一下`__libcpp_compressed_pair_switch`是什么：

```cpp
template <class _T1, class _T2, bool = is_same<typename remove_cv<_T1>::type,
                                                     typename remove_cv<_T2>::type>::value,
                                bool = is_empty<_T1>::value
                                       && !__libcpp_is_final<_T1>::value,
                                bool = is_empty<_T2>::value
                                       && !__libcpp_is_final<_T2>::value
         >
struct __libcpp_compressed_pair_switch;

template <class _T1, class _T2, bool IsSame>
struct __libcpp_compressed_pair_switch<_T1, _T2, IsSame, false, false> {enum {value = 0};};

template <class _T1, class _T2, bool IsSame>
struct __libcpp_compressed_pair_switch<_T1, _T2, IsSame, true, false>  {enum {value = 1};};

template <class _T1, class _T2, bool IsSame>
struct __libcpp_compressed_pair_switch<_T1, _T2, IsSame, false, true>  {enum {value = 2};};

template <class _T1, class _T2>
struct __libcpp_compressed_pair_switch<_T1, _T2, false, true, true>    {enum {value = 3};};

template <class _T1, class _T2>
struct __libcpp_compressed_pair_switch<_T1, _T2, true, true, true>     {enum {value = 1};};
```

`__libcpp_compressed_pair_switch`的三个bool模板参数的含义是：

1. `_T1`和`_T2`在去掉顶层的`const`和`volatile`后，是不是相同类型。
2. `_T1`是不是空类型。
3. `_T2`是不是空类型。

满足以下条件的类型就是空类型：

1. 不是union；
2. 除了size为0的位域之外，没有非static的成员变量；
3. 没有虚函数；
4. 没有虚基类；
5. 没有非空的基类。

可以看到，在`_T1`和`_T2`不同时，它们中的空类型就会被当作`__compressed_pair`的基类，就会利用到C++中的“空基类优化“。

那么在`unique_ptr`中，`_T1`和`_T2`都是什么呢？看前面的代码，`_T1`就是`__pointer_type<_Tp, deleter_type>::type`，而`_T2`则是Deleter，在默认情况下是`default_delete<_Tp>`。

我们先看`__pointer_type`是什么：

```cpp
namespace __pointer_type_imp
{

template <class _Tp, class _Dp, bool = __has_pointer_type<_Dp>::value>
struct __pointer_type
{
    typedef typename _Dp::pointer type;
};

template <class _Tp, class _Dp>
struct __pointer_type<_Tp, _Dp, false>
{
    typedef _Tp* type;
};

}  // __pointer_type_imp

template <class _Tp, class _Dp>
struct __pointer_type
{
    typedef typename __pointer_type_imp::__pointer_type<_Tp, typename remove_reference<_Dp>::type>::type type;
};
```

可以看到`__pointer_type<_Tp, deleter_type>::type`就是`__pointer_type_imp::__pointer_type<_Tp, typename remove_reference<_Dp>::type>::type`。这里我们看到了`__has_pointer_type`，它是什么？

```cpp
namespace __has_pointer_type_imp
{
    template <class _Up> static __two __test(...);
    template <class _Up> static char __test(typename _Up::pointer* = 0);
}
```

简单来说`__has_pointer_type`就是：如果`_Up`有一个内部类型`pointer`，即`_Up::pointer`是一个类型，那么`__has_pointer_type`就返回`true`，例如`pointer_traits::pointer`，否则返回`false`。

大多数场景下`_Dp`不会是`pointer_traits`，因此`__has_pointer_type`就是`false`，`__pointer_type<_Tp, deleter_type>::type`就是`_Tp*`，我们终于看到熟悉的原生指针了！

`_T1`是什么我们已经清楚了，就是`_Tp*`，它不会是空基类。那么`_T2`呢？我们看`default_delete<_Tp>`：

```cpp
template <class _Tp>
struct default_delete
{
    template <class _Up>
        default_delete(const default_delete<_Up>&,
             typename enable_if<is_convertible<_Up*, _Tp*>::value>::type* = 0) _NOEXCEPT {}
    void operator() (_Tp* __ptr) const _NOEXCEPT
        {
            static_assert(sizeof(_Tp) > 0, "default_delete can not delete incomplete type");
            static_assert(!is_void<_Tp>::value, "default_delete can not delete incomplete type");
            delete __ptr;
        }
};
```

我们看到`default_delete`符合上面说的空类型的几个要求，因此`_T2`就是空类型，也是`__compressed_pair`的基类，在”空基类优化“后，`_T2`就完全不占空间了，只占一个原生指针的空间。

而且`default_delete::operator()`是定义在`default_delete`内部的，默认是inline的，它在调用上的开销也被省掉了！

### 遗留问题

1. `__libcpp_compressed_pair_switch`在`_T1`和`_T2`类型相同，且都是空类型时，为什么只继承自`_T1`，而把`_T2`作为成员变量的类型？
2. `unique_ptr`与`pointer_traits`是如何交互的？
