#pragma once

#include "source.h"

#include <memory>
#include <utility>
#include <vector>

class visitor;

struct stmt {
    virtual ~stmt() = default;
    virtual void accept(visitor &v) const = 0;

    source_span source;
};

using stmt_ptr = std::unique_ptr<stmt>;

// Shared so declarations and control-flow statements can both own block bodies.
struct block_stmt : stmt {
    void accept(visitor &v) const override;

    std::vector<stmt_ptr> statements;

    explicit block_stmt(std::vector<stmt_ptr> s = {}) : statements(std::move(s)) {
    }
};

using block_ptr = std::unique_ptr<block_stmt>;
