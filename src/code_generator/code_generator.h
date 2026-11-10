#include "../ast/declaration.h"
#include "../ast/expression.h"
#include "../ast/statement.h"
#include "../ast/type.h"
#include "../visitor/visitor.h"

class code_generator : visitor {
  public:
    void visit(const program &node);

    // Expressions
    void visit(const binary_expr &node);
    void visit(const unary_expr &node);
    void visit(const postfix_expr &node);
    void visit(const literal &node);
    void visit(const variable &node);
    void visit(const assignment_expr &node);
    void visit(const array_literal &node);
    void visit(const struct_literal &node);
    void visit(const dynamic_alloc &node);
    void visit(const conversion_expr &node);
    void visit(const this_expr &node);

    // Statements
    void visit(const block_stmt &node);
    void visit(const expr_stmt &node);
    void visit(const decl_stmt &node);
    void visit(const return_stmt &node);
    void visit(const if_stmt &node);
    void visit(const while_stmt &node);
    void visit(const for_stmt &node);
    void visit(const step_stmt &node);

    // Declarations
    void visit(const variable_decl &node);
    void visit(const function_decl &node);
    void visit(const struct_decl &node);
    void visit(const impl_decl &node);
    void visit(const invariant_decl &node);

    // Type syntax
    void visit(const primitive_type_syntax &node);
    void visit(const named_type_syntax &node);
    void visit(const pointer_type_syntax &node);
    void visit(const array_type_syntax &node);
};
