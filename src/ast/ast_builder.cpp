
#include "ast_builder.h"
#include "ast_root.h"
#include "declaration.h"
#include "expression.h"
#include "expression_base.h"
#include "source.h"
#include "statement.h"
#include "statement_base.h"
#include "type.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// builds a node and records the source text it was parsed from
template <typename node_type, typename... arg_types>
static std::unique_ptr<node_type> make_node(const source_span &span, arg_types &&...args) {
    std::unique_ptr<node_type> node = std::make_unique<node_type>(std::forward<arg_types>(args)...);
    node->source = span;
    return node;
}

// ---- Root ----

ast_root_ptr make_ast_root(std::vector<decl_ptr> decls) {
    return make_program(std::move(decls));
}

// ---- Expressions ----

expr_ptr make_binary_expr(const source_span &span, binary_op o, expr_ptr lhs, expr_ptr rhs) {
    return make_node<binary_expr>(span, o, std::move(lhs), std::move(rhs));
}

expr_ptr make_unary_expr(const source_span &span, unary_op o, expr_ptr rhs) {
    return make_node<unary_expr>(span, o, std::move(rhs));
}

// object[index]
expr_ptr make_subscript_expr(const source_span &span, expr_ptr object, expr_ptr index) {
    return make_node<postfix_expr>(span, std::move(object), std::move(index));
}

// object.member
expr_ptr make_member_access_expr(const source_span &span, expr_ptr object, std::string member) {
    return make_node<postfix_expr>(span, std::move(object), std::move(member));
}

// callee(arguments)
expr_ptr make_call_expr(const source_span &span, expr_ptr callee, arg_list arguments) {
    return make_node<postfix_expr>(span, std::move(callee), std::move(arguments));
}

// callee(state arguments)(ordinary arguments)
expr_ptr make_update_call_expr(const source_span &span, expr_ptr callee,
                               update_arguments arguments) {
    return make_node<postfix_expr>(span, std::move(callee), std::move(arguments));
}

expr_ptr make_literal(const source_span &span, literal_val v) {
    return make_node<literal>(span, std::move(v));
}

identifier_ptr make_identifier(const source_span &span, std::string l) {
    return make_node<identifier>(span, std::move(l));
}

expr_ptr make_assignment_expr(const source_span &span, expr_ptr lhs, expr_ptr rhs) {
    return make_node<assignment_expr>(span, std::move(lhs), std::move(rhs));
}

expr_ptr make_array_literal(const source_span &span, arg_list e) {
    return make_node<array_literal>(span, std::move(e));
}

expr_ptr make_struct_literal(const source_span &span, std::string st, std::vector<field_init> il) {
    return make_node<struct_literal>(span, std::move(st), std::move(il));
}

expr_ptr make_dynamic_alloc(const source_span &span, type_ptr type, expr_ptr size) {
    return make_node<dynamic_alloc>(span, std::move(type), std::move(size));
}

expr_ptr make_conversion_expr(const source_span &span, type_ptr type, expr_ptr expression) {
    return make_node<conversion_expr>(span, std::move(type), std::move(expression));
}

expr_ptr make_this_expr(const source_span &span) {
    return make_node<this_expr>(span);
}

// ---- Declarations ----

variable_decl_ptr make_variable_decl(const source_span &span, identifier_ptr n, type_ptr type,
                                     expr_ptr init, variable_storage s, bool imut) {
    return make_node<variable_decl>(span, std::move(n), std::move(type), std::move(init), s, imut);
}

// Ordinary function or method, with an explicit return type.
function_decl_ptr make_function_decl(const source_span &span, identifier_ptr n,
                                     std::vector<parameter> params, type_ptr result, block_ptr b) {
    return make_node<function_decl>(span, std::move(n), std::move(params), std::move(result),
                                    std::move(b));
}

// Free update function, with ordinary and state parameter lists.
function_decl_ptr make_update_function_decl(const source_span &span, identifier_ptr n,
                                            std::vector<parameter> params,
                                            std::vector<parameter> state, block_ptr b) {
    return make_node<function_decl>(span, std::move(n), std::move(params), std::move(state),
                                    std::move(b));
}

// Update method: its enclosing impl supplies the implicit state receiver.
function_decl_ptr make_update_method_decl(const source_span &span, identifier_ptr n,
                                          std::vector<parameter> params, block_ptr b) {
    return make_node<function_decl>(span, std::move(n), std::move(params), std::move(b));
}

decl_ptr make_struct_decl(const source_span &span, identifier_ptr n, std::vector<field_decl> f) {
    return make_node<struct_decl>(span, std::move(n), std::move(f));
}

decl_ptr make_impl_decl(const source_span &span, identifier_ptr n,
                        std::vector<function_decl_ptr> m) {
    return make_node<impl_decl>(span, std::move(n), std::move(m));
}

decl_ptr make_invariant_decl(const source_span &span, expr_ptr c) {
    return make_node<invariant_decl>(span, std::move(c));
}

program_ptr make_program(std::vector<decl_ptr> d) {
    return std::make_unique<program>(std::move(d));
}

// ---- Statements ----

block_ptr make_block_stmt(const source_span &span, std::vector<stmt_ptr> s) {
    return make_node<block_stmt>(span, std::move(s));
}

stmt_ptr make_expr_stmt(const source_span &span, expr_ptr e) {
    return make_node<expr_stmt>(span, std::move(e));
}

stmt_ptr make_decl_stmt(const source_span &span, decl_ptr d) {
    return make_node<decl_stmt>(span, std::move(d));
}

stmt_ptr make_return_stmt(const source_span &span, expr_ptr v) {
    return make_node<return_stmt>(span, std::move(v));
}

// First branch is if; remaining branches are elif, in order.
stmt_ptr make_if_stmt(const source_span &span, std::vector<conditional_branch> b,
                      block_ptr otherwise) {
    return make_node<if_stmt>(span, std::move(b), std::move(otherwise));
}

stmt_ptr make_while_stmt(const source_span &span, expr_ptr c, block_ptr b) {
    return make_node<while_stmt>(span, std::move(c), std::move(b));
}

stmt_ptr make_for_stmt(const source_span &span, variable_decl_ptr init, expr_ptr test,
                       expr_ptr next, block_ptr b) {
    return make_node<for_stmt>(span, std::move(init), std::move(test), std::move(next),
                               std::move(b));
}

stmt_ptr make_step_stmt(const source_span &span, block_ptr b, block_ptr rejection) {
    return make_node<step_stmt>(span, std::move(b), std::move(rejection));
}

// ---- Types ----

type_ptr make_primitive_type(const source_span &span, primitive_type t) {
    return make_node<primitive_type_syntax>(span, t);
}

type_ptr make_named_type(const source_span &span, std::string n) {
    return make_node<named_type_syntax>(span, std::move(n));
}

type_ptr make_pointer_type(const source_span &span, type_ptr p) {
    return make_node<pointer_type_syntax>(span, std::move(p));
}

type_ptr make_array_type(const source_span &span, type_ptr e, expr_ptr s) {
    return make_node<array_type_syntax>(span, std::move(e), std::move(s));
}
