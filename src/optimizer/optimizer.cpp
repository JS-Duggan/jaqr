#include "optimizer.h"

#include <limits>
#include <string>

namespace {
class optimizer {
    optimization_options options;
    optimization_plan plan;

    const literal_val *constant(const expr *node) const {
        const auto found = plan.constants.find(node);
        return found == plan.constants.end() ? nullptr : &found->second;
    }
    std::optional<std::int32_t> integer(const expr *node) const {
        if (const auto *value = constant(node)) {
            if (const auto *number = std::get_if<std::int32_t>(value))
                return *number;
        }
        return {};
    }
    // Calculate in a wider type, but only substitute results representable by JAQR int.
    // An unprovable result stays in the emitted program rather than wrapping here.
    void store_integer(const expr *node, std::int64_t value) {
        if (value >= std::numeric_limits<std::int32_t>::min() &&
            value <= std::numeric_limits<std::int32_t>::max())
            plan.constants.emplace(node, static_cast<std::int32_t>(value));
    }
    void expression(const expr *node) {
        if (!node)
            return;
        if (const auto *value = dynamic_cast<const literal *>(node)) {
            plan.constants.emplace(node, value->value);
        } else if (const auto *binary = dynamic_cast<const binary_expr *>(node)) {
            expression(binary->left.get());
            expression(binary->right.get());
            const auto a = integer(binary->left.get());
            const auto b = integer(binary->right.get());
            if (a && b) {
                const std::int64_t lhs = *a, rhs = *b;
                switch (binary->operation) {
                case binary_op::ADD:
                    store_integer(node, lhs + rhs);
                    break;
                case binary_op::SUBTRACT:
                    store_integer(node, lhs - rhs);
                    break;
                case binary_op::MULTIPLY:
                    store_integer(node, lhs * rhs);
                    break;
                case binary_op::EQUALS:
                    plan.constants.emplace(node, lhs == rhs);
                    break;
                case binary_op::NOT_EQUALS:
                    plan.constants.emplace(node, lhs != rhs);
                    break;
                case binary_op::GT:
                    plan.constants.emplace(node, lhs > rhs);
                    break;
                case binary_op::GTE:
                    plan.constants.emplace(node, lhs >= rhs);
                    break;
                case binary_op::LT:
                    plan.constants.emplace(node, lhs < rhs);
                    break;
                case binary_op::LTE:
                    plan.constants.emplace(node, lhs <= rhs);
                    break;
                default:
                    break; // Division, shifts and floating-point evaluation stay at runtime.
                }
            }
            // A known left operand may make the right operand unreachable through
            // short-circuiting. A known right operand alone cannot justify dropping left.
            const auto lhs = plan.boolean(binary->left.get());
            const auto rhs = plan.boolean(binary->right.get());
            if (lhs) {
                if (binary->operation == binary_op::LOGICAL_AND && (!*lhs || rhs))
                    plan.constants.emplace(node, *lhs && rhs.value_or(false));
                if (binary->operation == binary_op::LOGICAL_OR && (*lhs || rhs))
                    plan.constants.emplace(node, *lhs || rhs.value_or(false));
                if (rhs && binary->operation == binary_op::EQUALS)
                    plan.constants.emplace(node, *lhs == *rhs);
                if (rhs && binary->operation == binary_op::NOT_EQUALS)
                    plan.constants.emplace(node, *lhs != *rhs);
            }
        } else if (const auto *unary = dynamic_cast<const unary_expr *>(node)) {
            expression(unary->right.get());
            if (unary->operation == unary_op::NEGATE) {
                if (const auto value = integer(unary->right.get()))
                    store_integer(node, -static_cast<std::int64_t>(*value));
            } else if (unary->operation == unary_op::LOGICAL_NOT) {
                if (const auto value = plan.boolean(unary->right.get()))
                    plan.constants.emplace(node, !*value);
            }
        } else if (const auto *assignment = dynamic_cast<const assignment_expr *>(node)) {
            expression(assignment->target.get());
            expression(assignment->value.get());
        } else if (const auto *postfix = dynamic_cast<const postfix_expr *>(node)) {
            expression(postfix->obj.get());
            if (const auto *index = std::get_if<expr_ptr>(&postfix->arg))
                expression(index->get());
            if (const auto *args = std::get_if<arg_list>(&postfix->arg))
                for (const auto &arg : *args)
                    expression(arg.get());
            if (const auto *args = std::get_if<update_arguments>(&postfix->arg)) {
                for (const auto &arg : args->regular)
                    expression(arg.get());
                for (const auto &arg : args->state)
                    expression(arg.get());
            }
        } else if (const auto *array = dynamic_cast<const array_literal *>(node)) {
            for (const auto &element : array->elements)
                expression(element.get());
        } else if (const auto *record = dynamic_cast<const struct_literal *>(node)) {
            for (const auto &field : record->init_list)
                expression(field.val.get());
        } else if (const auto *allocation = dynamic_cast<const dynamic_alloc *>(node)) {
            expression(allocation->count.get());
        } else if (const auto *conversion = dynamic_cast<const conversion_expr *>(node)) {
            expression(conversion->value.get());
        }
    }

    // Only these expressions have no calls, aliases, writes or hidden state access.
    bool reads(const expr *node, std::unordered_set<std::string> &names) const {
        if (!node || constant(node))
            return true;
        if (const auto *name = dynamic_cast<const identifier *>(node)) {
            names.insert(name->lexeme);
            return true;
        }
        if (const auto *binary = dynamic_cast<const binary_expr *>(node))
            return reads(binary->left.get(), names) && reads(binary->right.get(), names);
        if (const auto *unary = dynamic_cast<const unary_expr *>(node))
            return (unary->operation == unary_op::NEGATE ||
                    unary->operation == unary_op::LOGICAL_NOT) &&
                   reads(unary->right.get(), names);
        return false;
    }

    // Prove the trip count only for a bounded integer-counter pattern. Calls, aliasing
    // writes and body changes to the counter prevent that proof, so leave those loops.
    // Both iteration and statement budgets bound compile time and generated code size.
    void unroll(const for_stmt &loop) {
        if (plan.boolean(loop.condition.get()) == false) {
            plan.unrolled.emplace(&loop, 0); // Preserve the initializer even for zero iterations.
            return;
        }
        if (!loop.initializer || loop.initializer->immutable ||
            loop.initializer->storage != variable_storage::ORDINARY)
            return;
        const auto *type =
            dynamic_cast<const primitive_type_syntax *>(loop.initializer->declared_type.get());
        if (!type || type->type != primitive_type::INT)
            return;
        const auto initial = integer(loop.initializer->initializer.get());
        const auto *test = dynamic_cast<const binary_expr *>(loop.condition.get());
        const auto *increment = dynamic_cast<const assignment_expr *>(loop.increment.get());
        if (!initial || !test || !increment)
            return;
        const auto &name = loop.initializer->name->lexeme;
        const auto matches = [&](const expr *node) {
            const auto *id = dynamic_cast<const identifier *>(node);
            return id && id->lexeme == name;
        };
        const auto bound = integer(test->right.get());
        const auto *next = dynamic_cast<const binary_expr *>(increment->value.get());
        if (!bound || !matches(test->left.get()) || !matches(increment->target.get()) || !next ||
            !matches(next->left.get()))
            return;
        const auto amount = integer(next->right.get());
        if (!amount ||
            (next->operation != binary_op::ADD && next->operation != binary_op::SUBTRACT))
            return;
        const std::int64_t delta =
            next->operation == binary_op::ADD ? *amount : -static_cast<std::int64_t>(*amount);
        if (!delta)
            return;
        for (const auto &statement : loop.body->statements) {
            const auto *item = dynamic_cast<const expr_stmt *>(statement.get());
            if (!item)
                return;
            const auto *assignment = dynamic_cast<const assignment_expr *>(item->expression.get());
            if (!assignment || !dynamic_cast<const identifier *>(assignment->target.get()) ||
                matches(assignment->target.get()))
                return;
            std::unordered_set<std::string> used;
            if (!reads(assignment->value.get(), used))
                return;
        }
        std::int64_t value = *initial;
        std::size_t count = 0;
        for (;;) {
            bool execute;
            switch (test->operation) {
            case binary_op::LT:
                execute = value < *bound;
                break;
            case binary_op::LTE:
                execute = value <= *bound;
                break;
            case binary_op::GT:
                execute = value > *bound;
                break;
            case binary_op::GTE:
                execute = value >= *bound;
                break;
            default:
                return;
            }
            if (!execute)
                break;
            ++count;
            if (count > options.max_unroll_iterations ||
                count > options.max_unrolled_statements / (loop.body->statements.size() + 1))
                return;
            value += delta;
            if (value < std::numeric_limits<std::int32_t>::min() ||
                value > std::numeric_limits<std::int32_t>::max())
                return;
        }
        plan.unrolled.emplace(&loop, count);
    }

    // Restrict liveness to locals owned by this block. Any complex statement is
    // a barrier; calls and address-taking also disable removal for the block.
    void dead_locals(const block_stmt &block) {
        std::unordered_map<std::string, const variable_decl *> locals;
        bool seen_execution = false;
        for (const auto &statement : block.statements) {
            if (plan.omitted.contains(statement.get()))
                continue;
            if (const auto *decl = dynamic_cast<const decl_stmt *>(statement.get())) {
                if (seen_execution)
                    return;
                const auto *variable = dynamic_cast<const variable_decl *>(decl->declaration.get());
                if (!variable)
                    return;
                const auto *type =
                    dynamic_cast<const primitive_type_syntax *>(variable->declared_type.get());
                if (type && type->type == primitive_type::INT && !variable->immutable &&
                    variable->storage == variable_storage::ORDINARY)
                    locals.emplace(variable->name->lexeme, variable);
            } else
                seen_execution = true;
            std::unordered_set<std::string> used;
            const auto *item = dynamic_cast<const expr_stmt *>(statement.get());
            if (item) {
                const auto *assignment =
                    dynamic_cast<const assignment_expr *>(item->expression.get());
                if (!assignment || !dynamic_cast<const identifier *>(assignment->target.get()) ||
                    !reads(assignment->value.get(), used))
                    return;
            } else if (const auto *ret = dynamic_cast<const return_stmt *>(statement.get())) {
                if (!reads(ret->value.get(), used))
                    return;
            } else if (!dynamic_cast<const decl_stmt *>(statement.get()))
                return;
        }
        // An initializer can expose aliases or call code that observes earlier locals.
        for (const auto &statement : block.statements) {
            if (plan.omitted.contains(statement.get()))
                continue;
            if (const auto *decl = dynamic_cast<const decl_stmt *>(statement.get())) {
                const auto *variable = dynamic_cast<const variable_decl *>(decl->declaration.get());
                std::unordered_set<std::string> used;
                if (!reads(variable->initializer.get(), used))
                    return;
            }
        }
        // Walk backwards: a read needs the preceding value, while an assignment kills
        // that need. Only known constant integer writes to unused locals may be dropped.
        std::unordered_set<std::string> live;
        for (auto it = block.statements.rbegin(); it != block.statements.rend(); ++it) {
            const auto *statement = it->get();
            if (plan.omitted.contains(statement))
                continue;
            if (const auto *item = dynamic_cast<const expr_stmt *>(statement)) {
                const auto *assignment =
                    dynamic_cast<const assignment_expr *>(item->expression.get());
                const auto &name = static_cast<const identifier &>(*assignment->target).lexeme;
                if (locals.contains(name) && !live.contains(name) &&
                    integer(assignment->value.get())) {
                    plan.omitted.insert(statement);
                    continue;
                }
                live.erase(name);
                reads(assignment->value.get(), live);
            } else if (const auto *ret = dynamic_cast<const return_stmt *>(statement)) {
                reads(ret->value.get(), live);
            } else if (const auto *decl = dynamic_cast<const decl_stmt *>(statement)) {
                const auto &variable = static_cast<const variable_decl &>(*decl->declaration);
                if (locals.contains(variable.name->lexeme) &&
                    !live.contains(variable.name->lexeme) && integer(variable.initializer.get())) {
                    // Keep the binding if surviving assignments still target it.
                    bool written = false;
                    for (const auto &other : block.statements) {
                        if (plan.omitted.contains(other.get()))
                            continue;
                        if (const auto *item = dynamic_cast<const expr_stmt *>(other.get())) {
                            const auto *assignment =
                                dynamic_cast<const assignment_expr *>(item->expression.get());
                            written |=
                                static_cast<const identifier &>(*assignment->target).lexeme ==
                                variable.name->lexeme;
                        }
                    }
                    if (!written)
                        plan.omitted.insert(statement);
                }
                live.erase(variable.name->lexeme);
                reads(variable.initializer.get(), live);
            }
        }
    }

    bool block(const block_stmt &node) {
        bool terminated = false;
        for (const auto &item : node.statements) {
            if (terminated)
                plan.omitted.insert(item.get());
            else
                terminated = statement(*item);
        }
        dead_locals(node);
        return terminated;
    }
    bool statement(const stmt &node) {
        if (const auto *body = dynamic_cast<const block_stmt *>(&node))
            return block(*body);
        if (const auto *item = dynamic_cast<const expr_stmt *>(&node))
            expression(item->expression.get());
        else if (const auto *decl = dynamic_cast<const decl_stmt *>(&node))
            declaration(*decl->declaration);
        else if (const auto *ret = dynamic_cast<const return_stmt *>(&node)) {
            expression(ret->value.get());
            return true;
        } else if (const auto *conditional = dynamic_cast<const if_stmt *>(&node)) {
            bool all_return = true;
            for (const auto &branch : conditional->branches) {
                expression(branch.condition.get());
                const auto condition = plan.boolean(branch.condition.get());
                if (condition == false)
                    continue;
                all_return = block(*branch.body) && all_return;
                if (condition == true)
                    return all_return;
            }
            return conditional->else_body && block(*conditional->else_body) && all_return;
        } else if (const auto *loop = dynamic_cast<const while_stmt *>(&node)) {
            expression(loop->condition.get());
            if (plan.boolean(loop->condition.get()) == false)
                plan.omitted.insert(&node);
            else
                block(*loop->body);
        } else if (const auto *loop = dynamic_cast<const for_stmt *>(&node)) {
            if (loop->initializer)
                declaration(*loop->initializer);
            expression(loop->condition.get());
            expression(loop->increment.get());
            block(*loop->body);
            unroll(*loop);
        } else if (const auto *step = dynamic_cast<const step_stmt *>(&node)) {
            // Keep transactional bodies intact: their declarations and writes feed validation.
            // Ordinary nested expressions are deliberately left alone in this first version.
            (void)step;
        }
        return false;
    }
    void declaration(const decl &node) {
        if (const auto *variable = dynamic_cast<const variable_decl *>(&node))
            expression(variable->initializer.get());
        else if (const auto *function = dynamic_cast<const function_decl *>(&node))
            block(*function->body);
        else if (const auto *impl = dynamic_cast<const impl_decl *>(&node))
            for (const auto &method : impl->methods)
                declaration(*method);
        else if (const auto *invariant = dynamic_cast<const invariant_decl *>(&node))
            expression(invariant->condition.get());
    }

  public:
    explicit optimizer(optimization_options settings) : options(settings) {
    }
    optimization_plan run(const program &node) {
        if (options.enabled)
            for (const auto &decl : node.declarations)
                declaration(*decl);
        return std::move(plan);
    }
};
} // namespace

std::optional<bool> optimization_plan::boolean(const expr *node) const {
    const auto found = constants.find(node);
    if (found != constants.end()) {
        if (const auto *value = std::get_if<bool>(&found->second))
            return *value;
    }
    return {};
}

optimization_plan optimize(const program &node, optimization_options options) {
    return optimizer(options).run(node);
}
