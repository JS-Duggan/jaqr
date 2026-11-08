#pragma once

#include "source.h"

#include <memory>

// Shared by expression nodes and types whose array bounds are expressions.
struct expr {
    virtual ~expr() = default;

    source_span source;
};

using expr_ptr = std::unique_ptr<expr>;
