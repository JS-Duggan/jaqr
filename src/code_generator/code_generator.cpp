#include "code_generator.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace {

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

std::string quoted_literal(std::string_view value, char quote) {
    std::string result(1, quote);
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch == static_cast<unsigned char>(quote)) {
                result += '\\';
                result += static_cast<char>(ch);
            } else if (ch >= 0x20 && ch <= 0x7e) {
                result += static_cast<char>(ch);
            } else {
                // Three octal digits prevent following characters extending the escape.
                result += '\\';
                result += static_cast<char>('0' + ((unsigned int)(ch >> 6U) & 7U));
                result += static_cast<char>('0' + ((unsigned int)(ch >> 3U) & 7U));
                result += static_cast<char>('0' + (ch & 7U));
            }
        }
    }
    result += quote;
    return result;
}

} // namespace

void code_generator::visit(const program &node) {
    loc.clear();
    loc.emplace_back();

    for (const auto &declr : node.declarations) {
        declr->accept(*this);
    }
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
        loc.back() += ',';
        node.right->accept(*this);
        loc.back() += ')';
        break;
    default:
        node.left->accept(*this);
        loc.back() += operator_text(node.operation);
        node.right->accept(*this);
    }
    loc.back() += " )";
}

void code_generator::visit(const unary_expr &node) {
    loc.back() += " (";
    switch (node.operation) {
    case unary_op::NEGATE:
        loc.back() += '-';
        break;
    case unary_op::DEREFERENCE:
        loc.back() += '*';
        break;
    case unary_op::ADDRESS:
        loc.back() += '&';
        break;
    case unary_op::LOGICAL_NOT:
        loc.back() += '!';
        break;
    }
    loc.back() += '(';
    node.right->accept(*this);
    loc.back() += "))";
}

void code_generator::visit(const postfix_expr &node) {
    loc.back() += "((";
    node.obj->accept(*this);
    loc.back() += ')';

    switch (node.op) {
    case postfix_op::FUNCT_CALL: {
        loc.back() += '(';
        bool first = true;
        for (const auto &a : std::get<arg_list>(node.arg)) {
            if (!first)
                loc.back() += ", ";
            first = false;
            a->accept(*this);
        }
        loc.back() += ')';
        break;
    }
    case postfix_op::SUBSCRIPT:
        loc.back() += '[';
        std::get<expr_ptr>(node.arg)->accept(*this);
        loc.back() += ']';
        break;
    case postfix_op::MEMBER_ACCESS:
        loc.back() += '.';
        loc.back() += std::get<std::string>(node.arg);
        break;
    case postfix_op::UPDATE_CALL: {
        loc.back() += '(';
        bool first = true;
        auto &args = std::get<update_arguments>(node.arg);
        for (const auto &a : args.regular) {
            if (!first)
                loc.back() += ',';
            first = false;
            a->accept(*this);
        }
        for (const auto &a : args.state) {
            if (!first)
                loc.back() += ',';
            first = false;
            loc.back() += "&(";
            a->accept(*this);
            loc.back() += ')';
        }
        loc.back() += ')';
        break;
    }
    }
    loc.back() += ')';
}

void code_generator::visit(const literal &node) {
    loc.back() += '(';
    std::visit(
        overloaded{
            [this](std::int32_t value) { loc.back() += std::to_string(value); },
            [this](std::byte value) {
                loc.back() += "std::byte{";
                loc.back() += std::to_string(std::to_integer<unsigned int>(value));
                loc.back() += '}';
            },
            [this](const std::string &value) { loc.back() += quoted_literal(value, '"'); },
            [this](double value) { loc.back() += std::to_string(value); },
            [this](char value) { loc.back() += quoted_literal(std::string_view(&value, 1), '\''); },
            [this](bool value) { loc.back() += value ? "true" : "false"; },
        },
        node.value);
    loc.back() += ')';
}

void code_generator::visit(const identifier &node) {
    loc.back() += "(" + node.lexeme + ")";
}

void code_generator::visit(const assignment_expr &node) {
    loc.back() += '(';
    node.target->accept(*this);
    loc.back() += " = ";
    node.value->accept(*this);
    loc.back() += ')';
}

void code_generator::visit(const array_literal &node) {
    loc.back() += '{';
    bool first = true;
    for (const auto &arg : node.elements) {
        if (!first)
            loc.back() += ',';
        first = false;
        arg->accept(*this);
    }
    loc.back() += '}';
}

void code_generator::visit(const struct_literal &node) {
    loc.back() += node.struct_type + "{";
    bool first = true;
    for (const auto &arg : node.init_list) {
        if (!first)
            loc.back() += ',';
        loc.back() += "." + arg.name + " = ";
        first = false;
        arg.val->accept(*this);
    }
    loc.back() += '}';
}

void code_generator::visit(const dynamic_alloc &node) {
    loc.back() += "(new ";
    node.element_type->accept(*this);
    loc.back() += '[';
    node.count->accept(*this);
    loc.back() += "]())";
}

void code_generator::visit(const conversion_expr &node) {
    loc.back() += "(static_cast<";
    node.target_type->accept(*this);
    loc.back() += ">(";
    node.value->accept(*this);
    loc.back() += "))";
}

void code_generator::visit(const this_expr &node) {
    loc.back() += "(*this)";
}

// Statements
void code_generator::visit(const block_stmt &node) {
    loc.emplace_back("{");
    loc.emplace_back();
    for (const auto &stmt : node.statements) {
        stmt->accept(*this);
    }
    loc.push_back("}");
    loc.emplace_back();
}

void code_generator::visit(const expr_stmt &node) {
    node.expression->accept(*this);
    loc.back() += ';';
    loc.emplace_back();
}

void code_generator::visit(const decl_stmt &node) {
    node.declaration->accept(*this);
    loc.back() += ';';
    loc.emplace_back();
}

void code_generator::visit(const return_stmt &node) {
    loc.back() += "return";
    if (node.value) {
        loc.back() += ' ';
        node.value->accept(*this);
    }
    loc.back() += ';';
    loc.emplace_back();
}

void code_generator::visit(const if_stmt &node) {
    bool first = true;
    for (const auto &branch : node.branches) {
        loc.back() += first ? "if (" : "else if (";
        branch.condition->accept(*this);
        loc.back() += ") ";
        branch.body->accept(*this);
        first = false;
    }
    if (node.else_body) {
        loc.back() += "else ";
        node.else_body->accept(*this);
    }
}

void code_generator::visit(const while_stmt &node) {
    loc.back() += "while (";
    node.condition->accept(*this);
    loc.back() += ") ";
    node.body->accept(*this);
}

void code_generator::visit(const for_stmt &node) {
    loc.back() += "for (";
    if (node.initializer)
        node.initializer->accept(*this);
    loc.back() += "; ";
    if (node.condition)
        node.condition->accept(*this);
    loc.back() += "; ";
    if (node.increment)
        node.increment->accept(*this);
    loc.back() += ") ";
    node.body->accept(*this);
}

void code_generator::visit(const step_stmt &node) {
    // leaving for now, will come back later
}

// Declarations
void code_generator::visit(const variable_decl &node) {
    if (node.immutable)
        loc.back() += "const ";
    node.declared_type->accept(*this);
    loc.back() += " " + node.name->lexeme;

    if (node.initializer) {
        loc.back() += " = ";
        node.initializer->accept(*this);
    }
}

void code_generator::visit(const function_decl &node) {
    if (node.name->lexeme == "main") {
        loc.back() += "int";
    } else if (node.kind == function_kind::UPDATE) {
        loc.back() += "void";
    } else if (node.return_type) {
        node.return_type->accept(*this);
    } else {
        throw std::invalid_argument("Ordinary function requires a return type");
    }
    loc.back() += " " + node.name->lexeme;
    loc.back() += '(';

    bool first = true;
    for (const auto &param : node.ordinary_parameters) {
        if (!first) {
            loc.back() += ", ";
        }
        first = false;

        param.declared_type->accept(*this);
        loc.back() += " " + param.name;
    }

    for (const auto &param : node.state_parameters) {
        if (!first) {
            loc.back() += ", ";
        }
        first = false;

        param.declared_type->accept(*this);
        loc.back() += " &" + param.name;
    }

    loc.back() += ") ";
    node.body->accept(*this);
}

void code_generator::visit(const struct_decl &node) {
    loc.back() += "struct " + node.name->lexeme + " {";
    for (const auto &field : node.fields) {
        loc.emplace_back();
        field.declared_type->accept(*this);
        loc.back() += " " + field.name + ";";
    }
    loc.emplace_back("};");
    loc.emplace_back();

    // function declarations (no body) to go here:
}

void code_generator::visit(const impl_decl &node) {
    for (const auto &fn : node.methods) {
        fn->accept(*this);
    }
}

void code_generator::visit(const invariant_decl &node) {
    // to do
}

// Type syntax
void code_generator::visit(const primitive_type_syntax &node) {
    switch (node.type) {
    case primitive_type::VOID:
        loc.back() += "void";
        break;
    case primitive_type::INT:
        loc.back() += "std::int32_t";
        break;
    case primitive_type::FLOAT:
        loc.back() += "double";
        break;
    case primitive_type::CHAR:
        loc.back() += "char";
        break;
    case primitive_type::BOOL:
        loc.back() += "bool";
        break;
    case primitive_type::BYTE:
        loc.back() += "std::byte";
        break;
    }
}

void code_generator::visit(const named_type_syntax &node) {
    loc.back() += node.name;
}

void code_generator::visit(const pointer_type_syntax &node) {
    node.pointee->accept(*this);
    loc.back() += '*';
}

void code_generator::visit(const array_type_syntax &node) {
    loc.back() += "std::array<";
    node.element->accept(*this);
    loc.back() += ", ";
    node.size->accept(*this);
    loc.back() += '>';
}
