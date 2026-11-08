#pragma once

#include "expression_base.h"
#include "type.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
        : operation(o), left(std::move(lhs)), right(std::move(rhs)) {
    }
};

enum class unary_op : std::uint8_t { NEGATE, DEREFERENCE, ADDRESS, LOGICAL_NOT };

struct unary_expr : expr {
    unary_op operation;
    expr_ptr right;

    unary_expr(unary_op o, expr_ptr rhs) : operation(o), right(std::move(rhs)) {
    }
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
    postfix_op op;
    expr_ptr obj;
    postfix_arg arg;

    // object[index]
    postfix_expr(expr_ptr object, expr_ptr index)
        : op(postfix_op::SUBSCRIPT), obj(std::move(object)), arg(std::move(index)) {
    }

    // object.member
    postfix_expr(expr_ptr object, std::string member)
        : op(postfix_op::MEMBER_ACCESS), obj(std::move(object)), arg(std::move(member)) {
    }

    // callee(arguments)
    postfix_expr(expr_ptr callee, arg_list arguments)
        : op(postfix_op::FUNCT_CALL), obj(std::move(callee)), arg(std::move(arguments)) {
    }

    // callee(ordinary arguments)(state arguments)
    postfix_expr(expr_ptr callee, update_arguments arguments)
        : op(postfix_op::UPDATE_CALL), obj(std::move(callee)), arg(std::move(arguments)) {
    }
};
using literal_val = std::variant<std::int32_t, std::byte, std::string, float, unsigned char, bool>;
struct literal : expr {
    literal_val value;

    literal(literal_val v) : value(v) {
    }
};

struct variable : expr {
    std::string lexeme;
    variable(std::string &l) : lexeme(l) {
    }
};

struct assignment_expr : expr {
    expr_ptr target, value;

    assignment_expr(expr_ptr lhs, expr_ptr rhs) : target(std::move(lhs)), value(std::move(rhs)) {
    }
};

struct array_literal : expr {
    arg_list elements;

    array_literal(arg_list e) : elements(std::move(e)) {
    }
};

struct field_init {
    std::string name;
    expr_ptr val;
};

struct struct_literal : expr {
    std::string struct_type;
    std::vector<field_init> init_list;

    struct_literal(std::string st, std::vector<field_init> il)
        : struct_type(std::move(st)), init_list(std::move(il)) {
    }
};

struct dynamic_alloc : expr {
    type_ptr element_type;
    expr_ptr count;

    dynamic_alloc(type_ptr type, expr_ptr size)
        : element_type(std::move(type)), count(std::move(size)) {
    }
};

struct conversion_expr : expr {
    type_ptr target_type;
    expr_ptr value;

    conversion_expr(type_ptr type, expr_ptr expression)
        : target_type(std::move(type)), value(std::move(expression)) {
    }
};
