#pragma once

#include "../ast/declaration.h"
#include "../ast/expression.h"
#include "../ast/statement.h"
#include "../ast/type.h"
#include "../visitor/visitor.h"

#include <string>

class code_generator : visitor {
    std::vector<std::string> loc;

  public:
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
