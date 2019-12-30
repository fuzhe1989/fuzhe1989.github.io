---
title:      "C++：使用mixin获得比继承+组合更好的性能"
date:       2018-07-22 17:00:52
tags:
    - C++
---

注：本文所使用编译器均为gcc4.1.2，即只能使用C++98标准。

## 背景

```cpp
struct Node {
    int mRow;
    int mCol;
    int mValue;

    Node(int row, int col, int value)
        : mRow(row), mCol(col), mValue(value)
    {}
};

class Iterator {
public:
    virtual ~Iterator() {}

    virtual Node GetValue() const = 0;
    virtual bool IsValid() const = 0;
    virtual void MoveNext() = 0;
};
```

我们有一个接口类`Iterator`，它有三个方法，可以返回`Node`类型的值。现在我们要用这个接口来遍历一个矩阵。

矩阵的类型是`vector<vector<int>>`，我们有几种不同的遍历需求：

```cpp
Iterator* NewRawIterator(const std::vector<std::vector<int> >& matrix);
```

这个方法返回一个最原始的`Iterator`，只是单纯遍历数据。

```cpp
Iterator* NewOddIterator(const std::vector<std::vector<int> >& matrix);
```

在`RawIterator`基础上，只返回奇数。

```cpp
Iterator* NewAppendPerRowIterator(const std::vector<std::vector<int> >& matrix, const std::vector<int>& appendix);
```

在`OddIterator`基础上，遍历过程中，在每行末尾加上`appendix`中的元素。

```cpp
Iterator* NewDoubleIterator(const std::vector<std::vector<int> >& matrix, const std::vector<int>& appendix);
```

在`AppendPerRowIterator`基础上，返回的每个`Node`的value都乘2。

## 测试程序

```cpp
int main(int argc, char** argv) {
    srand(time(NULL));
    vector<vector<int> > matrix;
    vector<int> appendix;
    PrepareMatrix(matrix, 1000, 10000);
    PrepareAppendix(appendix, 3);

    int64_t t0 = Measure(NewRawIterator(matrix));
    int64_t t1 = Measure(NewOddIterator(matrix));
    int64_t t2 = Measure(NewAppendPerRowIterator(matrix, appendix));
    int64_t t3 = Measure(NewDoubleIterator(matrix, appendix));

    cout << "Raw:" << t0 << endl
         << "Odd:" << t1 << endl
         << "Append:" << t2 << endl
         << "Double:" << t3 << endl;
}
```

我们会在一个1000*10000的矩阵上测试各个`Iterator`。

## 基于继承+组合的实现

上面的需求看起来很适合用继承+组合来实现：

```cpp
class RawIterator: public Iterator {
public:
    explicit RawIterator(const vector<vector<int> >& matrix);
    //...
};

class OddIterator: public Iterator {
public:
    explicit OddIterator(Iterator* iter);
    //...
};

class AppendPerRowIterator: public Iterator {
public:
    AppendPerRowIterator(Iterator* iter, const vector<int>& appendix);
    //...
};

class DoubleIterator: public Iterator {
public:
    explicit DoubleIterator(Iterator* iter);
    // ...
};
```

我们用了4个派生自`Iterator`的新类型来实现不同的需求，除了`RawIterator`外，其它3种类型都是在组合了已有的`Iterator`基础上实现新功能，因此可以任意顺序组合。

几个New函数的实现：

```cpp
Iterator* NewRawIterator(const vector<vector<int> >& matrix) {
    return new RawIterator(matrix);
}

Iterator* NewOddIterator(const vector<vector<int> >& matrix) {
    Iterator* rawIter = NewRawIterator(matrix);
    return new OddIterator(rawIter);
}

Iterator* NewAppendPerRowIterator(const vector<vector<int> >& matrix, const vector<int>& appendix) {
    Iterator* oddIter = NewOddIterator(matrix);
    return new AppendPerRowIterator(oddIter, appendix);
}

Iterator* NewDoubleIterator(const vector<vector<int> >& matrix, const vector<int>& appendix) {
    Iterator* appendIter = NewAppendPerRowIterator(matrix, appendix);
    return new DoubleIterator(appendIter);
}
```

测试结果为：

```
Raw:164311
Odd:316020
Append:436596
Double:458733
```

可以看到，每增加一层功能，调用延时就明显上涨了一截。有没有其它性能更好的方案了呢？

## mixin

mixin是给一组基础类，每个都建模了一个抽象概念，且能直接组合成一个新的类，像乐高一样，满足需求。如果你新增了基础类，只要它是和其它基础类正交的，就可以扩展到这个组合类里。

C++中的一种mixin方法是结合模板和继承，通过模板参数来组合基础类（见[通过mixin组合功能](/2018/04/21/mixin/)）。

在上面这个例子中，`RawIterator`是一个基础类，可以单独使用，而`OddIterator`、`AppendIterator`和`DoubleIterator`则是用来提供新的功能的，是mixin类，我们可以将这三个类型实现为模板类，以`OddIterator`为例：

```cpp
template <typename T>
class OddIterator: public T {
public:
    OddIterator(?)
        : T(?) {
        MoveUntilOdd();
    }

    Node GetValue() const {
        return T::GetValue();
    }

    bool IsValid() const {
        return T::IsValid();
    }

    void MoveNext() {
        T::MoveNext();
        MoveUntilOdd();
    }
private:
    void MoveUntilOdd() {
        while (T::IsValid() && T::GetValue().mValue % 2 == 0) {
            T::MoveNext();
        }
    }
};
```

可以看到，新的`OddIterator`是通过继承自`T`来给`T`增加功能的，这有什么好处？摘取上文中的一段：

> 不需要堆分配。
> 没有运行期检查null。
> 不需要显式管理生命期。
> 没有多余的虚函数定义及其调用。
> 编译器有机会做更多优化。

似乎一切都很美好，除了代码有些怪异外。但我们还有一个问题没有解决：怎么构造`T`？

## 一种mixin的通用构造方法

这是一个很大很复杂的课题，这里我只提出一种似乎可行的方式（至少本文的例子是没问题的）。

我们要求每个mixin类型，及需要与这个mixin类型组合的基础类型内部，都定义一个Context类型，且有一个只有一个Context类型参数的构造函数。例子：

```cpp
class RawIterator: public Iterator {
public:
    struct Context {
        const vector<vector<int> >& mMatrix;

        explicit Context(const vector<vector<int> >& matrix)
            : mMatrix(matrix) {}
    };

    explicit RawIterator(const Context& context);
    // ...
};

template <typename T>
class OddIterator: public T {
public:
    struct Context {
        const typename T::Context& mBase;

        explicit Context(const typename T::Context& base)
            : mBase(base) {}
    };

    OddIterator(const Context& c)
        : T(c.mBase)
    //...
};

template <typename T>
class AppendPerRowIterator: public T {
public:
    struct Context {
        const typename T::Context& mBase;
        const vector<int>& mAppendix;

        explicit Context(const typename T::Context& base, const vector<int>& appendix)
            : mBase(base), mAppendix(appendix) {}
    };
    AppendPerRowIterator(const Context& c)
       : T(c.mBase)
    //...
```

通过这种方式，我们可以在不知道`T`的具体类型（也就不知道它到底有什么样的构造函数）的情况下，构造出组合类型。从而也获得了任意组合这些mixin顺序的能力。

在C++11后，我们可以通过变长参数和继承构造函数的方式提供更优雅的mixin的通用构造方案，这是另外的话题，这里不展开了。

在使用了上面的方法来构造mixin后，几个New函数的实现为：

```cpp
Iterator* NewRawIterator(const vector<vector<int> >& matrix) {
    RawIterator::Context c(matrix);
    return new RawIterator(c);
}

Iterator* NewOddIterator(const vector<vector<int> >& matrix) {
    typedef OddIterator<RawIterator> OddIter;

    RawIterator::Context c0(matrix);
    OddIter::Context c1(c0);
    return new OddIter(c1);
}

Iterator* NewAppendPerRowIterator(const vector<vector<int> >& matrix, const vector<int>& appendix) {
    typedef OddIterator<RawIterator> OddIter;
    typedef AppendPerRowIterator<OddIter> AppendIter;

    RawIterator::Context c0(matrix);
    OddIter::Context c1(c0);
    AppendIter::Context c2(c1, appendix);
    return new AppendIter(c2);
}

Iterator* NewDoubleIterator(const vector<vector<int> >& matrix, const vector<int>& appendix) {
    typedef OddIterator<RawIterator> OddIter;
    typedef AppendPerRowIterator<OddIter> AppendIter;
    typedef DoubleIterator<AppendIter> DoubleIter;

    RawIterator::Context c0(matrix);
    OddIter::Context c1(c0);
    AppendIter::Context c2(c1, appendix);
    DoubleIter::Context c3(c2);
    return new DoubleIter(c3);
}
```

看起来还是很笨拙，但至少我们成功地把它们构造出来了。

## mixin的性能

重新编译，运行，基于mixin的结果为：

```
Raw:165505
Odd:164377
Append:187320
Double:191260
```

可以看到，在Raw结果不变的情况下（这是当然的），其它三种场景mixin方案的延时分别比继承+组合减少了48%、57%和58%，调用链越长，mixin方案的优势越大。

## 结论

本文提出了用mixin来解决功能组合的方案，且使用了Context类型来解决构造问题，相比常见的继承+组合的方案，mixin方案有着非常明显的性能优势。
