---
title:      "Magic Enum C++ 是如何工作的"
date:       2022-06-28 19:18:58
tags:
---

**TL;DR**

<!--more-->

定制点：

```cpp
namespace customize {

// If need another range for all enum types by default, redefine the macro MAGIC_ENUM_RANGE_MIN and MAGIC_ENUM_RANGE_MAX.
// If need another range for specific enum type, add specialization enum_range for necessary enum type.
template <typename E>
struct enum_range {
  static constexpr int min = MAGIC_ENUM_RANGE_MIN;
  static constexpr int max = MAGIC_ENUM_RANGE_MAX;
};

namespace detail {
enum class default_customize_tag {};
enum class invalid_customize_tag {};
} // namespace magic_enum::customize::detail

using customize_t = std::variant<string_view, detail::default_customize_tag, detail::invalid_customize_tag>;

// Default customize.
inline constexpr auto default_tag = detail::default_customize_tag{};
// Invalid customize.
inline constexpr auto invalid_tag = detail::invalid_customize_tag{};

// If need custom names for enum, add specialization enum_name for necessary enum type.
template <typename E>
constexpr customize_t enum_name(E) noexcept {
  return default_tag;
}

// If need custom type name for enum, add specialization enum_type_name for necessary enum type.
template <typename E>
constexpr customize_t enum_type_name() noexcept {
  return default_tag;
}

} // namespace magic_enum::customize
```

获得 enum class name：

```cpp
template <typename E>
constexpr auto n() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::n requires enum type.");

  [[maybe_unused]] constexpr auto custom = customize::enum_type_name<E>();
  static_assert(std::is_same_v<std::decay_t<decltype(custom)>, customize::customize_t>, "magic_enum::customize requires customize_t type.");
  if constexpr (custom.index() == 0) {
    constexpr auto name = std::get<string_view>(custom);
    static_assert(!name.empty(), "magic_enum::customize requires not empty string.");
    return static_string<name.size()>{name};
  } else if constexpr (custom.index() == 1 && supported<E>::value) {
#if defined(__clang__) || defined(__GNUC__)
    constexpr auto name = pretty_name({__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 2});
#elif defined(_MSC_VER)
    constexpr auto name = pretty_name({__FUNCSIG__, sizeof(__FUNCSIG__) - 17});
#else
    constexpr auto name = string_view{};
#endif
    return static_string<name.size()>{name};
  } else {
    return static_string<0>{}; // Unsupported compiler or Invalid customize.
  }
}
```

获得 enum value name:

```cpp
template <typename E, E V>
constexpr auto n() noexcept {
  static_assert(is_enum_v<E>, "magic_enum::detail::n requires enum type.");

  [[maybe_unused]] constexpr auto custom = customize::enum_name<E>(V);
  static_assert(std::is_same_v<std::decay_t<decltype(custom)>, customize::customize_t>, "magic_enum::customize requires customize_t type.");
  if constexpr (custom.index() == 0) {
    constexpr auto name = std::get<string_view>(custom);
    static_assert(!name.empty(), "magic_enum::customize requires not empty string.");
    return static_string<name.size()>{name};
  } else if constexpr (custom.index() == 1 && supported<E>::value) {
#if defined(__clang__) || defined(__GNUC__)
    constexpr auto name = pretty_name({__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 2});
#elif defined(_MSC_VER)
    constexpr auto name = pretty_name({__FUNCSIG__, sizeof(__FUNCSIG__) - 17});
#else
    constexpr auto name = string_view{};
#endif
    return static_string<name.size()>{name};
  } else {
    return static_string<0>{}; // Unsupported compiler or Invalid customize.
  }
}
```