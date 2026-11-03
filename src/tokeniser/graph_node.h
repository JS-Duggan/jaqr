
#include "../tokens/token.h"
#include "edge.h"
#include <optional>
#include <vector>

struct graph_node {
    std::optional<token_type> type; // empty = not an accepting state
    std::vector<edge> edges;
};
