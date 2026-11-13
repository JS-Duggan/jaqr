#include "ast_dump.h"

#include "../src/ast/declaration.h"
#include "../src/ast/expression.h"
#include "../src/ast/expression_base.h"
#include "../src/ast/statement.h"
#include "../src/ast/statement_base.h"
#include "../src/ast/type.h"
#include "../src/visitor/visitor.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

std::string_view to_text(binary_op operation) {
    switch (operation) {
    case binary_op::LOGICAL_OR:
        return "||";
    case binary_op::LOGICAL_AND:
        return "&&";
    case binary_op::BITWISE_OR:
        return "|";
    case binary_op::BITWISE_AND:
        return "&";
    case binary_op::BITWISE_XOR:
        return "^";
    case binary_op::LEFT_SHIFT:
        return "<<";
    case binary_op::RIGHT_SHIFT:
        return ">>";
    case binary_op::ADD:
        return "+";
    case binary_op::SUBTRACT:
        return "-";
    case binary_op::MULTIPLY:
        return "*";
    case binary_op::DIVIDE:
        return "/";
    case binary_op::FLOOR_DIVIDE:
        return "\\";
    case binary_op::EXPONENT:
        return "~";
    case binary_op::EQUALS:
        return "==";
    case binary_op::NOT_EQUALS:
        return "!=";
    case binary_op::GT:
        return ">";
    case binary_op::GTE:
        return ">=";
    case binary_op::LT:
        return "<";
    case binary_op::LTE:
        return "<=";
    }
    return "?";
}

std::string_view to_text(unary_op operation) {
    switch (operation) {
    case unary_op::NEGATE:
        return "-";
    case unary_op::DEREFERENCE:
        return "<-";
    case unary_op::ADDRESS:
        return "@";
    case unary_op::LOGICAL_NOT:
        return "!";
    }
    return "?";
}

std::string_view to_text(primitive_type type) {
    switch (type) {
    case primitive_type::INT:
        return "int";
    case primitive_type::FLOAT:
        return "float";
    case primitive_type::CHAR:
        return "char";
    case primitive_type::BOOL:
        return "bool";
    case primitive_type::BYTE:
        return "byte";
    case primitive_type::VOID:
        return "void";
    }
    return "?";
}

// writes ch the way it would be escaped inside a literal that is closed by quote
std::string escaped(char ch, char quote) {
    switch (ch) {
    case '\n':
        return "\\n";
    case '\t':
        return "\\t";
    case '\r':
        return "\\r";
    case '\0':
        return "\\0";
    case '\\':
        return "\\\\";
    default:
        return ch == quote ? std::string{'\\', ch} : std::string{ch};
    }
}

struct literal_dumper {
    std::string operator()(std::int32_t value) const {
        return std::format("int:{}", value);
    }
    std::string operator()(std::byte value) const {
        return std::format("byte:{}", std::to_integer<int>(value));
    }
    std::string operator()(const std::string &value) const {
        std::string text = "str:\"";
        for (const char ch : value) {
            text += escaped(ch, '"');
        }
        return text + '"';
    }
    std::string operator()(double value) const {
        return std::format("float:{}", value);
    }
    std::string operator()(char value) const {
        return std::format("char:'{}'", escaped(value, '\''));
    }
    std::string operator()(bool value) const {
        return value ? "bool:true" : "bool:false";
    }
};

class dumper : public visitor {
  public:
    std::string text;

    void visit(const program &node) override {
        text += "(program";
        for (const decl_ptr &declaration : node.declarations) {
            child(*declaration);
        }
        text += ')';
    }

    // ---- expressions ----

    void visit(const binary_expr &node) override {
        open(to_text(node.operation));
        child(*node.left);
        child(*node.right);
        text += ')';
    }

    void visit(const unary_expr &node) override {
        open(to_text(node.operation));
        child(*node.right);
        text += ')';
    }

    void visit(const postfix_expr &node) override {
        switch (node.op) {
        case postfix_op::SUBSCRIPT:
            open("index");
            child(*node.obj);
            child(*std::get<expr_ptr>(node.arg));
            break;
        case postfix_op::MEMBER_ACCESS:
            open(".");
            child(*node.obj);
            word(std::get<std::string>(node.arg));
            break;
        case postfix_op::FUNCT_CALL:
            open("call");
            child(*node.obj);
            for (const expr_ptr &argument : std::get<arg_list>(node.arg)) {
                child(*argument);
            }
            break;
        case postfix_op::UPDATE_CALL: {
            const update_arguments &arguments = std::get<update_arguments>(node.arg);
            open("update-call");
            child(*node.obj);
            text += ' ';
            list(arguments.state);
            text += ' ';
            list(arguments.regular);
            break;
        }
        }
        text += ')';
    }

    void visit(const literal &node) override {
        text += std::visit(literal_dumper{}, node.value);
    }

    void visit(const identifier &node) override {
        text += node.lexeme;
    }

    void visit(const assignment_expr &node) override {
        open("=");
        child(*node.target);
        child(*node.value);
        text += ')';
    }

    void visit(const array_literal &node) override {
        open("array");
        for (const expr_ptr &element : node.elements) {
            child(*element);
        }
        text += ')';
    }

    void visit(const struct_literal &node) override {
        open("struct-init");
        word(node.struct_type);
        for (const field_init &field : node.init_list) {
            text += " (" + field.name;
            child(*field.val);
            text += ')';
        }
        text += ')';
    }

    void visit(const dynamic_alloc &node) override {
        open("new");
        child(*node.element_type);
        child(*node.count);
        text += ')';
    }

    void visit(const conversion_expr &node) override {
        open("convert");
        child(*node.target_type);
        child(*node.value);
        text += ')';
    }

    void visit(const this_expr & /*node*/) override {
        text += "this";
    }

    // ---- statements ----

    void visit(const block_stmt &node) override {
        text += "(block";
        for (const stmt_ptr &statement : node.statements) {
            child(*statement);
        }
        text += ')';
    }

    void visit(const expr_stmt &node) override {
        open("expr");
        child(*node.expression);
        text += ')';
    }

    void visit(const decl_stmt &node) override {
        node.declaration->accept(*this);
    }

    void visit(const return_stmt &node) override {
        open("return");
        optional_child(node.value);
        text += ')';
    }

    void visit(const if_stmt &node) override {
        open("if");
        for (const conditional_branch &branch : node.branches) {
            text += " (";
            branch.condition->accept(*this);
            child(*branch.body);
            text += ')';
        }
        if (node.else_body) {
            text += " (else";
            child(*node.else_body);
            text += ')';
        }
        text += ')';
    }

    void visit(const while_stmt &node) override {
        open("while");
        child(*node.condition);
        child(*node.body);
        text += ')';
    }

    void visit(const for_stmt &node) override {
        open("for");
        child(*node.initializer);
        child(*node.condition);
        child(*node.increment);
        child(*node.body);
        text += ')';
    }

    void visit(const step_stmt &node) override {
        open("step");
        child(*node.body);
        if (node.reject_body) {
            text += " (reject";
            child(*node.reject_body);
            text += ')';
        }
        text += ')';
    }

    // ---- declarations ----

    void visit(const variable_decl &node) override {
        const bool is_state = node.storage == variable_storage::STATE;
        if (node.immutable) {
            open(is_state ? "imut-state" : "imut");
        } else {
            open(is_state ? "state" : "let");
        }
        child(*node.name);
        child(*node.declared_type);
        optional_child(node.initializer);
        text += ')';
    }

    void visit(const function_decl &node) override {
        const bool is_update = node.kind == function_kind::UPDATE;
        open(is_update ? "update-fn" : "fn");
        child(*node.name);
        if (is_update) {
            text += ' ';
            parameters(node.state_parameters);
        }
        text += ' ';
        parameters(node.ordinary_parameters);
        if (!is_update) {
            child(*node.return_type);
        }
        child(*node.body);
        text += ')';
    }

    void visit(const struct_decl &node) override {
        open("struct");
        child(*node.name);
        for (const field_decl &field : node.fields) {
            text += " (" + field.name;
            child(*field.declared_type);
            text += ')';
        }
        text += ')';
    }

    void visit(const impl_decl &node) override {
        open("impl");
        child(*node.type_name);
        for (const function_decl_ptr &method : node.methods) {
            child(*method);
        }
        text += ')';
    }

    void visit(const invariant_decl &node) override {
        open("inv");
        child(*node.condition);
        text += ')';
    }

    // ---- types ----

    void visit(const primitive_type_syntax &node) override {
        text += to_text(node.type);
    }

    void visit(const named_type_syntax &node) override {
        text += node.name;
    }

    void visit(const pointer_type_syntax &node) override {
        open("->");
        child(*node.pointee);
        text += ')';
    }

    void visit(const array_type_syntax &node) override {
        open("[]");
        child(*node.size);
        child(*node.element);
        text += ')';
    }

  private:
    void open(std::string_view head) {
        text += '(';
        text += head;
    }

    void word(std::string_view value) {
        text += ' ';
        text += value;
    }

    template <typename node_type> void child(const node_type &node) {
        text += ' ';
        node.accept(*this);
    }

    template <typename pointer_type> void optional_child(const pointer_type &node) {
        if (node) {
            child(*node);
        }
    }

    // (EXPR...) with no head
    void list(const arg_list &expressions) {
        text += '(';
        bool first = true;
        for (const expr_ptr &expression : expressions) {
            if (!first) {
                text += ' ';
            }
            first = false;
            expression->accept(*this);
        }
        text += ')';
    }

    // ((NAME TYPE)...)
    void parameters(const std::vector<parameter> &params) {
        text += '(';
        bool first = true;
        for (const parameter &param : params) {
            if (!first) {
                text += ' ';
            }
            first = false;
            text += '(' + param.name;
            child(*param.declared_type);
            text += ')';
        }
        text += ')';
    }
};

template <typename node_type> std::string dump_node(const node_type &node) {
    dumper visitor;
    node.accept(visitor);
    return visitor.text;
}

} // namespace

std::string dump(const program &node) {
    return dump_node(node);
}

std::string dump(const decl &node) {
    return dump_node(node);
}

std::string dump(const stmt &node) {
    return dump_node(node);
}

std::string dump(const expr &node) {
    return dump_node(node);
}

std::string dump(const type_syntax &node) {
    return dump_node(node);
}
