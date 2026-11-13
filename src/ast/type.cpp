#include "type.h"

#include "../visitor/visitor.h"

void primitive_type_syntax::accept(visitor &v) const {
    v.visit(*this);
}

void named_type_syntax::accept(visitor &v) const {
    v.visit(*this);
}

void pointer_type_syntax::accept(visitor &v) const {
    v.visit(*this);
}

void array_type_syntax::accept(visitor &v) const {
    v.visit(*this);
}
