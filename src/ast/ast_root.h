#pragma once

#include "declaration.h"

#include <memory>
#include <utility>
#include <vector>

struct ast_root {
    std::vector<decl_ptr> declarations;

    explicit ast_root(std::vector<decl_ptr> decls = {}) : declarations(std::move(decls)) {
    }
};

using ast_root_ptr = std::unique_ptr<ast_root>;
