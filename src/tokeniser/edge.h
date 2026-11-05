#pragma once

#include <functional>

struct graph_node;

struct edge {
    std::function<bool(char)> matches;
    const graph_node *to;
};
