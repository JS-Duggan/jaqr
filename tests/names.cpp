#include "code_generator/code_generator.h"
#include <iostream>
#include <stdexcept>

namespace {
identifier_ptr id(std::string name) {
    return std::make_unique<identifier>(std::move(name));
}
type_ptr integer() {
    return std::make_unique<primitive_type_syntax>(primitive_type::INT);
}
expr_ptr number(int value) {
    return std::make_unique<literal>(std::int32_t{value});
}
expr_ptr binary(binary_op op, expr_ptr left, expr_ptr right) {
    return std::make_unique<binary_expr>(op, std::move(left), std::move(right));
}
expr_ptr call(expr_ptr callee, expr_ptr arg = nullptr) {
    arg_list args;
    if (arg)
        args.push_back(std::move(arg));
    return std::make_unique<postfix_expr>(std::move(callee), std::move(args));
}
stmt_ptr local(std::string name, int value, variable_storage storage = variable_storage::ORDINARY) {
    return std::make_unique<decl_stmt>(
        std::make_unique<variable_decl>(id(std::move(name)), integer(), number(value), storage));
}
stmt_ptr expression(expr_ptr value) {
    return std::make_unique<expr_stmt>(std::move(value));
}
void require(bool condition) {
    if (!condition)
        throw std::runtime_error("Name emission regression");
}
} // namespace
int main(int argc, char **) try {
    program node;
    node.declarations.push_back(
        std::make_unique<variable_decl>(id("runtime"), integer(), number(4)));
    std::vector<field_decl> fields;
    fields.emplace_back("private", integer());
    node.declarations.push_back(std::make_unique<struct_decl>(id("template"), std::move(fields)));
    auto method_body = std::make_unique<block_stmt>();
    method_body->statements.push_back(std::make_unique<return_stmt>(binary(
        binary_op::ADD,
        std::make_unique<postfix_expr>(std::make_unique<this_expr>(), std::string("private")),
        id("default"))));
    std::vector<parameter> method_params;
    method_params.emplace_back("default", integer());
    auto method = std::make_unique<function_decl>(id("operator"), std::move(method_params),
                                                  integer(), std::move(method_body));
    struct_impls_t impls;
    impls["template"].push_back(method.get());
    std::vector<function_decl_ptr> methods;
    methods.push_back(std::move(method));
    auto method_main_body = std::make_unique<block_stmt>();
    method_main_body->statements.push_back(
        std::make_unique<return_stmt>(std::make_unique<literal>(2.5)));
    auto method_main = std::make_unique<function_decl>(
        id("main"), std::vector<parameter>{},
        std::make_unique<primitive_type_syntax>(primitive_type::FLOAT),
        std::move(method_main_body));
    impls["template"].push_back(method_main.get());
    methods.push_back(std::move(method_main));
    node.declarations.push_back(std::make_unique<impl_decl>(id("template"), std::move(methods)));
    auto body = std::make_unique<block_stmt>();
    body->statements.push_back(
        std::make_unique<return_stmt>(binary(binary_op::ADD, id("namespace"), number(1))));
    std::vector<parameter> params;
    params.emplace_back("namespace", integer());
    node.declarations.push_back(std::make_unique<function_decl>(id("switch"), std::move(params),
                                                                integer(), std::move(body)));
    auto update_body = std::make_unique<block_stmt>();
    update_body->statements.push_back(expression(std::make_unique<assignment_expr>(
        id("switch"), binary(binary_op::ADD, id("switch"), id("class")))));
    std::vector<parameter> ordinary, state_params;
    ordinary.emplace_back("class", integer());
    state_params.emplace_back("switch", integer());
    node.declarations.push_back(std::make_unique<function_decl>(
        id("delete"), std::move(ordinary), std::move(state_params), std::move(update_body)));
    auto main = std::make_unique<block_stmt>();
    for (const auto *name :
         {"class", "jaqr_class", "_x", "a__b", "escaped_61", "generated_step_0_value_0"})
        main->statements.push_back(local(name, 1));
    std::vector<field_init> init;
    init.push_back({"private", number(4)});
    main->statements.push_back(std::make_unique<decl_stmt>(std::make_unique<variable_decl>(
        id("concept"), std::make_unique<named_type_syntax>("template"),
        std::make_unique<struct_literal>("template", std::move(init)))));
    auto state = std::make_unique<variable_decl>(id("namespace"), integer(), number(5),
                                                 variable_storage::STATE);
    const auto *state_ptr = state.get();
    main->statements.push_back(std::make_unique<decl_stmt>(std::move(state)));
    auto invariant =
        std::make_unique<invariant_decl>(binary(binary_op::GTE, id("namespace"), number(0)));
    invariant->state_vars.push_back(state_ptr);
    main->statements.push_back(std::make_unique<decl_stmt>(std::move(invariant)));
    auto step = std::make_unique<block_stmt>();
    update_arguments args;
    args.regular.push_back(number(1));
    args.state.push_back(id("namespace"));
    step->statements.push_back(
        expression(std::make_unique<postfix_expr>(id("delete"), std::move(args))));
    auto inner_state = std::make_unique<variable_decl>(id("virtual"), integer(), number(2),
                                                       variable_storage::STATE);
    const auto *inner_ptr = inner_state.get();
    step->statements.push_back(std::make_unique<decl_stmt>(std::move(inner_state)));
    auto inner_inv =
        std::make_unique<invariant_decl>(binary(binary_op::EQUALS, id("virtual"), number(2)));
    inner_inv->state_vars.push_back(inner_ptr);
    step->statements.push_back(std::make_unique<decl_stmt>(std::move(inner_inv)));
    main->statements.push_back(std::make_unique<step_stmt>(std::move(step)));
    main->statements.push_back(
        expression(call(id("print"), std::make_unique<literal>(std::string("class")))));
    auto failure = binary(binary_op::NOT_EQUALS, id("namespace"), number(6));
    const auto check = [&](expr_ptr value, expr_ptr expected) {
        failure = binary(binary_op::LOGICAL_OR, std::move(failure),
                         binary(binary_op::NOT_EQUALS, std::move(value), std::move(expected)));
    };
    check(call(id("switch"), id("class")), number(2));
    check(call(std::make_unique<postfix_expr>(id("concept"), std::string("operator")), number(2)),
          number(6));
    check(call(std::make_unique<postfix_expr>(id("concept"), std::string("main"))),
          std::make_unique<literal>(2.5));
    check(id("runtime"), number(4));
    for (const auto *name : {"jaqr_class", "_x", "a__b", "escaped_61", "generated_step_0_value_0"})
        check(id(name), number(1));
    main->statements.push_back(std::make_unique<return_stmt>(std::move(failure)));
    node.declarations.push_back(std::make_unique<function_decl>(
        id("main"), std::vector<parameter>{}, integer(), std::move(main)));
    state_invs_t invs;
    code_generator generator(impls, invs);
    const auto lines = generator.gen_code(node, {.enabled = argc == 1});
    require(lines == generator.gen_code(node, {.enabled = argc == 1}));
    require(state_ptr->name->lexeme == "namespace");
    std::string source;
    for (const auto &line : lines)
        source += line + '\n';
    require(source.find("jaqr_template::jaqr_operator") != std::string::npos);
    require(source.find("double jaqr_template::jaqr_main") != std::string::npos);
    require(source.find("jaqr_jaqr_class") != std::string::npos);
    require(source.find("int main()") != std::string::npos);
    require(source.find("jaqr_runtime::print") != std::string::npos);
    require(source.find("\"class\"") != std::string::npos);
    std::cout << source;
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
