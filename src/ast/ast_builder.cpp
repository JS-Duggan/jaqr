
#include "ast_builder.h"
#include "ast_root.h"
#include "declaration.h"
#include "expression.h"
#include "expression_base.h"
#include "statement.h"
#include "statement_base.h"
#include "type.h"

// TODO: I include these in the header so i dont need to include them here as well? I forgot if
// including it is idomatic or not
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ---- Root ----

ast_root_ptr make_ast_root(std::vector<decl_ptr> decls) {
    return std::make_unique<ast_root>(std::move(decls));
}

// ---- Expressions ----

expr_ptr make_binary_expr(binary_op o, expr_ptr lhs, expr_ptr rhs) {
    return std::make_unique<binary_expr>(o, std::move(lhs), std::move(rhs));
}

expr_ptr make_unary_expr(unary_op o, expr_ptr rhs) {
    return std::make_unique<unary_expr>(o, std::move(rhs));
}

// object[index]
expr_ptr make_subscript_expr(expr_ptr object, expr_ptr index) {
    return std::make_unique<postfix_expr>(std::move(object), std::move(index));
}

// object.member
expr_ptr make_member_access_expr(expr_ptr object, std::string member) {
    return std::make_unique<postfix_expr>(std::move(object), std::move(member));
}

// callee(arguments)
expr_ptr make_call_expr(expr_ptr callee, arg_list arguments) {
    return std::make_unique<postfix_expr>(std::move(callee), std::move(arguments));
}

// callee(ordinary arguments)(state arguments)
expr_ptr make_update_call_expr(expr_ptr callee, update_arguments arguments) {
    return std::make_unique<postfix_expr>(std::move(callee), std::move(arguments));
}

expr_ptr make_literal(literal_val v) {
    return std::make_unique<literal>(v);
}

expr_ptr make_variable(std::string l) {
    return std::make_unique<variable>(l);
}

expr_ptr make_assignment_expr(expr_ptr lhs, expr_ptr rhs) {
    return std::make_unique<assignment_expr>(std::move(lhs), std::move(rhs));
}

expr_ptr make_array_literal(arg_list e) {
    return std::make_unique<array_literal>(std::move(e));
}

expr_ptr make_struct_literal(std::string st, std::vector<field_init> il) {
    return std::make_unique<struct_literal>(std::move(st), std::move(il));
}

expr_ptr make_dynamic_alloc(type_ptr type, expr_ptr size) {
    return std::make_unique<dynamic_alloc>(std::move(type), std::move(size));
}

expr_ptr make_conversion_expr(type_ptr type, expr_ptr expression) {
    return std::make_unique<conversion_expr>(std::move(type), std::move(expression));
}

expr_ptr make_this_expr() {
    return std::make_unique<this_expr>();
}

// ---- Declarations ----

variable_decl_ptr make_variable_decl(std::string n, type_ptr type, expr_ptr init,
                                     variable_storage s, bool imut) {
    return std::make_unique<variable_decl>(std::move(n), std::move(type), std::move(init), s, imut);
}

// Ordinary function or method, with an explicit return type.
function_decl_ptr make_function_decl(std::string n, std::vector<parameter> params, type_ptr result,
                                     block_ptr b) {
    return std::make_unique<function_decl>(std::move(n), std::move(params), std::move(result),
                                           std::move(b));
}

// Free update function, with ordinary and state parameter lists.
function_decl_ptr make_update_function_decl(std::string n, std::vector<parameter> params,
                                            std::vector<parameter> state, block_ptr b) {
    return std::make_unique<function_decl>(std::move(n), std::move(params), std::move(state),
                                           std::move(b));
}

// Update method: its enclosing impl supplies the implicit state receiver.
function_decl_ptr make_update_method_decl(std::string n, std::vector<parameter> params,
                                          block_ptr b) {
    return std::make_unique<function_decl>(std::move(n), std::move(params), std::move(b));
}

decl_ptr make_struct_decl(std::string n, std::vector<field_decl> f) {
    return std::make_unique<struct_decl>(std::move(n), std::move(f));
}

decl_ptr make_impl_decl(std::string n, std::vector<function_decl_ptr> m) {
    return std::make_unique<impl_decl>(std::move(n), std::move(m));
}

decl_ptr make_invariant_decl(expr_ptr c) {
    return std::make_unique<invariant_decl>(std::move(c));
}

program_ptr make_program(std::vector<decl_ptr> d) {
    return std::make_unique<program>(std::move(d));
}

// ---- Statements ----

block_ptr make_block_stmt(std::vector<stmt_ptr> s) {
    return std::make_unique<block_stmt>(std::move(s));
}

stmt_ptr make_expr_stmt(expr_ptr e) {
    return std::make_unique<expr_stmt>(std::move(e));
}

stmt_ptr make_decl_stmt(decl_ptr d) {
    return std::make_unique<decl_stmt>(std::move(d));
}

stmt_ptr make_return_stmt(expr_ptr v) {
    return std::make_unique<return_stmt>(std::move(v));
}

// First branch is if; remaining branches are elif, in order.
stmt_ptr make_if_stmt(std::vector<conditional_branch> b, block_ptr otherwise) {
    return std::make_unique<if_stmt>(std::move(b), std::move(otherwise));
}

stmt_ptr make_while_stmt(expr_ptr c, block_ptr b) {
    return std::make_unique<while_stmt>(std::move(c), std::move(b));
}

stmt_ptr make_for_stmt(variable_decl_ptr init, expr_ptr test, expr_ptr next, block_ptr b) {
    return std::make_unique<for_stmt>(std::move(init), std::move(test), std::move(next),
                                      std::move(b));
}

stmt_ptr make_step_stmt(block_ptr b, block_ptr rejection) {
    return std::make_unique<step_stmt>(std::move(b), std::move(rejection));
}

// ---- Types ----

type_ptr make_primitive_type(primitive_type t) {
    return std::make_unique<primitive_type_syntax>(t);
}

type_ptr make_named_type(std::string n) {
    return std::make_unique<named_type_syntax>(std::move(n));
}

type_ptr make_pointer_type(type_ptr p) {
    return std::make_unique<pointer_type_syntax>(std::move(p));
}

type_ptr make_array_type(type_ptr e, expr_ptr s) {
    return std::make_unique<array_type_syntax>(std::move(e), std::move(s));
}
