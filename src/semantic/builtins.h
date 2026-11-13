#pragma once

#include "../ast/statement.h"
#include <cstdint>
#include <unordered_map>

enum class builtin_function : std::uint8_t { print, scan };
using builtin_calls = std::unordered_map<const postfix_expr *, builtin_function>;

// Resolve built-ins by lexical binding and validate their uses before optimisation.
// This is deliberately separate from the general type checker still to be built.
builtin_calls resolve_builtins(const program &node);
