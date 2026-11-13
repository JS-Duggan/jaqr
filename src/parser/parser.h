#pragma once

#include "../ast/ast_root.h"
#include "../tokens/token.h"
#include <vector>

ast_root_ptr parse(const std::vector<token> &tokens);
