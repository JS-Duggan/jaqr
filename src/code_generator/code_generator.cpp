#include "code_generator.h"

void code_generator::visit(const program &node) {
}

// Expressions:

const char *operator_text(binary_op op) {
    switch (op) {
    case binary_op::LOGICAL_OR:
        return " || ";
    case binary_op::LOGICAL_AND:
        return " && ";
    case binary_op::BITWISE_OR:
        return " | ";
    case binary_op::BITWISE_AND:
        return " & ";
    case binary_op::BITWISE_XOR:
        return " ^ ";
    case binary_op::ADD:
        return " + ";
    case binary_op::SUBTRACT:
        return " - ";
    case binary_op::MULTIPLY:
        return " * ";
    case binary_op::DIVIDE:
        return " / ";
    case binary_op::EQUALS:
        return " == ";
    case binary_op::NOT_EQUALS:
        return " != ";
    case binary_op::GT:
        return " > ";
    case binary_op::GTE:
        return " >= ";
    case binary_op::LT:
        return " < ";
    case binary_op::LTE:
        return " <= ";
    default:
        throw std::invalid_argument("Operator requires special handling");
    }
}

void code_generator::visit(const binary_expr &node) {
    loc.back() += " (";
    switch (node.operation) {
    case binary_op::FLOOR_DIVIDE:
        loc.back() += " std::floor((double)(";
        node.left->accept(*this);
        loc.back() += ") / (double)(";
        node.right->accept(*this);
        loc.back() += "))";
        break;
    case binary_op::EXPONENT:
        loc.back() += " std::pow(";
        node.left->accept(*this);
        loc.back() += ",";
        node.right->accept(*this);
        loc.back() += ")";
        break;
    default:
        node.left->accept(*this);
        loc.back() += operator_text(node.operation);
        node.right->accept(*this);
    }
    loc.back() += " )";
}

void code_generator::visit(const unary_expr &node) {
}
void code_generator::visit(const postfix_expr &node) {
}
void code_generator::visit(const literal &node) {
}
void code_generator::visit(const variable &node) {
}
void code_generator::visit(const assignment_expr &node) {
}
void code_generator::visit(const array_literal &node) {
}
void code_generator::visit(const struct_literal &node) {
}
void code_generator::visit(const dynamic_alloc &node) {
}
void code_generator::visit(const conversion_expr &node) {
}
void code_generator::visit(const this_expr &node) {
}

// Statements
void code_generator::visit(const block_stmt &node) {
}
void code_generator::visit(const expr_stmt &node) {
}
void code_generator::visit(const decl_stmt &node) {
}
void code_generator::visit(const return_stmt &node) {
}
void code_generator::visit(const if_stmt &node) {
}
void code_generator::visit(const while_stmt &node) {
}
void code_generator::visit(const for_stmt &node) {
}
void code_generator::visit(const step_stmt &node) {
}

// Declarations
void code_generator::visit(const variable_decl &node) {
}
void code_generator::visit(const function_decl &node) {
}
void code_generator::visit(const struct_decl &node) {
}
void code_generator::visit(const impl_decl &node) {
}
void code_generator::visit(const invariant_decl &node) {
}

// Type syntax
void code_generator::visit(const primitive_type_syntax &node) {
}
void code_generator::visit(const named_type_syntax &node) {
}
void code_generator::visit(const pointer_type_syntax &node) {
}
void code_generator::visit(const array_type_syntax &node) {
}