#include "expression.h"

#include "../visitor/visitor.h"

void binary_expr::accept(visitor &v) const {
    v.visit(*this);
}

void unary_expr::accept(visitor &v) const {
    v.visit(*this);
}

void postfix_expr::accept(visitor &v) const {
    v.visit(*this);
}

void literal::accept(visitor &v) const {
    v.visit(*this);
}

void identifier::accept(visitor &v) const {
    v.visit(*this);
}

void assignment_expr::accept(visitor &v) const {
    v.visit(*this);
}

void array_literal::accept(visitor &v) const {
    v.visit(*this);
}

void struct_literal::accept(visitor &v) const {
    v.visit(*this);
}

void dynamic_alloc::accept(visitor &v) const {
    v.visit(*this);
}

void conversion_expr::accept(visitor &v) const {
    v.visit(*this);
}

void this_expr::accept(visitor &v) const {
    v.visit(*this);
}
