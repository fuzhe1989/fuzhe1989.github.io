---

title:      "C++：几种callable实现方式的性能对比"
date:       2017-07-21 16:23:06
tags:
    - C++
---

## 前言

C++中想实现一个callable的对象，通常有四种方式：

1. `std::function`：最common的方式，一般会配合`std::bind`使用。
2. function pointer：最C的方式，但没办法实现有状态的callable object。
3. function object：就是重载了`operator()`的类，C++98的STL中经常用。
4. lambda expression：不会污染namespace，一般来说编译器内部会实现为一个匿名的function object。

从原理上性能最好的应该是3和4，其次是2，最差的是`std::function`（[戳这里](/2017/05/22/effective-modern-cpp-chapter2-iterm5-6)）。下面我们用一小段代码来测试它们的性能。

<!--more-->

## 测试结果

* 测试机器：MBP。
* 编译器：Apple LLVM version 8.1.0 (clang-802.0.42)。
* 编译方式：g++ test.cpp -std=c++14 -O2。

```shell
./a.out "std::function"  0.15s user 0.20s system 98% cpu 0.358 total
./a.out "function_pointer"  0.10s user 0.11s system 98% cpu 0.209 total
./a.out "function_object"  0.03s user 0.01s system 92% cpu 0.042 total
./a.out "lambda"  0.03s user 0.01s system 93% cpu 0.042 total
```

可以看到3和4只要42ms，而相对应的2需要209ms，1需要358ms。这个顺序符合我们的预期，但相差这么多还是比较意外的。

## 测试程序

```cpp
#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include <utility>

using namespace std;

template <typename HandlerT = std::function<void (int)>>
class Worker{
public:
    explicit Worker(const HandlerT& handler): mHandler(handler) {}
    void Run(int x) {
        mHandler(x);
    }
private:
    HandlerT mHandler;
};

template <typename HandlerT>
void Test(HandlerT&& h) {
    using WorkerT = Worker<HandlerT>;
    vector<WorkerT> v;
    for (int i = 0; i < 10000000; ++i) {
        v.emplace_back(std::forward<HandlerT>(h));
    }
    int j = 0;
    for (auto& w: v) {
        w.Run(++j);
    }
}

void Func(int x) {
    int y = x + 5;
    y += 3;
}

struct Functor {
    void operator()(int x) const {
        int y = x + 5;
        y += 3;
    }
};

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "error input" << endl;
        exit(1);
    }

    string mode{argv[1]};
    if (mode == "std::function") {
        Test(bind(Func, placeholders::_1));
    } else if (mode == "function_pointer") {
        Test(Func);
    } else if (mode == "function_object") {
        Test(Functor{});
    } else if (mode == "lambda") {
        Test([](int x) -> void {int y = x + 5; y += 3;});
    } else {
        cerr << "error mode:" << mode << endl;
        exit(1);
    }
}
```