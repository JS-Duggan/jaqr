#pragma once

#include "expression_base.h"
#include "source.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

struct type_syntax {
    virtual ~type_syntax() = default;

    source_span source;
};

enum class primitive_type : std::uint8_t { INT, FLOAT, CHAR, BOOL, BYTE };

using type_ptr = std::unique_ptr<type_syntax>;

struct primitive_type_syntax : type_syntax {
    primitive_type kind;

    explicit primitive_type_syntax(primitive_type k) : kind(k) {
    }
};

struct named_type_syntax : type_syntax {
    std::string name;

    explicit named_type_syntax(std::string n) : name(std::move(n)) {
    }
};

struct pointer_type_syntax : type_syntax {
    type_ptr pointee;

    explicit pointer_type_syntax(type_ptr p) : pointee(std::move(p)) {
    }
};

struct array_type_syntax : type_syntax {
    type_ptr element;
    expr_ptr size;

    array_type_syntax(type_ptr e, expr_ptr s) : element(std::move(e)), size(std::move(s)) {
    }
};
