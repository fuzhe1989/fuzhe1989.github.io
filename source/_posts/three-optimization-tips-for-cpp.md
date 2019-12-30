---
title: "[笔记] Three Optimization Tips for C++"
date: 2019-04-07 19:29:29
tags:
    - 笔记
    - C++
    - 性能优化
---

> 原文：[Three Optimization Tips for C++](https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920/)

建议：
* Prefer static linking and position-dependent code (as opposed to PIC, position-independent code).
* Prefer 64-bit code and 32-bit data.
* Prefer array indexing to pointers (this one seems to reverse every ten years).
* Prefer regular memory access patterns.
* Minimize control flow.
* Avoid data dependencies.

<!--more-->

算术操作的开销顺序：
* comparisons
* (u)int add, subtract, bitops, shift
* floating point add, sub (separate unit!)
* indexed array access (caveat: cache effects)
* (u)int32 mul
* FP mul
* FP division, remainder
* (u)int division, remainder

后面在讨论怎么加速`digits10`和`u64ToAsciiClassic`：
1. `digits10`：输入一个int，返回它在10进制下的位数。
    1. 优化1：循环展开，减少使用除法的次数。
    1. 优化2：手动二分。
1. `u64ToAsciiClassic`：一个`itoa`实现。这里的优化点是尽量减少**写**内存的次数，宁肯先读一遍，此处用到了`digits10`。

```cpp
uint32_t digits10(uint64_t v) {
  if (v < P01) return 1;
  if (v < P02) return 2;
  if (v < P03) return 3;
  if (v < P12) {
    if (v < P08) {
      if (v < P06) {
        if (v < P04) return 4;
        return 5 + (v >= P05);
      }
      return 7 + (v >= P07);
    }
    if (v < P10) {
      return 9 + (v >= P09);
    }
    return 11 + (v >= P11);
  }
  return 12 + digits10(v / P12);
}

unsigned u64ToAsciiTable(uint64_t value, char* dst) {
  static const char digits[201] =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";
  uint32_t const length = digits10(value);
  uint32_t next = length - 1;
  while (value >= 100) {
    auto const i = (value % 100) * 2;
    value /= 100;
    dst[next] = digits[i + 1];
    dst[next - 1] = digits[i];
    next -= 2;
  }
  // Handle last 1-2 digits
  if (value < 10) {
    dst[next] = '0' + uint32_t(value);
  } else {
    auto i = uint32_t(value) * 2;
    dst[next] = digits[i + 1];
    dst[next - 1] = digits[i];
  }
  return length;
}
```

评论区有人贴了两个github项目，里面是很多种`digits10`和`itoa`的实现，和benchmark：
1. https://github.com/localvoid/cxx-benchmark-count-digits
1. https://github.com/localvoid/cxx-benchmark-itoa