---
title:      "C++：在实现文件中内联小的成员函数"
date:       2018-07-22 16:53:22
tags:
    - C++
---

注：本文所使用编译器均为gcc4.1.2。

例子：

```cpp
// file.h

#ifndef XXX_FILE_H
#define XXX_FILE_H

class File {
public:
    explicit File(int content);

    int GetContent();
private:
    bool IsLoaded();
    void Load();
private:
    bool mIsLoaded;
    int mContent;
};

#endif
```

首先我们在file.h中定义了一个类型`File`。为了保持头文件简洁，我们这里只声明了各个成员函数，而将定义都放到实现文件中：

```cpp
// file.cpp

#include "file.h"

File::File(int content)
  : mIsLoaded(false),
    mContent(content) {}

int File::GetContent() {
    if (!IsLoaded()) {
        Load();
    }
    return mContent;
}

bool File::IsLoaded() {
    return mIsLoaded;
}

void File::Load() {
    mIsLoaded = true;
}
```

接下来，我们在main.cpp中使用这个类：

```cpp
// main.cpp

#include "file.h"
#include <iostream>

using namespace std;

int main() {
    File f(5);
    cout << f.GetContent() << endl;
}
```

在这个很简单的例子中，我们要关注的是：对于简短的成员函数，将其实现放到.cpp中，而不是直接定义在类定义体中（也就是放到.h中`class File`内部），会不会损害性能？

我们用`-O2`编译，再看一下其汇编指令：

```
0000000000400840 <_ZN4File10GetContentEv>:
  400840:   53                      push   %rbx
  400841:   48 89 fb                mov    %rdi,%rbx
  400844:   e8 d7 ff ff ff          callq  400820 <_ZN4File8IsLoadedEv>
  400849:   84 c0                   test   %al,%al
  40084b:   75 08                   jne    400855 <_ZN4File10GetContentEv+0x15>
  40084d:   48 89 df                mov    %rbx,%rdi
  400850:   e8 db ff ff ff          callq  400830 <_ZN4File4LoadEv>
  400855:   8b 43 04                mov    0x4(%rbx),%eax
  400858:   5b                      pop    %rbx
  400859:   c3                      retq
  40085a:   90                      nop
  40085b:   90                      nop
  40085c:   90                      nop
  40085d:   90                      nop
  40085e:   90                      nop
  40085f:   90                      nop
```

可以看到，`GetContent`里确实调用了`IsLoaded`和`Load`。

我们知道，如果把`IsLoaded`和`Load`的定义放到类定义体内，这样的函数是默认内联的，`GetContent`中就不会真的去调用这两个函数。但现在，我们为了简洁，把它们的实现移到了.cpp中，导致了它们没有被内联，确实损害了性能。

怎么既把小函数定义放到.cpp中，又能让它们在`GetContent`中内联呢？

我们先把`IsLoaded`和`Load`的实现放到`GetContent`前面：

```cpp
bool File::IsLoaded() {
    return mIsLoaded;
}

void File::Load() {
    mIsLoaded = true;
}

int File::GetContent() {
    if (!IsLoaded()) {
        Load();
    }
    return mContent;
}
```

这次编译的结果没有变，`IsLoaded`和`Load`没有内联。

我们再在两个函数的定义前面加上`inline`：

```cpp
inline bool File::IsLoaded() {
    return mIsLoaded;
}

inline void File::Load() {
    mIsLoaded = true;
}

int File::GetContent() {
    if (!IsLoaded()) {
        Load();
    }
    return mContent;
}
```

这回编译结果变成了：

```
0000000000400820 <_ZN4File10GetContentEv>:
  400820:   80 3f 00                cmpb   $0x0,(%rdi)
  400823:   75 03                   jne    400828 <_ZN4File10GetContentEv+0x8>
  400825:   c6 07 01                movb   $0x1,(%rdi)
  400828:   8b 47 04                mov    0x4(%rdi),%eax
  40082b:   c3                      retq
  40082c:   90                      nop
  40082d:   90                      nop
  40082e:   90                      nop
  40082f:   90                      nop
```

可以看到，`inline`起作用了，`GetContent`里面的函数调用都被抹平了。

假如把`IsLoaded`和`Load`的实现加上`inline`，但放到`GetContet`后面，`inline`还会生效吗：

```cpp
int File::GetContent() {
    if (!IsLoaded()) {
        Load();
    }
    return mContent;
}

inline bool File::IsLoaded() {
    return mIsLoaded;
}

inline void File::Load() {
    mIsLoaded = true;
}
```

编译结果为：

```
0000000000400820 <_ZN4File10GetContentEv>:
  400820:   80 3f 00                cmpb   $0x0,(%rdi)
  400823:   75 03                   jne    400828 <_ZN4File10GetContentEv+0x8>
  400825:   c6 07 01                movb   $0x1,(%rdi)
  400828:   8b 47 04                mov    0x4(%rdi),%eax
  40082b:   c3                      retq
  40082c:   90                      nop
  40082d:   90                      nop
  40082e:   90                      nop
  40082f:   90                      nop
```

可以看到，即使我们把待`inline`的成员函数的实现放到调用处后面，这种`inline`仍然是生效的。

结论：我们可以在成员函数的定义前显式加上`inline`，这样同文件的调用处就可以内联这个成员函数，去除多余的跳转，提升性能，且在同一个.cpp文件中待内联的函数与调用处的顺序不会影响内联的效果。
