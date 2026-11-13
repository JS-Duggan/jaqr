#include "code_generator/code_generator.h"
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>

namespace {
void require(bool condition, std::source_location location = std::source_location::current()) {
    if (!condition)
        throw std::runtime_error("Optimizer regression failed at line " +
                                 std::to_string(location.line()));
}
identifier_ptr id(std::string name) {
    return std::make_unique<identifier>(std::move(name));
}
expr_ptr num(std::int32_t n) {
    return std::make_unique<literal>(n);
}
expr_ptr boolean(bool b) {
    return std::make_unique<literal>(b);
}
type_ptr integer() {
    return std::make_unique<primitive_type_syntax>(primitive_type::INT);
}
expr_ptr bin(binary_op op, expr_ptr a, expr_ptr b) {
    return std::make_unique<binary_expr>(op, std::move(a), std::move(b));
}
expr_ptr assign(std::string name, expr_ptr value) {
    return std::make_unique<assignment_expr>(id(std::move(name)), std::move(value));
}
stmt_ptr expression(expr_ptr e) {
    return std::make_unique<expr_stmt>(std::move(e));
}
stmt_ptr local(std::string name, expr_ptr value) {
    return std::make_unique<decl_stmt>(
        std::make_unique<variable_decl>(id(std::move(name)), integer(), std::move(value)));
}
std::unique_ptr<function_decl> function(std::string name, block_ptr body) {
    return std::make_unique<function_decl>(id(std::move(name)), std::vector<parameter>{}, integer(),
                                           std::move(body));
}
expr_ptr call(std::string name) {
    return std::make_unique<postfix_expr>(id(std::move(name)), arg_list{});
}
std::unique_ptr<for_stmt> loop(int count, block_ptr body) {
    return std::make_unique<for_stmt>(std::make_unique<variable_decl>(id("i"), integer(), num(0)),
                                      bin(binary_op::LT, id("i"), num(count)),
                                      assign("i", bin(binary_op::ADD, id("i"), num(1))),
                                      std::move(body));
}
} // namespace
int main(int argc, char **) try {
    program p;
    p.declarations.push_back(std::make_unique<variable_decl>(id("effects"), integer(), num(0)));
    auto effect = std::make_unique<block_stmt>();
    effect->statements.push_back(
        expression(assign("effects", bin(binary_op::ADD, id("effects"), num(1)))));
    effect->statements.push_back(std::make_unique<return_stmt>(num(7)));
    p.declarations.push_back(function("effect", std::move(effect)));

    auto dead = std::make_unique<block_stmt>();
    dead->statements.push_back(local("unused", num(10)));
    const auto *unused = dead->statements.back().get();
    dead->statements.push_back(local("x", num(0)));
    dead->statements.push_back(expression(assign("x", num(111))));
    const auto *store = dead->statements.back().get();
    dead->statements.push_back(expression(assign("x", bin(binary_op::ADD, num(2), num(3)))));
    dead->statements.push_back(std::make_unique<return_stmt>(id("x")));
    dead->statements.push_back(expression(call("effect")));
    const auto *unreachable = dead->statements.back().get();
    p.declarations.push_back(function("dead", std::move(dead)));

    auto main = std::make_unique<block_stmt>();
    main->statements.push_back(local("sum", num(0)));
    // An unused call result must retain the call.
    main->statements.push_back(local("unused_result", call("effect")));
    auto body = std::make_unique<block_stmt>();
    body->statements.push_back(expression(assign("sum", bin(binary_op::ADD, id("sum"), id("i")))));
    auto small = loop(4, std::move(body));
    const auto *unrolled = small.get();
    main->statements.push_back(std::move(small));
    auto large_body = std::make_unique<block_stmt>();
    large_body->statements.push_back(
        expression(assign("sum", bin(binary_op::ADD, id("sum"), num(1)))));
    auto large = loop(10, std::move(large_body));
    const auto *retained = large.get();
    main->statements.push_back(std::move(large));
    auto zero = loop(0, std::make_unique<block_stmt>());
    zero->initializer->initializer = call("effect");
    zero->condition = boolean(false);
    main->statements.push_back(std::move(zero));
    auto never = std::make_unique<block_stmt>();
    never->statements.push_back(expression(call("effect")));
    main->statements.push_back(std::make_unique<while_stmt>(boolean(false), std::move(never)));
    auto chosen = std::make_unique<block_stmt>();
    chosen->statements.push_back(local("sum", num(500))); // Keep the branch's scope.
    std::vector<conditional_branch> branches;
    branches.emplace_back(bin(binary_op::EQUALS, num(1), num(1)), std::move(chosen));
    auto otherwise = std::make_unique<block_stmt>();
    otherwise->statements.push_back(expression(call("effect")));
    main->statements.push_back(
        std::make_unique<if_stmt>(std::move(branches), std::move(otherwise)));
    main->statements.push_back(
        expression(bin(binary_op::LOGICAL_AND, boolean(false), call("effect"))));

    // Step writes must survive even if the last write overwrites the first.
    auto state =
        std::make_unique<variable_decl>(id("state"), integer(), num(5), variable_storage::STATE);
    const auto *state_input = state.get();
    main->statements.push_back(std::make_unique<decl_stmt>(std::move(state)));
    auto inv = std::make_unique<invariant_decl>(bin(binary_op::GTE, id("state"), num(0)));
    inv->state_vars.push_back(state_input);
    main->statements.push_back(std::make_unique<decl_stmt>(std::move(inv)));
    auto transaction = std::make_unique<block_stmt>();
    transaction->statements.push_back(expression(assign("state", num(-1))));
    const auto *step_write = transaction->statements.back().get();
    transaction->statements.push_back(expression(assign("state", num(6))));
    main->statements.push_back(std::make_unique<step_stmt>(std::move(transaction)));
    auto failed = std::make_unique<block_stmt>();
    failed->statements.push_back(expression(assign("state", num(-2))));
    main->statements.push_back(std::make_unique<step_stmt>(std::move(failed)));
    auto result = bin(binary_op::LOGICAL_OR, bin(binary_op::NOT_EQUALS, id("sum"), num(16)),
                      bin(binary_op::NOT_EQUALS, id("effects"), num(2)));
    result = bin(binary_op::LOGICAL_OR, std::move(result),
                 bin(binary_op::NOT_EQUALS, call("dead"), num(5)));
    result = bin(binary_op::LOGICAL_OR, std::move(result),
                 bin(binary_op::NOT_EQUALS, id("state"), num(6)));
    main->statements.push_back(std::make_unique<return_stmt>(std::move(result)));
    p.declarations.push_back(function("main", std::move(main)));

    const auto plan = optimize(p);
    require(plan.omitted.contains(unused));
    require(plan.omitted.contains(store));
    require(plan.omitted.contains(unreachable));
    require(!plan.omitted.contains(step_write));
    require(plan.unrolled.at(unrolled) == 4);
    require(!plan.unrolled.contains(retained));
    require(optimize(p, {.enabled = false}).constants.empty());
    require(!optimize(p, {.max_unroll_iterations = 0}).unrolled.contains(unrolled));

    // Folding must not introduce signed overflow or fold floating-point operations.
    program edge;
    auto edge_body = std::make_unique<block_stmt>();
    auto overflow = bin(binary_op::ADD, num(std::numeric_limits<std::int32_t>::max()), num(1));
    const auto *overflow_ptr = overflow.get();
    edge_body->statements.push_back(expression(std::move(overflow)));
    auto floating =
        bin(binary_op::ADD, std::make_unique<literal>(1.0), std::make_unique<literal>(2.0));
    const auto *floating_ptr = floating.get();
    edge_body->statements.push_back(expression(std::move(floating)));
    edge.declarations.push_back(function("edge", std::move(edge_body)));
    auto loop_cases = std::make_unique<block_stmt>();
    auto changed_body = std::make_unique<block_stmt>();
    changed_body->statements.push_back(expression(assign("i", num(20))));
    auto changed = loop(3, std::move(changed_body));
    const auto *changed_ptr = changed.get();
    loop_cases->statements.push_back(std::move(changed));
    auto calling_body = std::make_unique<block_stmt>();
    calling_body->statements.push_back(expression(assign("sum", call("effect"))));
    auto calling = loop(3, std::move(calling_body));
    const auto *calling_ptr = calling.get();
    loop_cases->statements.push_back(std::move(calling));
    auto descending = loop(0, std::make_unique<block_stmt>());
    descending->initializer->initializer = num(3);
    descending->condition = bin(binary_op::GTE, id("i"), num(0));
    descending->increment = assign("i", bin(binary_op::SUBTRACT, id("i"), num(1)));
    const auto *descending_ptr = descending.get();
    loop_cases->statements.push_back(std::move(descending));
    auto overflowing = loop(0, std::make_unique<block_stmt>());
    overflowing->initializer->initializer = num(std::numeric_limits<std::int32_t>::max());
    overflowing->condition =
        bin(binary_op::LTE, id("i"), num(std::numeric_limits<std::int32_t>::max()));
    const auto *overflowing_ptr = overflowing.get();
    loop_cases->statements.push_back(std::move(overflowing));
    edge.declarations.push_back(function("loop_cases", std::move(loop_cases)));
    // A reference before a local declaration belongs to the outer binding.
    auto shadowing = std::make_unique<block_stmt>();
    shadowing->statements.push_back(expression(assign("effects", num(1))));
    const auto *outer_store = shadowing->statements.back().get();
    shadowing->statements.push_back(local("effects", num(2)));
    shadowing->statements.push_back(std::make_unique<return_stmt>(num(0)));
    edge.declarations.push_back(function("shadowing", std::move(shadowing)));
    const auto edges = optimize(edge);
    require(!edges.unrolled.contains(changed_ptr));
    require(!edges.unrolled.contains(calling_ptr));
    require(!edges.unrolled.contains(overflowing_ptr));
    require(edges.unrolled.at(descending_ptr) == 4);
    require(!edges.omitted.contains(outer_store));
    require(!optimize(p, {.max_unrolled_statements = 1}).unrolled.contains(unrolled));
    require(!edges.constants.contains(overflow_ptr));
    require(!edges.constants.contains(floating_ptr));

    struct_impls_t impls;
    state_invs_t invs;
    code_generator generator(impls, invs);
    const optimization_options options{.enabled = argc == 1};
    const auto output = generator.gen_code(p, options);
    require(output == generator.gen_code(p, options));
    for (const auto &line : output)
        std::cout << line << '\n';
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
