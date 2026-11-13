#pragma once

#include "../ast/statement.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>

struct optimization_options {
    bool enabled = true;
    std::size_t max_unroll_iterations = 8;
    std::size_t max_unrolled_statements = 64;
};

// A structured emission plan preserves AST ownership and declaration identities,
// including the pointers held by invariant declarations and impl indexes.
struct optimization_plan {
    std::unordered_map<const expr *, literal_val> constants;
    std::unordered_set<const stmt *> omitted;
    std::unordered_map<const for_stmt *, std::size_t> unrolled;

    std::optional<bool> boolean(const expr *node) const;
};

// Requires a semantically valid AST. Unknown effects are retained conservatively.
optimization_plan optimize(const program &node, optimization_options options = {});
