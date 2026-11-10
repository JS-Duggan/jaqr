#pragma once

#include "declaration.h"
#include "expression_base.h"
#include "statement_base.h"

#include <memory>
#include <utility>
#include <vector>

struct expr_stmt : stmt {
    void accept(visitor &v) const override;

    expr_ptr expression;

    explicit expr_stmt(expr_ptr e) : expression(std::move(e)) {
    }
};

// Wraps local variable declarations and invariants. The parser controls which
// declarations are allowed in a statement position.
struct decl_stmt : stmt {
    void accept(visitor &v) const override;

    decl_ptr declaration;

    explicit decl_stmt(decl_ptr d) : declaration(std::move(d)) {
    }
};

struct return_stmt : stmt {
    void accept(visitor &v) const override;

    expr_ptr value;

    explicit return_stmt(expr_ptr v = nullptr) : value(std::move(v)) {
    }
};

struct conditional_branch {
    expr_ptr condition;
    block_ptr body;

    conditional_branch(expr_ptr c, block_ptr b) : condition(std::move(c)), body(std::move(b)) {
    }
};

struct if_stmt : stmt {
    void accept(visitor &v) const override;

    // Nonempty: the first branch is if; remaining branches are elif, in order.
    std::vector<conditional_branch> branches;
    block_ptr else_body; // Null when there is no else branch.

    explicit if_stmt(std::vector<conditional_branch> b, block_ptr otherwise = nullptr)
        : branches(std::move(b)), else_body(std::move(otherwise)) {
    }
};

struct while_stmt : stmt {
    void accept(visitor &v) const override;

    expr_ptr condition;
    block_ptr body;

    while_stmt(expr_ptr c, block_ptr b) : condition(std::move(c)), body(std::move(b)) {
    }
};

struct for_stmt : stmt {
    void accept(visitor &v) const override;

    // The current grammar requires a let declaration and all three clauses.
    std::unique_ptr<variable_decl> initializer;
    expr_ptr condition;
    expr_ptr increment;
    block_ptr body;

    for_stmt(std::unique_ptr<variable_decl> init, expr_ptr test, expr_ptr next, block_ptr b)
        : initializer(std::move(init)), condition(std::move(test)), increment(std::move(next)),
          body(std::move(b)) {
    }
};

struct step_stmt : stmt {
    void accept(visitor &v) const override;

    block_ptr body;
    block_ptr reject_body; // Null when there is no reject branch.

    explicit step_stmt(block_ptr b, block_ptr rejection = nullptr)
        : body(std::move(b)), reject_body(std::move(rejection)) {
    }
};
