#include <bits/stdc++.h>

using literal_val = std::variant<std::int32_t, std::byte, std::string, double, char, bool>;
struct literal {
    literal_val value;

    literal(literal_val v) : value(v) {
    }
};

int main() {
    char a = 'a';
    literal x{1.5f};

    std::cout << x.value.index() << '\n';
}
