#pragma once

#include <memory>

// Shared by expression nodes and types whose array bounds are expressions.
struct expr {
    virtual ~expr() = default;
};

using expr_ptr = std::unique_ptr<expr>;
