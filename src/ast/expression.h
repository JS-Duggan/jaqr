#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct expr {
    virtual ~expr() = default;
};
using expr_ptr = std::unique_ptr<expr>;

enum class binary_op : std::uint8_t {
    LOGICAL_OR,
    LOGICAL_AND,
    BITWISE_OR,
    BITWISE_AND,
    BITWISE_XOR,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE,
    EXPONENT,
    EQUALS,
    GT,
    GTE,
    LT,
    LTE,
};

struct binary_expr : expr {
    binary_op operation;
    expr_ptr left, right;

    binary_expr(binary_op o, expr_ptr lhs, expr_ptr rhs)
        : operation(o), left(std::move(lhs)), right(std::move(rhs)) {};
};

enum class unary_op : std::uint8_t { NEGATE, DEREFERENCE, ADDRESS, LOGICAL_NOT };

struct unary_expr : expr {
    unary_op operation;
    expr_ptr right;

    unary_expr(unary_op o, expr_ptr rhs) : operation(o), right(std::move(rhs)) {};
};

using arg_list = std::vector<expr_ptr>;
using postfix_arg = std::variant<expr_ptr, std::string, arg_list>;
enum class postfix_op : std::uint8_t { SUBSCRIPT, MEMBER_ACCESS, PTR_MEMBER_ACCESS, FUNCT_CALL };

struct postfix_expr : expr {
    postfix_op operation;
    expr_ptr obj;
    postfix_arg arg;
    postfix_expr(postfix_op o, expr_ptr lhs, postfix_arg rhs)
        : operation(o), obj(std::move(lhs)), arg(std::move(rhs)) {};
};

using literal_val = std::variant<std::int32_t, std::byte, float, char, bool>;
struct literal : expr {
    literal_val value;

    literal(literal_val v) : value(v) {};
};

struct variable : expr {
    std::string lexeme;
    variable(std::string &l) : lexeme(l) {};
};
