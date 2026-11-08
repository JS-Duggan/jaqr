#pragma once

#include "source.h"

#include <memory>
#include <utility>
#include <vector>

struct stmt {
    virtual ~stmt() = default;

    source_span source;
};

using stmt_ptr = std::unique_ptr<stmt>;

// Shared so declarations and control-flow statements can both own block bodies.
struct block_stmt : stmt {
    std::vector<stmt_ptr> statements;

    explicit block_stmt(std::vector<stmt_ptr> s = {}) : statements(std::move(s)) {
    }
};

using block_ptr = std::unique_ptr<block_stmt>;
