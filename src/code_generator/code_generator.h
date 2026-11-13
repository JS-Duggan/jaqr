#pragma once

#include "../ast/declaration.h"
#include "../ast/expression.h"
#include "../ast/statement.h"
#include "../ast/type.h"
#include "../optimizer/optimizer.h"
#include "../semantic/builtins.h"
#include "../visitor/visitor.h"

#include <string>
#include <unordered_map>

using struct_impls_t = std::unordered_map<std::string, std::vector<const function_decl *>>;
using state_invs_t = std::unordered_map<std::string, std::vector<const invariant_decl *>>;

class code_generator : visitor {

    std::vector<std::string> loc;
    struct_impls_t impls; // struct name to decl
    state_invs_t invs;    // state name to invs
    std::unordered_map<const invariant_decl *, std::string> invariant_names;
    struct state_binding {
        const void *identity;
        const type_syntax *type;
        bool state;
        bool immutable;
    };
    std::vector<std::unordered_map<std::string, state_binding>> scopes;
    std::vector<std::unordered_map<const void *, std::string>> step_values;
    std::vector<const invariant_decl *> active_invariants;
    std::unordered_map<std::string, const struct_decl *> structs;
    std::size_t next_step = 0;
    std::size_t step_scope_depth = 0;
    bool in_update_function = false;
    optimization_plan optimizations;
    builtin_calls builtins;

    bool emit_constant(const expr &node);

    const state_binding *find_binding(const std::string &name) const;
    std::string state_value(const void *identity, const std::string &name) const;
    bool can_copy_state(const type_syntax &type) const;

  public:
    code_generator(struct_impls_t &imp, state_invs_t &inv) : impls(imp), invs(inv) {
    }

    std::vector<std::string> gen_code(const program &node, optimization_options options = {});

    void visit(const program &node) override;

    // Expressions:
    void visit(const binary_expr &node) override;
    void visit(const unary_expr &node) override;
    void visit(const postfix_expr &node) override;
    void visit(const literal &node) override;
    void visit(const identifier &node) override;
    void visit(const assignment_expr &node) override;
    void visit(const array_literal &node) override;
    void visit(const struct_literal &node) override;
    void visit(const dynamic_alloc &node) override;
    void visit(const conversion_expr &node) override;
    void visit(const this_expr &node) override;

    // Statements
    void visit(const block_stmt &node) override;
    void visit(const expr_stmt &node) override;
    void visit(const decl_stmt &node) override;
    void visit(const return_stmt &node) override;
    void visit(const if_stmt &node) override;
    void visit(const while_stmt &node) override;
    void visit(const for_stmt &node) override;
    void visit(const step_stmt &node) override;

    // Declarations
    void visit(const variable_decl &node) override;
    void visit(const function_decl &node) override;
    void visit(const struct_decl &node) override;
    void visit(const impl_decl &node) override;
    void visit(const invariant_decl &node) override;

    // Type syntax
    void visit(const primitive_type_syntax &node) override;
    void visit(const named_type_syntax &node) override;
    void visit(const pointer_type_syntax &node) override;
    void visit(const array_type_syntax &node) override;
};
