#include <iostream>
#include <type_traits>
#include <string_view>

enum class Color { RED, GREEN, YELLOW };

template <typename E, E V>
constexpr auto n() {
    static_assert(std::is_enum_v<E>);
    return std::string_view(__PRETTY_FUNCTION__);
}

int main() {
    std::cout << n<Color, Color::RED>() << std::endl;
}
