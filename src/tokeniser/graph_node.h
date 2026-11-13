#pragma once

#include "../tokens/token.h"
#include "edge.h"
#include <memory>
#include <optional>
#include <vector>

// Nodes own their outgoing edges, but edge targets borrow the static nodes in dfa.cpp.
// Cycles therefore model repeated input without creating ownership cycles.
struct graph_node {
    std::optional<token_type> type; // empty = not an accepting state
    std::vector<std::unique_ptr<edge>> edges;
};
