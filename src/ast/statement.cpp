#include "statement.h"

#include "../visitor/visitor.h"

void block_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void expr_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void decl_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void return_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void if_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void while_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void for_stmt::accept(visitor &v) const {
    v.visit(*this);
}

void step_stmt::accept(visitor &v) const {
    v.visit(*this);
}
