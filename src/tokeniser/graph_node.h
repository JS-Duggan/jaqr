#pragma once

#include "../tokens/token.h"
#include "edge.h"
#include <memory>
#include <optional>
#include <vector>

struct graph_node {
    std::optional<token_type> type; // empty = not an accepting state
    std::vector<std::unique_ptr<edge>> edges;
};
