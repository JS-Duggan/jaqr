#include "code_generator/code_generator.h"
#include <iostream>
#include <stdexcept>

namespace {
identifier_ptr id(std::string name) {
    return std::make_unique<identifier>(std::move(name));
}
expr_ptr num(int value) {
    return std::make_unique<literal>(std::int32_t{value});
}
type_ptr type(primitive_type value = primitive_type::INT) {
    return std::make_unique<primitive_type_syntax>(value);
}
expr_ptr call(std::string name, expr_ptr value = nullptr) {
    arg_list args;
    if (value)
        args.push_back(std::move(value));
    return std::make_unique<postfix_expr>(id(std::move(name)), std::move(args));
}
stmt_ptr expression(expr_ptr value) {
    return std::make_unique<expr_stmt>(std::move(value));
}
stmt_ptr print(expr_ptr value) {
    return expression(call("print", std::move(value)));
}
stmt_ptr text(std::string value) {
    return print(std::make_unique<literal>(std::move(value)));
}
stmt_ptr local(std::string name, primitive_type kind, expr_ptr init, bool immutable = false) {
    return std::make_unique<decl_stmt>(std::make_unique<variable_decl>(
        id(std::move(name)), type(kind), std::move(init), variable_storage::ORDINARY, immutable));
}
function_decl_ptr function(std::string name, block_ptr body) {
    return std::make_unique<function_decl>(id(std::move(name)), std::vector<parameter>{}, type(),
                                           std::move(body));
}
void expect_failure(program &node, const std::string &message) {
    struct_impls_t impls;
    state_invs_t invs;
    code_generator generator(impls, invs);
    try {
        generator.gen_code(node);
    } catch (const std::invalid_argument &error) {
        if (std::string(error.what()).find(message) != std::string::npos)
            return;
        throw;
    }
    throw std::runtime_error("Expected error: " + message);
}
void diagnostic_tests() {
    for (int scenario = 0; scenario < 12; ++scenario) {
        program node;
        auto body = std::make_unique<block_stmt>();
        std::string message;
        if (scenario == 0) {
            body->statements.push_back(expression(call("scan", num(5))));
            message = "writable";
        } else if (scenario == 1) {
            body->statements.push_back(local("x", primitive_type::INT, num(5), true));
            body->statements.push_back(expression(call("scan", id("x"))));
            message = "writable";
        } else if (scenario == 2) {
            body->statements.push_back(expression(call("print")));
            message = "exactly one";
        } else if (scenario == 3) {
            body->statements.push_back(print(std::make_unique<literal>(std::byte{3})));
            message = "print supports";
        } else if (scenario == 4) {
            auto transaction = std::make_unique<block_stmt>();
            transaction->statements.push_back(print(num(1)));
            body->statements.push_back(std::make_unique<step_stmt>(std::move(transaction)));
            message = "not allowed";
        } else if (scenario == 5) {
            auto helper = std::make_unique<block_stmt>();
            helper->statements.push_back(print(num(1)));
            node.declarations.push_back(function("helper", std::move(helper)));
            auto intermediary = std::make_unique<block_stmt>();
            intermediary->statements.push_back(expression(call("helper")));
            node.declarations.push_back(function("intermediary", std::move(intermediary)));
            auto transaction = std::make_unique<block_stmt>();
            transaction->statements.push_back(expression(call("intermediary")));
            body->statements.push_back(std::make_unique<step_stmt>(std::move(transaction)));
            message = "through function";
        } else if (scenario == 6) {
            body->statements.push_back(std::make_unique<decl_stmt>(
                std::make_unique<variable_decl>(id("s"), type(), num(0), variable_storage::STATE)));
            body->statements.push_back(expression(call("scan", id("s"))));
            message = "non-state";
        } else if (scenario == 7) {
            // Validation must happen before unreachable-code removal.
            body->statements.push_back(std::make_unique<return_stmt>(num(0)));
            body->statements.push_back(expression(call("scan", num(1))));
            message = "writable";
        } else if (scenario == 9) {
            body->statements.push_back(local("x", primitive_type::INT, num(0)));
            body->statements.push_back(expression(
                call("scan", std::make_unique<binary_expr>(binary_op::ADD, id("x"), num(1)))));
            message = "writable";
        } else if (scenario == 10) {
            arg_list arguments;
            arguments.push_back(num(1));
            arguments.push_back(num(2));
            body->statements.push_back(
                expression(std::make_unique<postfix_expr>(id("print"), std::move(arguments))));
            message = "exactly one";
        } else if (scenario == 11) {
            body->statements.push_back(std::make_unique<decl_stmt>(
                std::make_unique<invariant_decl>(call("print", num(1)))));
            message = "not allowed";
        } else {
            body->statements.push_back(
                expression(call("scan", std::make_unique<literal>(std::string("text")))));
            message = "writable";
        }
        node.declarations.push_back(function("main", std::move(body)));
        expect_failure(node, message);
    }
    // A user function called print must remain an ordinary function.
    program shadowed;
    auto custom_body = std::make_unique<block_stmt>();
    custom_body->statements.push_back(std::make_unique<return_stmt>(num(8)));
    shadowed.declarations.push_back(function("print", std::move(custom_body)));
    auto body = std::make_unique<block_stmt>();
    body->statements.push_back(std::make_unique<return_stmt>(call("print")));
    shadowed.declarations.push_back(function("main", std::move(body)));
    if (!resolve_builtins(shadowed).empty())
        throw std::runtime_error("Shadowed function mapped to builtin");
    // A block-local binding shadows only within its lexical scope.
    program scoped;
    auto scoped_body = std::make_unique<block_stmt>();
    auto inner = std::make_unique<block_stmt>();
    inner->statements.push_back(local("print", primitive_type::INT, num(1)));
    inner->statements.push_back(expression(call("print", num(1))));
    scoped_body->statements.push_back(std::move(inner));
    scoped_body->statements.push_back(print(num(2)));
    scoped.declarations.push_back(function("main", std::move(scoped_body)));
    if (resolve_builtins(scoped).size() != 1)
        throw std::runtime_error("Incorrect block shadowing");
    program update;
    auto update_body = std::make_unique<block_stmt>();
    update_body->statements.push_back(print(num(1)));
    update.declarations.push_back(std::make_unique<function_decl>(
        id("update"), std::vector<parameter>{}, std::move(update_body)));
    expect_failure(update, "not allowed");
}
} // namespace
int main(int argc, char **) try {
    diagnostic_tests();
    program node;
    std::vector<field_decl> fields;
    fields.emplace_back("x", type());
    node.declarations.push_back(std::make_unique<struct_decl>(id("Point"), std::move(fields)));
    auto body = std::make_unique<block_stmt>();
    body->statements.push_back(local("number", primitive_type::INT, num(0)));
    body->statements.push_back(
        local("fraction", primitive_type::FLOAT, std::make_unique<literal>(0.0)));
    body->statements.push_back(
        local("character", primitive_type::CHAR, std::make_unique<literal>('?')));
    body->statements.push_back(
        local("flag", primitive_type::BOOL, std::make_unique<literal>(false)));
    body->statements.push_back(local("constant", primitive_type::INT, num(42), true));
    body->statements.push_back(text("values:"));
    for (const auto *name : {"number", "fraction", "character", "flag"}) {
        body->statements.push_back(print(call("scan", id(name))));
        body->statements.push_back(text("="));
        body->statements.push_back(print(id(name)));
        body->statements.push_back(text(";"));
    }
    body->statements.push_back(print(id("constant")));
    body->statements.push_back(text("\n"));
    arg_list elements;
    elements.push_back(num(0));
    elements.push_back(num(0));
    body->statements.push_back(std::make_unique<decl_stmt>(std::make_unique<variable_decl>(
        id("array"), std::make_unique<array_type_syntax>(type(), num(2)),
        std::make_unique<array_literal>(std::move(elements)))));
    std::vector<field_init> values;
    values.push_back({"x", num(0)});
    body->statements.push_back(std::make_unique<decl_stmt>(std::make_unique<variable_decl>(
        id("point"), std::make_unique<named_type_syntax>("Point"),
        std::make_unique<struct_literal>("Point", std::move(values)))));
    const auto element = [] { return std::make_unique<postfix_expr>(id("array"), num(1)); };
    const auto field = [] { return std::make_unique<postfix_expr>(id("point"), std::string("x")); };
    body->statements.push_back(print(call("scan", element())));
    body->statements.push_back(text("="));
    body->statements.push_back(print(element()));
    body->statements.push_back(text(";"));
    body->statements.push_back(print(call("scan", field())));
    body->statements.push_back(text("="));
    body->statements.push_back(print(field()));
    body->statements.push_back(text(";"));
    std::vector<field_init> temporary;
    temporary.push_back({"x", num(6)});
    body->statements.push_back(print(std::make_unique<postfix_expr>(
        std::make_unique<struct_literal>("Point", std::move(temporary)), std::string("x"))));
    body->statements.push_back(text("\n"));
    body->statements.push_back(local("failed", primitive_type::INT, num(99)));
    body->statements.push_back(print(call("scan", id("failed"))));
    body->statements.push_back(text(";"));
    body->statements.push_back(print(call("scan", id("failed"))));
    body->statements.push_back(text("\n"));
    body->statements.push_back(std::make_unique<return_stmt>(num(0)));
    node.declarations.push_back(function("main", std::move(body)));
    struct_impls_t impls;
    state_invs_t invs;
    code_generator generator(impls, invs);
    const auto source = generator.gen_code(node, {.enabled = argc == 1});
    if (source != generator.gen_code(node, {.enabled = argc == 1}))
        throw std::runtime_error("Unstable output");
    for (const auto &line : source)
        std::cout << line << '\n';
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
