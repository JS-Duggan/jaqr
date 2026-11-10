#pragma once

#include "expression_base.h"
#include "source.h"
#include "statement_base.h"
#include "type.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class visitor;

struct decl {
    virtual ~decl() = default;
    virtual void accept(visitor &v) const = 0;

    source_span source;
};

using decl_ptr = std::unique_ptr<decl>;

enum class variable_storage : std::uint8_t { ORDINARY, STATE };

// At file scope this represents a define declaration. In a block it is wrapped
// in decl_stmt. State and immutable declarations require an initializer.
struct variable_decl : decl {
    void accept(visitor &v) const override;

    std::string name;
    type_ptr declared_type;
    expr_ptr initializer;
    variable_storage storage;
    bool immutable;

    variable_decl(std::string n, type_ptr type, expr_ptr init = nullptr,
                  variable_storage s = variable_storage::ORDINARY, bool imut = false)
        : name(std::move(n)), declared_type(std::move(type)), initializer(std::move(init)),
          storage(s), immutable(imut) {
    }
};

struct parameter {
    std::string name;
    type_ptr declared_type;

    parameter(std::string n, type_ptr type) : name(std::move(n)), declared_type(std::move(type)) {
    }
};

enum class function_kind : std::uint8_t { ORDINARY, UPDATE };

struct function_decl : decl {
    void accept(visitor &v) const override;

    std::string name;
    function_kind kind;
    std::vector<parameter> ordinary_parameters;
    std::vector<parameter> state_parameters;
    type_ptr return_type; // Null for update functions and update methods.
    block_ptr body;

    // Ordinary function or method, with an explicit return type.
    function_decl(std::string n, std::vector<parameter> params, type_ptr result, block_ptr b)
        : name(std::move(n)), kind(function_kind::ORDINARY), ordinary_parameters(std::move(params)),
          return_type(std::move(result)), body(std::move(b)) {
    }

    // Free update function, with ordinary and state parameter lists.
    function_decl(std::string n, std::vector<parameter> params, std::vector<parameter> state,
                  block_ptr b)
        : name(std::move(n)), kind(function_kind::UPDATE), ordinary_parameters(std::move(params)),
          state_parameters(std::move(state)), return_type(nullptr), body(std::move(b)) {
    }

    // Update method: its enclosing impl supplies the implicit state receiver.
    function_decl(std::string n, std::vector<parameter> params, block_ptr b)
        : function_decl(std::move(n), std::move(params), std::vector<parameter>{}, std::move(b)) {
    }
};

struct field_decl {
    std::string name;
    type_ptr declared_type;

    field_decl(std::string n, type_ptr type) : name(std::move(n)), declared_type(std::move(type)) {
    }
};

struct struct_decl : decl {
    void accept(visitor &v) const override;

    std::string name;
    std::vector<field_decl> fields;

    struct_decl(std::string n, std::vector<field_decl> f)
        : name(std::move(n)), fields(std::move(f)) {
    }
};

struct impl_decl : decl {
    void accept(visitor &v) const override;

    std::string type_name;
    std::vector<std::unique_ptr<function_decl>> methods;

    impl_decl(std::string n, std::vector<std::unique_ptr<function_decl>> m)
        : type_name(std::move(n)), methods(std::move(m)) {
    }
};

// Used directly at file scope, or inside decl_stmt for a local invariant.
struct invariant_decl : decl {
    void accept(visitor &v) const override;

    expr_ptr condition;

    explicit invariant_decl(expr_ptr c) : condition(std::move(c)) {
    }
};

struct program {
    void accept(visitor &v) const;

    std::vector<decl_ptr> declarations;

    explicit program(std::vector<decl_ptr> d = {}) : declarations(std::move(d)) {
    }
};
