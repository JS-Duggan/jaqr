#include "semantic.h"

#include "../visitor/visitor.h"
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace {
void fail(const char *code, const source_span &span, const std::string &message) {
    throw std::runtime_error(std::format("{} at line {}, column {}: {}", code, span.start_line,
                                         span.start_col, message));
}

bool numeric(primitive_type type) {
    return type == primitive_type::INT || type == primitive_type::FLOAT;
}

bool truthy(primitive_type type) {
    return numeric(type) || type == primitive_type::BOOL;
}

bool compatible(primitive_type expected, primitive_type actual) {
    return expected == actual || (numeric(expected) && numeric(actual));
}

class checker final : public visitor {
    struct binding {
        primitive_type type;
        bool state;
        bool immutable;
    };
    struct expr_result {
        primitive_type type;
        const binding *location = nullptr;
        bool assignment = false;
    };

    std::unordered_map<std::string, binding> globals;
    std::unordered_map<std::string, binding> locals;
    std::optional<expr_result> result;
    std::optional<primitive_type> resolved_type;
    const function_decl *main_function = nullptr;
    bool global_mode = false;
    bool in_step = false;
    bool in_reject = false;

    const binding &lookup(const identifier &name) const {
        if (const auto local = locals.find(name.lexeme); local != locals.end())
            return local->second;
        if (const auto global = globals.find(name.lexeme); global != globals.end())
            return global->second;
        fail("SEM001", name.source, "unknown name '" + name.lexeme + "'");
    }

    expr_result evaluate(const expr &node) {
        result.reset();
        node.accept(*this);
        if (!result)
            fail("SEM000", node.source, "expression is not supported yet");
        return *result;
    }

    primitive_type resolve(const type_syntax &node) {
        resolved_type.reset();
        node.accept(*this);
        if (!resolved_type)
            fail("SEM000", node.source, "type is not supported yet");
        return *resolved_type;
    }

    static void require_compatible(primitive_type expected, primitive_type actual,
                                   const source_span &span) {
        if (!compatible(expected, actual))
            fail("SEM003", span,
                 std::format("expected {}, got {}", spelling(expected), spelling(actual)));
    }

  public:
    void run(const ast_root &root) {
        if (root.declarations.empty())
            fail("SEM000", source_span{0, 0, 0, 0}, "fn main() is required");
        global_mode = true;
        for (const decl_ptr &entry : root.declarations)
            entry->accept(*this);
        global_mode = false;
        if (!main_function)
            fail("SEM000", root.declarations.back()->source, "fn main() is required");
        main_function->body->accept(*this);
    }

    void visit(const program &node) override {
        fail("SEM000", source_span{0, 0, 0, 0}, "use the AST root for analysis");
    }

    void visit(const literal &node) override {
        if (std::holds_alternative<std::int32_t>(node.value))
            result = expr_result{primitive_type::INT};
        else if (std::holds_alternative<double>(node.value))
            result = expr_result{primitive_type::FLOAT};
        else if (std::holds_alternative<char>(node.value))
            result = expr_result{primitive_type::CHAR};
        else if (std::holds_alternative<std::byte>(node.value))
            result = expr_result{primitive_type::BYTE};
        else if (std::holds_alternative<bool>(node.value))
            result = expr_result{primitive_type::BOOL};
        else
            fail("SEM000", node.source, "string literals have no Step type yet");
    }

    void visit(const identifier &node) override {
        const binding &found = lookup(node);
        result = expr_result{found.type, &found};
    }

    void visit(const conversion_expr &node) override {
        const primitive_type target = resolve(*node.target_type);
        if (target == primitive_type::VOID)
            fail("SEM004", node.source, "invalid conversion target");
        evaluate(*node.value);
        result = expr_result{target};
    }

    void visit(const unary_expr &node) override {
        const primitive_type operand = evaluate(*node.right).type;
        if (node.operation == unary_op::LOGICAL_NOT && truthy(operand)) {
            result = expr_result{primitive_type::BOOL};
            return;
        }
        if (node.operation == unary_op::NEGATE && numeric(operand)) {
            result = expr_result{operand};
            return;
        }
        fail("SEM004", node.source, "invalid unary operand type");
    }

    void visit(const binary_expr &node) override {
        const primitive_type left = evaluate(*node.left).type;
        const primitive_type right = evaluate(*node.right).type;
        switch (node.operation) {
        case binary_op::ADD:
        case binary_op::SUBTRACT:
        case binary_op::MULTIPLY:
        case binary_op::DIVIDE:
        case binary_op::FLOOR_DIVIDE:
        case binary_op::EXPONENT:
            if (numeric(left) && numeric(right)) {
                result = expr_result{left == primitive_type::FLOAT || right == primitive_type::FLOAT
                                         ? primitive_type::FLOAT
                                         : primitive_type::INT};
                return;
            }
            break;
        case binary_op::BITWISE_AND:
        case binary_op::BITWISE_OR:
        case binary_op::BITWISE_XOR:
        case binary_op::LEFT_SHIFT:
        case binary_op::RIGHT_SHIFT:
            if (left == primitive_type::INT && right == primitive_type::INT) {
                result = expr_result{primitive_type::INT};
                return;
            }
            break;
        case binary_op::LOGICAL_AND:
        case binary_op::LOGICAL_OR:
            if (truthy(left) && truthy(right)) {
                result = expr_result{primitive_type::BOOL};
                return;
            }
            break;
        case binary_op::EQUALS:
        case binary_op::NOT_EQUALS:
            if ((numeric(left) && numeric(right)) || left == right) {
                result = expr_result{primitive_type::BOOL};
                return;
            }
            break;
        case binary_op::GT:
        case binary_op::GTE:
        case binary_op::LT:
        case binary_op::LTE:
            if ((numeric(left) && numeric(right)) ||
                (left == right && (left == primitive_type::CHAR || left == primitive_type::BYTE))) {
                result = expr_result{primitive_type::BOOL};
                return;
            }
            break;
        }
        fail("SEM004", node.source, "invalid binary operand types");
    }

    void visit(const assignment_expr &node) override {
        const expr_result target = evaluate(*node.target);
        if (!target.location)
            fail("SEM000", node.target->source, "assignment target is not supported yet");
        if (target.location->immutable)
            fail("SEM012", node.target->source, "cannot assign to an immutable variable");
        if (target.location->state && (!in_step || in_reject))
            fail("SEM017", node.target->source, "state writes require an active step body");
        const primitive_type actual = evaluate(*node.value).type;
        require_compatible(target.type, actual, node.value->source);
        result = expr_result{target.type, nullptr, true};
    }

    void visit(const postfix_expr &node) override {
        fail("SEM000", node.source, "postfix expressions are not supported yet");
    }
    void visit(const array_literal &node) override {
        fail("SEM000", node.source, "array literals are not supported yet");
    }
    void visit(const struct_literal &node) override {
        fail("SEM000", node.source, "struct literals are not supported yet");
    }
    void visit(const dynamic_alloc &node) override {
        fail("SEM000", node.source, "allocation is not supported yet");
    }
    void visit(const this_expr &node) override {
        fail("SEM000", node.source, "this is not supported yet");
    }

    void visit(const primitive_type_syntax &node) override {
        resolved_type = node.type;
    }
    void visit(const named_type_syntax &node) override {
        fail("SEM000", node.source, "named types are not supported yet");
    }
    void visit(const pointer_type_syntax &node) override {
        fail("SEM000", node.source, "pointer types are not supported yet");
    }
    void visit(const array_type_syntax &node) override {
        fail("SEM000", node.source, "array types are not supported yet");
    }

    void visit(const variable_decl &node) override {
        if (in_reject && node.storage == variable_storage::STATE)
            fail("SEM017", node.source, "reject branches cannot declare state");
        const primitive_type declared = resolve(*node.declared_type);
        if (declared == primitive_type::VOID)
            fail("SEM004", node.declared_type->source, "void is not a variable type");
        const std::string &name = node.name->lexeme;
        if (locals.contains(name) || globals.contains(name))
            fail("SEM002", node.name->source, "duplicate name '" + name + "'");
        if (node.initializer)
            require_compatible(declared, evaluate(*node.initializer).type,
                               node.initializer->source);
        auto &destination = global_mode ? globals : locals;
        destination.emplace(
            name, binding{declared, node.storage == variable_storage::STATE, node.immutable});
    }

    void visit(const function_decl &node) override {
        if (!global_mode || node.kind != function_kind::ORDINARY || node.name->lexeme != "main" ||
            main_function || !node.ordinary_parameters.empty() || !node.state_parameters.empty())
            fail("SEM000", node.source, "only one fn main() is supported");
        main_function = &node;
    }
    void visit(const struct_decl &node) override {
        fail("SEM000", node.source, "structs are not supported yet");
    }
    void visit(const impl_decl &node) override {
        fail("SEM000", node.source, "impl blocks are not supported yet");
    }
    void visit(const invariant_decl &node) override {
        fail("SEM000", node.source, "invariants are not supported yet");
    }

    void visit(const block_stmt &node) override {
        const auto saved = locals;
        for (const stmt_ptr &statement : node.statements)
            statement->accept(*this);
        locals = saved;
    }
    void visit(const decl_stmt &node) override {
        node.declaration->accept(*this);
    }
    void visit(const expr_stmt &node) override {
        if (!evaluate(*node.expression).assignment)
            fail("SEM000", node.source, "only assignments are supported yet");
    }
    void visit(const step_stmt &node) override {
        if (in_step || in_reject)
            fail("SEM019", node.source, "nested step blocks are prohibited");
        in_step = true;
        node.body->accept(*this);
        in_step = false;
        if (node.reject_body) {
            in_reject = true;
            node.reject_body->accept(*this);
            in_reject = false;
        }
    }
    void visit(const return_stmt &node) override {
        fail("SEM000", node.source, "return statements are not supported yet");
    }
    void visit(const if_stmt &node) override {
        fail("SEM000", node.source, "if statements are not supported yet");
    }
    void visit(const while_stmt &node) override {
        fail("SEM000", node.source, "while statements are not supported yet");
    }
    void visit(const for_stmt &node) override {
        fail("SEM000", node.source, "for statements are not supported yet");
    }
};
} // namespace

void analyse(const ast_root &root) {
    checker{}.run(root);
}
