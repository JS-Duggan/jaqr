#pragma once

#include "../tokens/token.h"
#include <vector>

// input is one vector per source line, including '\n'
std::vector<token> tokenise(const std::vector<std::vector<char>> &input);
