#pragma once

#include "expression_base.h"
#include "source.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

class visitor;

struct type_syntax {
    virtual ~type_syntax() = default;
    virtual void accept(visitor &v) const = 0;

    source_span source;
};

enum class primitive_type : std::uint8_t { INT, FLOAT, CHAR, BOOL, BYTE, VOID };

constexpr std::string_view spelling(primitive_type type) {
    switch (type) {
    case primitive_type::INT:
        return "int";
    case primitive_type::FLOAT:
        return "float";
    case primitive_type::CHAR:
        return "char";
    case primitive_type::BOOL:
        return "bool";
    case primitive_type::BYTE:
        return "byte";
    case primitive_type::VOID:
        return "void";
    }
    return "<invalid type>";
}

using type_ptr = std::unique_ptr<type_syntax>;

struct primitive_type_syntax : type_syntax {
    void accept(visitor &v) const override;

    primitive_type type;

    explicit primitive_type_syntax(primitive_type t) : type(t) {
    }
};

struct named_type_syntax : type_syntax {
    void accept(visitor &v) const override;

    std::string name;

    explicit named_type_syntax(std::string n) : name(std::move(n)) {
    }
};

struct pointer_type_syntax : type_syntax {
    void accept(visitor &v) const override;

    type_ptr pointee;

    explicit pointer_type_syntax(type_ptr p) : pointee(std::move(p)) {
    }
};

struct array_type_syntax : type_syntax {
    void accept(visitor &v) const override;

    type_ptr element;
    expr_ptr size;

    array_type_syntax(type_ptr e, expr_ptr s) : element(std::move(e)), size(std::move(s)) {
    }
};
