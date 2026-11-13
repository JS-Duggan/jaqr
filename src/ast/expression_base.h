#pragma once

#include "source.h"

#include <memory>

class visitor;

// Shared by expression nodes and types whose array bounds are expressions.
struct expr {
    virtual ~expr() = default;

    source_span source;

    virtual void accept(visitor &v) const = 0;
};

using expr_ptr = std::unique_ptr<expr>;
