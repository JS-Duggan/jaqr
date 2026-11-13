#pragma once

#include "../ast/declaration.h"
#include "../ast/expression.h"
#include "../ast/statement.h"
#include "../ast/type.h"

// Each node's accept() selects its concrete overload. This keeps pass-specific logic
// outside the AST and makes missing node handlers a compile-time error for visitors.
class visitor {
  public:
    virtual ~visitor() = default;

    virtual void visit(const program &node) = 0;

    // Expressions
    virtual void visit(const binary_expr &node) = 0;
    virtual void visit(const unary_expr &node) = 0;
    virtual void visit(const postfix_expr &node) = 0;
    virtual void visit(const literal &node) = 0;
    virtual void visit(const identifier &node) = 0;
    virtual void visit(const assignment_expr &node) = 0;
    virtual void visit(const array_literal &node) = 0;
    virtual void visit(const struct_literal &node) = 0;
    virtual void visit(const dynamic_alloc &node) = 0;
    virtual void visit(const conversion_expr &node) = 0;
    virtual void visit(const this_expr &node) = 0;

    // Statements
    virtual void visit(const block_stmt &node) = 0;
    virtual void visit(const expr_stmt &node) = 0;
    virtual void visit(const decl_stmt &node) = 0;
    virtual void visit(const return_stmt &node) = 0;
    virtual void visit(const if_stmt &node) = 0;
    virtual void visit(const while_stmt &node) = 0;
    virtual void visit(const for_stmt &node) = 0;
    virtual void visit(const step_stmt &node) = 0;

    // Declarations
    virtual void visit(const variable_decl &node) = 0;
    virtual void visit(const function_decl &node) = 0;
    virtual void visit(const struct_decl &node) = 0;
    virtual void visit(const impl_decl &node) = 0;
    virtual void visit(const invariant_decl &node) = 0;

    // Type syntax
    virtual void visit(const primitive_type_syntax &node) = 0;
    virtual void visit(const named_type_syntax &node) = 0;
    virtual void visit(const pointer_type_syntax &node) = 0;
    virtual void visit(const array_type_syntax &node) = 0;
};
