#include "semantic.h"

#include "../ast/declaration.h"
#include "../ast/expression.h"
#include "../ast/statement.h"
#include "../ast/type.h"
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace {
void fail(const char *code, const source_span &span, const std::string &message) {
    throw std::runtime_error(std::format("{} at line {}, column {}: {}", code, span.start_line,
                                         span.start_col, message));
}

const char *type_name(primitive_type type) {
    return type == primitive_type::INT ? "int" : "bool";
}

class checker {
    std::unordered_map<std::string, primitive_type> locals;

    primitive_type expression(const expr &node) {
        if (const auto *value = dynamic_cast<const literal *>(&node)) {
            if (std::holds_alternative<std::int32_t>(value->value))
                return primitive_type::INT;
            if (std::holds_alternative<bool>(value->value))
                return primitive_type::BOOL;
        }
        if (const auto *name = dynamic_cast<const identifier *>(&node)) {
            const auto found = locals.find(name->lexeme);
            if (found == locals.end())
                fail("SEM001", node.source, "unknown name '" + name->lexeme + "'");
            return found->second;
        }
        fail("SEM000", node.source, "expression is not supported yet");
    }

    void assignment(const assignment_expr &node) {
        const auto *target = dynamic_cast<const identifier *>(node.target.get());
        if (!target)
            fail("SEM000", node.target->source, "assignment target is not supported yet");
        const primitive_type expected = expression(*target);
        const primitive_type actual = expression(*node.value);
        if (expected != actual)
            fail("SEM003", node.value->source,
                 std::format("expected {}, got {}", type_name(expected), type_name(actual)));
    }

    void declaration(const variable_decl &node) {
        if (node.storage != variable_storage::ORDINARY || node.immutable)
            fail("SEM000", node.source, "only local let declarations are supported");
        const auto *type = dynamic_cast<const primitive_type_syntax *>(node.declared_type.get());
        if (!type || (type->type != primitive_type::INT && type->type != primitive_type::BOOL))
            fail("SEM000", node.declared_type->source, "only int and bool types are supported");
        const primitive_type declared = type->type;
        const std::string &name = node.name->lexeme;
        if (locals.contains(name))
            fail("SEM002", node.name->source, "duplicate name '" + name + "'");
        // Evaluate before insertion: a variable cannot refer to itself in its initializer.
        if (node.initializer) {
            const primitive_type actual = expression(*node.initializer);
            if (actual != declared)
                fail("SEM003", node.initializer->source,
                     std::format("expected {}, got {}", type_name(declared), type_name(actual)));
        }
        locals.emplace(name, declared);
    }

  public:
    void run(const ast_root &root) {
        if (root.declarations.size() != 1)
            fail("SEM000", root.declarations.empty() ? source_span{0, 0, 0, 0}
                                                       : root.declarations.back()->source,
                 "exactly one fn main() is supported");
        const auto *main = dynamic_cast<const function_decl *>(root.declarations.front().get());
        if (!main || main->kind != function_kind::ORDINARY || main->name->lexeme != "main" ||
            !main->ordinary_parameters.empty() || !main->state_parameters.empty())
            fail("SEM000", root.declarations.front()->source, "only fn main() is supported");
        for (const stmt_ptr &statement : main->body->statements) {
            if (const auto *local = dynamic_cast<const decl_stmt *>(statement.get())) {
                const auto *variable = dynamic_cast<const variable_decl *>(local->declaration.get());
                if (!variable)
                    fail("SEM000", local->source, "declaration is not supported yet");
                declaration(*variable);
            } else if (const auto *operation = dynamic_cast<const expr_stmt *>(statement.get())) {
                const auto *assign = dynamic_cast<const assignment_expr *>(operation->expression.get());
                if (!assign)
                    fail("SEM000", operation->source, "only assignments are supported yet");
                assignment(*assign);
            } else {
                fail("SEM000", statement->source, "statement is not supported yet");
            }
        }
    }
};
} // namespace

void analyse(const ast_root &root) {
    checker{}.run(root);
}
