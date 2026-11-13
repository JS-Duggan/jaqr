#pragma once

#include "../ast/declaration.h"
#include "../tokens/token.h"
#include <vector>

program_ptr parse(const std::vector<token> &tokens);
