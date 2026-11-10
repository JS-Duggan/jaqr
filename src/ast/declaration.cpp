#include "declaration.h"

#include "../visitor/visitor.h"

void variable_decl::accept(visitor &v) const {
    v.visit(*this);
}

void function_decl::accept(visitor &v) const {
    v.visit(*this);
}

void struct_decl::accept(visitor &v) const {
    v.visit(*this);
}

void impl_decl::accept(visitor &v) const {
    v.visit(*this);
}

void invariant_decl::accept(visitor &v) const {
    v.visit(*this);
}

void program::accept(visitor &v) const {
    v.visit(*this);
}
