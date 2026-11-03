
#include "../tokens/token.h"
#include "edge.h"
#include <vector>

struct graph_node {
    token_type type;
    std::vector<edge> edges;
};
