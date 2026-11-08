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
    FLOOR_DIVIDE,
    EXPONENT,
    EQUALS,
    NOT_EQUALS,
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

struct update_arguments {
    arg_list regular;
    arg_list state;
};

using postfix_arg = std::variant<expr_ptr,        // Index expression
                                 std::string,     // Member name
                                 arg_list,        // Ordinary call arguments
                                 update_arguments // Update call arguments
                                 >;

enum class postfix_op : std::uint8_t { SUBSCRIPT, MEMBER_ACCESS, FUNCT_CALL, UPDATE_CALL };

struct postfix_expr : expr {
    postfix_op operation;
    expr_ptr obj;
    postfix_arg arg;

    // object[index]
    postfix_expr(expr_ptr object, expr_ptr index)
        : operation(postfix_op::SUBSCRIPT), obj(std::move(object)), arg(std::move(index)) {
    }

    // object.member
    postfix_expr(expr_ptr object, std::string member)
        : operation(postfix_op::MEMBER_ACCESS), obj(std::move(object)), arg(std::move(member)) {
    }

    // callee(arguments)
    postfix_expr(expr_ptr callee, arg_list arguments)
        : operation(postfix_op::FUNCT_CALL), obj(std::move(callee)), arg(std::move(arguments)) {
    }

    // callee(ordinary arguments)(state arguments)
    postfix_expr(expr_ptr callee, update_arguments arguments)
        : operation(postfix_op::UPDATE_CALL), obj(std::move(callee)), arg(std::move(arguments)) {
    }
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

struct assignment_expr : expr {
    expr_ptr target, value;

    assignment_expr(expr_ptr lhs, expr_ptr rhs)
        : target(std::move(target)), value(std::move(value)) {};
};
