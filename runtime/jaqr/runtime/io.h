#pragma once

#include <iostream>

namespace jaqr_runtime {

template <typename T> void print(const T &value) {
    std::cout << value;
}

template <typename T> bool scan(T &value) {
    return static_cast<bool>(std::cin >> value);
}

} // namespace jaqr_runtime
