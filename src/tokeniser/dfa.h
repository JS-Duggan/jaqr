#pragma once

#include "graph_node.h"

// Start state of the tokeniser DFA.
// Edges are checked in order and the first match wins, so specific edges come before general ones.
extern const graph_node *const dfa_start;
