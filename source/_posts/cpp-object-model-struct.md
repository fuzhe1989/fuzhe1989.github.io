---
title:      "C++对象模型（二）struct/class的内存布局"
date:       2018-03-11 15:53:13
tags:
    - C++
---

* 运行环境：x86-64。
* 编译器：gcc4.8.5。
* 编译选项：-O2。
* 语言标准：以c++98为主，兼顾c++11/14。

# c++中的struct与c中的struct

第一个问题：c++中的struct与c中的struct相同吗？

答案是，有时相同，有时不同。

## 像c一样定义struct

如果我们简单的按照c的方式定义一个struct，如c代码：

```c
typedef struct {
    int8_t a;
    int64_t b;
    int32_t c;
} S;

int main() {
    S ss = {1, 2, 3};
    S s = ss;
    printf(
        "size:%d &a-&s:%d, &b-&s:%d &c-&s:%d\n",
        sizeof(s),
        (char*)&s.a - (char*)&s,
        (char*)&s.b - (char*)&s,
        (char*)&s.c - (char*)&s);
    return 0;
}
```

和c++代码：

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;
};

int main() {
    S ss = {1, 2, 3};
    S s = ss;
    printf(
        "size:%d &a-&s:%d, &b-&s:%d &c-&s:%d\n",
        sizeof(s),
        (char*)&s.a - (char*)&s,
        (char*)&s.b - (char*)&s,
        (char*)&s.c - (char*)&s);
    return 0;
}
```

分别用gcc和g++编译上面两段代码，结果是：

```
size:24 &a-&s:0, &b-&s:8 &c-&s:16

size:24 &a-&s:0, &b-&s:8 &c-&s:16
```

完全一样对不对？再看下汇编指令：

```
  400440:   48 83 ec 08             sub    $0x8,%rsp
  400444:   41 b8 10 00 00 00       mov    $0x10,%r8d
  40044a:   b9 08 00 00 00          mov    $0x8,%ecx
  40044f:   31 d2                   xor    %edx,%edx
  400451:   be 18 00 00 00          mov    $0x18,%esi
  400456:   bf f0 05 40 00          mov    $0x4005f0,%edi
  40045b:   31 c0                   xor    %eax,%eax
  40045d:   e8 ae ff ff ff          callq  400410 <printf@plt>
  400462:   31 c0                   xor    %eax,%eax
  400464:   48 83 c4 08             add    $0x8,%rsp
```

和

```
  400500:   48 83 ec 08             sub    $0x8,%rsp
  400504:   41 b8 10 00 00 00       mov    $0x10,%r8d
  40050a:   b9 08 00 00 00          mov    $0x8,%ecx
  40050f:   31 d2                   xor    %edx,%edx
  400511:   be 18 00 00 00          mov    $0x18,%esi
  400516:   bf b0 06 40 00          mov    $0x4006b0,%edi
  40051b:   31 c0                   xor    %eax,%eax
  40051d:   e8 ae ff ff ff          callq  4004d0 <printf@plt>
  400522:   31 c0                   xor    %eax,%eax
  400524:   48 83 c4 08             add    $0x8,%rsp
```

是不是也完全一样？当我们在c++里像c一样定义struct时，编译器会给我们一个与c的struct完全相同的结构。

## 为struct添加静态成员

我们为`S`添加一个静态成员变量与静态成员函数：

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;

    static int d;

    static void Print() {
        printf("%d\n", d);
    }
};

int S::d = 0;
```

仍然运行上面的`main`函数，结果就不贴了，我们会发现与c的struct仍然相同。这说明：

1. 静态成员变量不会影响struct布局。换句话说，静态成员变量不存在于对象内部。
2. 静态成员方法也不会影响struct布局，即也不存在于对象内部。

## 为struct添加非静态非虚成员函数

### 为struct添加构造、析构、复制、移动函数

假设我们向`S`中添加上述函数，上面的结论会有什么变化？

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;

    S(int8_t x, int64_t y, int32_t z) {
        a = x;
        b = y;
        c = z;
    }

    ~S() {
        a = 0;
        b = 0;
        c = 0;
    }

    S(const S& s) {
        a = s.a;
        b = s.b;
        c = s.c;
    }

    S(S&& s) {
        a = s.a;
        b = s.b;
        c = s.c;
    }
};
```

运行结果没有任何变化，汇编指令也完全相同。这说明struct加上构造函数并没有改变它的布局，即：

* 以上函数均不存在于对象内部。
* 以上函数均未带来额外开销。

但我们不能说这些函数对struct没有任何影响，后面会讲到，当我们添加了这些函数后：

* 这个struct不再满足POD的定义，即不再有bitwise语义。
* 如果上述函数未被定义为内联（inline）函数，则其会带来额外开销。

### 为struct添加普通非虚函数

我们比较以下两种写法。写法一：

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;
};

void Func(S* s) {
    s->a += 1;
    s->b -= 1;
    s->c *= 2;
    printf("a:%d b:%ld c:%d\n", s->a, s->b, s->c);
}
```

与写法二：

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;

    void Func() {
        a += 1;
        b -= 1;
        c *= 2;
        printf("a:%d b:%ld c:%d\n", a, b, c);
    }
};
```

对应的`main`函数为：

```cpp
int main() {
    S s = {1, 2, 3};
    s.Func(); // or Func(&s);
    printf(
        "size:%d &a-&s:%d, &b-&s:%d &c-&s:%d\n",
        sizeof(s),
        (char*)&s.a - (char*)&s,
        (char*)&s.b - (char*)&s,
        (char*)&s.c - (char*)&s);
}
```

写法一的结果：

```
a:2 b:1 c:6
size:24 &a-&s:0, &b-&s:8 &c-&s:16

  400500:   48 83 ec 28             sub    $0x28,%rsp
  400504:   48 89 e7                mov    %rsp,%rdi
  400507:   c6 04 24 01             movb   $0x1,(%rsp)
  40050b:   48 c7 44 24 08 02 00    movq   $0x2,0x8(%rsp)
  400512:   00 00
  400514:   c7 44 24 10 03 00 00    movl   $0x3,0x10(%rsp)
  40051b:   00
  40051c:   e8 1f 01 00 00          callq  400640 <_Z4FuncP1S>
  400521:   41 b8 10 00 00 00       mov    $0x10,%r8d
  400527:   b9 08 00 00 00          mov    $0x8,%ecx
  40052c:   31 d2                   xor    %edx,%edx
  40052e:   be 18 00 00 00          mov    $0x18,%esi
  400533:   bf 18 07 40 00          mov    $0x400718,%edi
  400538:   31 c0                   xor    %eax,%eax
  40053a:   e8 91 ff ff ff          callq  4004d0 <printf@plt>
  40053f:   31 c0                   xor    %eax,%eax
  400541:   48 83 c4 28             add    $0x28,%rsp
```

写法二的结果：

```
a:2 b:1 c:6
size:24 &a-&s:0, &b-&s:8 &c-&s:16

  400500:   48 83 ec 08             sub    $0x8,%rsp
  400504:   b9 06 00 00 00          mov    $0x6,%ecx
  400509:   ba 01 00 00 00          mov    $0x1,%edx
  40050e:   be 02 00 00 00          mov    $0x2,%esi
  400513:   bf c0 06 40 00          mov    $0x4006c0,%edi
  400518:   31 c0                   xor    %eax,%eax
  40051a:   e8 b1 ff ff ff          callq  4004d0 <printf@plt>
  40051f:   41 b8 10 00 00 00       mov    $0x10,%r8d
  400525:   b9 08 00 00 00          mov    $0x8,%ecx
  40052a:   31 d2                   xor    %edx,%edx
  40052c:   be 18 00 00 00          mov    $0x18,%esi
  400531:   bf d8 06 40 00          mov    $0x4006d8,%edi
  400536:   31 c0                   xor    %eax,%eax
  400538:   e8 93 ff ff ff          callq  4004d0 <printf@plt>
  40053d:   31 c0                   xor    %eax,%eax
  40053f:   48 83 c4 08             add    $0x8,%rsp
```

对比两种写法的结果，我们发现：

* 成员布局上，两者相同，即普通的非虚成员函数不存在于对象中，不会占用空间。
* 汇编指令上，第一种写法调用了`Func(S*)`，而第二种写法完全看不到`S::Func`，而是直接调用了`printf`。

上面的第二条发现，实际上就是inline的效果。c++标准规定了定义在类（无论是class还是struct）定义中的函数都默认带有inline效果，因此它被编译器直接展开到调用处了。

如果我们给`Func(S*)`前面加上`inline`，我们会得到与写法二完全相同的汇编指令（不贴了）。因此结论就是：

* 普通的非虚成员函数不会占用对象空间，也不会带来额外开销，与对应的非成员函数完全相同。
* `Func(S*)`等效于`S::Func()`。

针对上面的第二点，实际上`S::Func()`会被编译器变成一个非成员函数，类似为`S_Func(S* const this)`，而`S::Func() const`则对应`S_Func(const S* const this)`。

## 为struct添加虚成员函数

我们将`S::Func`改为一个虚函数：

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;

    virtual void Func() {
        a += 1;
        b -= 1;
        c *= 2;
        printf("a:%d b:%ld c:%d\n", a, b, c);
    }
};
```

对应的`main`函数不变，直接运行会报错：

```
struct.cpp: In function ‘int main()’:
struct.cpp:18:19: error: in C++98 ‘s’ must be initialized by constructor, not by ‘{...}’
     S s = {1, 2, 3};
                   ^
struct.cpp:18:19: error: could not convert ‘{1, 2, 3}’ from ‘<brace-enclosed initializer list>’ to ‘S’
```

似乎此时struct与c的struct已经不一样了。我们给它加上一个构造函数：

```cpp
S::S(int8_t x, int64_t y, int32_t z) {
    a = x;
    b = y;
    c = z;
}
```

就可以编译过了。先运行前面的`main`函数（构造那行要改），结果是：

```
size:32 &a-&s:8, &b-&s:16 &c-&s:24
```

我们发现：

* 加入虚函数后，对象变大了，说明虚函数占用了一部分对象空间。
* 对象变大了8字节（实际是虚表指针），且正好在对象的最前面，其它成员变量的位置依次向下8字节。

这是第一个与c的struct布局不同的场景。我们知道虚函数是为了实现运行期多态的，那么就需要有信息来帮助程序在运行期根据对象的不同而选择不同的行为，这种信息就会带来运行期的额外开销。

但调用虚函数真的就会有运行期开销吗？我们分别看一下直接通过对象来调用虚函数，与通过指针或引用调用虚函数的区别。

我们添加三个Test函数，并在main函数中调用它：

```cpp
void Test1(S s) {
    s.Func();
}

void Test2(S* s) {
    s->Func();
}

void Test3(S& s) {
    s.Func();
}
```

对应的汇编指令为：

```
0000000000400710 <_Z5Test11S>:
  400710:   0f b6 47 08             movzbl 0x8(%rdi),%eax
  400714:   8d 70 01                lea    0x1(%rax),%esi
  400717:   48 8b 47 10             mov    0x10(%rdi),%rax
  40071b:   40 88 77 08             mov    %sil,0x8(%rdi)
  40071f:   40 0f be f6             movsbl %sil,%esi
  400723:   48 8d 50 ff             lea    -0x1(%rax),%rdx
  400727:   8b 47 18                mov    0x18(%rdi),%eax
  40072a:   48 89 57 10             mov    %rdx,0x10(%rdi)
  40072e:   8d 0c 00                lea    (%rax,%rax,1),%ecx
  400731:   31 c0                   xor    %eax,%eax
  400733:   89 4f 18                mov    %ecx,0x18(%rdi)
  400736:   bf 20 08 40 00          mov    $0x400820,%edi
  40073b:   e9 20 fe ff ff          jmpq   400560 <printf@plt>

0000000000400740 <_Z5Test2P1S>:
  400740:   48 8b 07                mov    (%rdi),%rax
  400743:   48 8b 00                mov    (%rax),%rax
  400746:   ff e0                   jmpq   *%rax
  400748:   0f 1f 84 00 00 00 00    nopl   0x0(%rax,%rax,1)
  40074f:   00

0000000000400750 <_Z5Test3R1S>:
  400750:   48 8b 07                mov    (%rdi),%rax
  400753:   48 8b 00                mov    (%rax),%rax
  400756:   ff e0                   jmpq   *%rax
  400758:   0f 1f 84 00 00 00 00    nopl   0x0(%rax,%rax,1)
  40075f:   00
```

我们发现：

* `Test1`中`Func`被展开了，看不到直接的`Func`调用，这点与调用一个非虚函数的行为相同。
* `Test2`中通过间接跳转（jmpq）调用了`Func`，方法是先取出`s`（%rdi）的前8字节到%rax，再从%rax取出前8字节放到%rax，这就是`Func`的地址，之后就是一次间接跳转。
* `Test3`与`Test2`完全相同。

结论：

* 通过一个对象调用虚函数时，编译器没有采用运行期多态，而是直接像调用一个非虚函数一样，没有运行期开销。
* 通过指针调用虚函数时，有运行期开销，即需要一次间接跳转，此时虚函数无法展开。
* 引用与指针此处无区别，引用就是一种语法糖。

## 为struct添加一个非虚继承的基类

### 为struct添加一个无虚函数的非虚继承基类

我们修改一下`S`：

```cpp
struct Base {
    int32_t ba;
};
struct S: public Base {
    int8_t a;
    int64_t b;
    int32_t c;
};
```

运行前面的`main`函数，结果为：

```
size:24 &a-&s:4, &b-&s:8 &c-&s:16
```

此时`S`的布局可以认为是：

```cpp
struct S {
    int32_t ba;
    int8_t a;
    int64_t b;
    int32_t c;
};
```

或

```cpp
struct S {
    Base base;
    int8_t a;
    int64_t b;
    int32_t c;
};
```

两者的区别在于，前者是基类的所有成员都可以被当作子类的成员，而后者是基类子对象就是子类的第一个成员。

到底是哪种呢？当基类为：

```cpp
struct Base {
    int32_t ba;
    int8_t bb;
};
```

时，如果按前者，`Base::bb`和`S::a`之间应该没有padding，即此时`S`的大小仍然是24；如果按后者，`Base`的alignment为8，此时`Base::bb`后面会有padding，`S`的大小应该是32。我们试一下，结果为：

```
size:32 &a-&s:8, &b-&s:16 &c-&s:24
```

说明：基类子对象可以被当作子类对象的第一个成员，且保持自己的alignment和padding。

### 为struct添加第二个非虚基类

我们再加一个基类：

```cpp
struct Base {
    int32_t ba;
    int8_t bb;
};
struct Base2 {
    int32_t ca;
    int8_t cb;
};
struct S: public Base, public Base2 {
    int8_t a;
    int64_t b;
    int32_t c;
};
```

根据上节的结论，我们可以认为`Base2`也是`S`的一个成员，且应排列在`Base`后面，`S`的大小应该是40。实验结果为：

```
size:40 &a-&s:16, &b-&s:24 &c-&s:32
```

证实了我们的猜测。

### 为struct添加空的基类

所谓空类型，指：

* 没有任何非静态成员变量。
* 没有任何虚函数。
* 没有任何虚基类。
* 其上没有基类，或只有空基类。

当我们给`S`添加一个空类型的基类时，如：

```cpp
struct Base {
};
struct S: public Base {
    int64_t b; // 注意该场景中没有成员a
    int32_t c;
};
```

根据之前的结论，`S`应该相当于：

```cpp
struct S {
    Base base;
    int64_t b;
    int32_t c;    
};
```

我们知道，c++中任何类型的size都至少是1，这是为了避免不同变量对应相同的内存地址。那么`base`的size就是1，`S`的size就应该是24。实际上呢？

```
size:16 &b-&s:0 &c-&s:8
```

居然是16！这就是c++的空基类优化（Empty Base Optimization，EBO），当基类子对象为空时，其不必在子类对象中占据空间，且与子类对象共享相同的地址。这里是一个c的oop无法模拟的点。

## 为struct添加有虚函数的非虚继承基类

### 基类无成员变量

```cpp
struct Base {
    virtual ~Base() {}
};
struct S: public Base {
    int8_t a;
    int64_t b;
    int32_t c;
};
int main() {
    S s;
    printf(
        "Base:%d S:%d &base-&s:%d &a-&s:%d &b-&s:%d &c-&s:%d\n",
        sizeof(Base),
        sizeof(S),
        (char*)static_cast<Base*>(&s) - (char*)&s,
        (char*)&s.a - (char*)&s,
        (char*)&s.b - (char*)&s,
        (char*)&s.c - (char*)&s);
}
```

结果：

```
Base:8 S:32 &base-&s:0 &a-&s:8 &b-&s:16 &c-&s:24
```

结论：

* 有虚函数的类型，其对象中需要有1个虚表指针来存放运行期信息，不再是空类型，作为基类也没办法应用EBO。
* 子类对象中不会有2个虚表指针（基类子对象1个，子类对象1个），而是与基类共用1个虚表指针。

### 基类有成员变量

假设我们给基类添加一个成员变量：

```cpp
struct Base {
    virtual ~Base() {}
    int8_t ba;
};
struct S: public Base {
    int8_t a;
    int64_t b;
    int32_t c;
};
```

根据前文规则，`Base`大小为16，其alignment为8，`S`的大小就会是40，且`Base::bb`与`S::a`之间有padding。但运行结果却是：

```
Base:16 S:32 &base-&s:0 &a-&s:9 &b-&s:16 &c-&s:24
```

与我们的猜测不符，`Base::bb`与`S::a`之间没有padding。

后面我们会说到c++有一种标准布局（Standard Layout），这种布局需要与c的struct布局兼容（外加空基类优化）。而当`Base`或`S`中加入虚函数后，它们就不再符合标准布局了，编译器就可以应用更紧凑的布局了。

为什么标准布局需要与c的struct布局兼容？因为POD（Plain Old Data）类型首先需要是标准布局类型，而POD类型本身就是为了与c兼容而提出的概念。

## 为struct添加虚继承基类

注：大多数c++项目都禁止使用虚继承，因此下面的几个场景我们只给输出和大概的结论，不进行更多的探索和解释了。

### 基类为空类型

```cpp
struct Base {
};
struct S: public virtual Base {
    int8_t a;
    int64_t b;
    int32_t c;
};
```

输出：

```
Base:1 S:32 &base-&s:0 &a-&s:8 &b-&s:16 &c-&s:24
```

结论：

* 虚基类会在子类中占用额外空间（1个指针），位置在子类最前面，此时无法应用EBO。

### 基类为非空无虚函数类型

```cpp
struct Base {
    int8_t ba;
};
struct S: public virtual Base {
    int8_t a;
    int64_t b;
    int32_t c;
};
```

输出：

```
Base:1 S:32 &base-&s:28 &a-&s:8 &b-&s:16 &c-&s:24
```

结论：

* 子类对象最前面仍然是1个指针。
* 此时基类子对象位于子类最后。

### 基类为非空有虚函数类型

```cpp
struct Base {
    virtual ~Base() {}
    int8_t ba;
};
struct S: public virtual Base {
    int8_t a;
    int64_t b;
    int32_t c;
};
```

输出：

```
Base:16 S:48 &base-&s:32 &a-&s:8 &b-&s:16 &c-&s:24
```

结论：

* 此时子类对象的前8字节不再是虚表指针，而是指向虚基类子对象的指针。
* 基类子对象的前8字节是虚表指针，且其整体位于子类最后一个成员变量的后面。

### 无虚函数的菱形继承

```cpp
struct Base {
    int8_t ba;
};
struct C: public virtual Base {
    int32_t ca;
};
struct D: public virtual Base {
    int32_t da;
};
struct S: public C, public D {
    int64_t b;
    int32_t c;
};
```

输出：

```
Base:1 C:16 D:16 S:48 &Base-&S:44 &C-&S:0 &D-&S:16 &b-&s:32 &c-&s:40
```

结论：

* 此时`C`的子对象与`S`对象共享一个虚基类指针，而`D`则自己使用一个虚基类指针。
* `C`与`D`依次位于`S`的前端，而`Base`依然在最后端。
* 调用来自虚基类的虚函数时，相比非虚基类的虚函数，要多一次间接跳转：先通过虚基类指针找到虚表指针，再通过虚表指针找到对应函数地址。

### 有虚函数的菱形继承

```cpp
struct Base {
    virtual ~Base() {}
    int8_t ba;
};
struct C: public virtual Base {
    int32_t ca;
};
struct D: public virtual Base {
    int32_t da;
};
struct S: public C, public D {
    int64_t b;
    int32_t c;
};
```

输出：

```
Base:16 C:32 D:32 S:64 &Base-&S:48 &C-&S:0 &D-&S:16 &b-&s:32 &c-&s:40
```

结论：

* `C`和`D`依然位于`S`的前端，而`Base`位于后端。
* `C`与`S`共享一个虚基类指针，`D`自己使用一个虚基类指针。
* `C`与`D`与`S`与`Base`共享一个虚表指针。

# struct与class

第二个问题，c++中的struct和class有什么区别？

答案是，除了默认访问权限不同（struct默认为public，而class默认为private）外，其它完全相同。

上面的例子中，我们把每个struct都换成class，仍然能得到相同的结论。决定对象模型的不是用哪个关键字修饰它，而是它本身的性质，是否有基类，是否有虚函数，是否有虚基类。

## 然而struct不能用于修饰模板参数类型

但struct却不能用于下面这个场景：

```cpp
template <struct X>
void PrintSize() {
    printf("%d\n", sizeof(X));
}
```

当我们编译时，报错信息为：

```
struct.cpp:19:18: error: ‘struct X’ is not a valid type for a template non-type parameter
 template <struct X>
                  ^
```

而当我们把struct改成class或typename后，就可以编译成功了。

# struct的零额外开销

众所周知，c++的一个核心理念就是保证某个功能对不使用它的用户零额外开销。我们从几方面看一下struct是如何实现零额外开销的。

## 使用栈上的struct成员

下面两段代码：

```cpp
int64_t Func(int8_t x, int64_t y, int32_t z) {
    int8_t a;
    int64_t b;
    int32_t c;
    a = x;
    b = y;
    c = z;
    return a + b + c;
}
```

和

```cpp
struct S {
    int8_t a;
    int64_t b;
    int32_t c;
};
int64_t Func(int8_t x, int64_t y, int32_t z) {
    S s;
    s.a = x;
    s.b = y;
    s.c = z;
    return s.a + s.b + s.c;
}
```

它们对应的汇编指令为：

```
00000000004005b0 <_Z4Funcali>:
  4005b0:   48 0f be ff             movsbq %dil,%rdi
  4005b4:   48 63 d2                movslq %edx,%rdx
  4005b7:   48 01 fe                add    %rdi,%rsi
  4005ba:   48 8d 04 16             lea    (%rsi,%rdx,1),%rax
  4005be:   c3                      retq
  4005bf:   90                      nop
```

和

```
00000000004005b0 <_Z4Funcali>:
  4005b0:   48 0f be ff             movsbq %dil,%rdi
  4005b4:   48 63 d2                movslq %edx,%rdx
  4005b7:   48 01 fe                add    %rdi,%rsi
  4005ba:   48 8d 04 16             lea    (%rsi,%rdx,1),%rax
  4005be:   c3                      retq
  4005bf:   90                      nop
```

完全相同，说明使用栈上的struct成员，与使用栈上变量完全相同，零额外开销。

## 传递小struct

下面两段代码：

```cpp
void Func(int32_t x, int32_t y) {
    printf("x:%d y:%d\n", x, y);
}
int main() {
    int32_t x = 1;
    int32_t y = 2;
    Func(1, 2);
}
```

与

```cpp
struct S {
    int32_t x;
    int32_t y;
};
void Func(S s) {
    printf("x:%d y:%d\n", s.x, s.y);
}
int main() {
    S s;
    s.x = 1;
    s.y = 2;
    Func(s);
}
```

对应的汇编指令为：

```
0000000000400500 <main>:
  400500:   48 83 ec 08             sub    $0x8,%rsp
  400504:   ba 02 00 00 00          mov    $0x2,%edx
  400509:   be 01 00 00 00          mov    $0x1,%esi
  40050e:   bf b0 06 40 00          mov    $0x4006b0,%edi
  400513:   31 c0                   xor    %eax,%eax
  400515:   e8 b6 ff ff ff          callq  4004d0 <printf@plt>
  40051a:   31 c0                   xor    %eax,%eax
  40051c:   48 83 c4 08             add    $0x8,%rsp
  400520:   c3                      retq
  400521:   0f 1f 00                nopl   (%rax)
```

和

```
0000000000400500 <main>:
  400500:   48 83 ec 08             sub    $0x8,%rsp
  400504:   ba 02 00 00 00          mov    $0x2,%edx
  400509:   be 01 00 00 00          mov    $0x1,%esi
  40050e:   bf c0 06 40 00          mov    $0x4006c0,%edi
  400513:   31 c0                   xor    %eax,%eax
  400515:   e8 b6 ff ff ff          callq  4004d0 <printf@plt>
  40051a:   31 c0                   xor    %eax,%eax
  40051c:   48 83 c4 08             add    $0x8,%rsp
  400520:   c3                      retq
  400521:   0f 1f 00                nopl   (%rax)
```

注意此时两个`Func`函数都被inline掉了，因此我们可以直接对应`main`的汇编代码。可以看到它们完全相同，也符合上节的结论。

当我们把inline关掉后，先看一下`main`（看参数是如何传递的）：

```
0000000000400500 <main>:
  400500:   48 83 ec 08             sub    $0x8,%rsp
  400504:   be 02 00 00 00          mov    $0x2,%esi
  400509:   bf 01 00 00 00          mov    $0x1,%edi
  40050e:   e8 fd 00 00 00          callq  400610 <_Z4Funcii>
```

和

```
0000000000400500 <main>:
  400500:   48 83 ec 08             sub    $0x8,%rsp
  400504:   48 bf 01 00 00 00 02    movabs $0x200000001,%rdi
  40050b:   00 00 00
  40050e:   e8 fd 00 00 00          callq  400610 <_Z4Func1S>
```

可以看到直接传递一个struct反倒少了一条指令！原因是此时`S`为8个字节，刚好可以放入一个寄存器中，因此可以一条指令传递过去。而如果分成两个`int32_t`，则编译器必须用两个寄存器传递，多了一条指令。

再对比一下`Func`的汇编代码：

```
0000000000400610 <_Z4Funcii>:
  400610:   89 f2                   mov    %esi,%edx
  400612:   31 c0                   xor    %eax,%eax
  400614:   89 fe                   mov    %edi,%esi
  400616:   bf b0 06 40 00          mov    $0x4006b0,%edi
  40061b:   e9 b0 fe ff ff          jmpq   4004d0 <printf@plt>
```

和

```
0000000000400610 <_Z4FuncS>:
  400610:   48 89 fa                mov    %rdi,%rdx
  400613:   89 fe                   mov    %edi,%esi
  400615:   31 c0                   xor    %eax,%eax
  400617:   48 c1 fa 20             sar    $0x20,%rdx
  40061b:   bf c0 06 40 00          mov    $0x4006c0,%edi
  400620:   e9 ab fe ff ff          jmpq   4004d0 <printf@plt>
```

可以看到传递`S`的版本多了一条`sar $0x20,%rdx`，这是因为我们用一个寄存器传递了两个值，但在调用`printf`时还是要把它们分开，因此这里需要先把低4字节放到另一个寄存器里，再把%rdx的内容右移32位，从而得到高4字节的值。

把`main`和`Func`加起来，两个版本的汇编指令数量仍然完全相同，区别在于前者传递时多一次赋值，后者运算时多一次右移，可以认为开销相同。
