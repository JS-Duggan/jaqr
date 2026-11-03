#pragma once

struct graph_node;

struct edge {
    bool (*matches)(char);
    const graph_node *to;
};
