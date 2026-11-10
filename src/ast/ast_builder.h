#pragma once

#include "declaration.h"
#include "expression.h"
#include "expression_base.h"
#include "statement.h"
#include "statement_base.h"
#include "type.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// ---- Expressions ----

expr_ptr make_binary_expr(binary_op o, expr_ptr lhs, expr_ptr rhs);

expr_ptr make_unary_expr(unary_op o, expr_ptr rhs);

// object[index]
expr_ptr make_subscript_expr(expr_ptr object, expr_ptr index);
// object.member
expr_ptr make_member_access_expr(expr_ptr object, std::string member);

// callee(arguments)
expr_ptr make_call_expr(expr_ptr callee, arg_list arguments);

// callee(ordinary arguments)(state arguments)
expr_ptr make_update_call_expr(expr_ptr callee, update_arguments arguments);

expr_ptr make_literal(literal_val v);

expr_ptr make_variable(std::string l);

expr_ptr make_assignment_expr(expr_ptr lhs, expr_ptr rhs);

expr_ptr make_array_literal(arg_list e);

expr_ptr make_struct_literal(std::string st, std::vector<field_init> il);
expr_ptr make_dynamic_alloc(type_ptr type, expr_ptr size);
expr_ptr make_conversion_expr(type_ptr type, expr_ptr expression);

expr_ptr make_this_expr();

// ---- Declarations ----

// Returns the concrete type so for_stmt can take it; converts to decl_ptr implicitly.
std::unique_ptr<variable_decl> make_variable_decl(std::string n, type_ptr type,
                                                  expr_ptr init = nullptr,
                                                  variable_storage s = variable_storage::ORDINARY,
                                                  bool imut = false);

// Ordinary function or method, with an explicit return type.
// Returns the concrete type so impl_decl can take it; converts to decl_ptr implicitly.
std::unique_ptr<function_decl> make_function_decl(std::string n, std::vector<parameter> params,
                                                  type_ptr result, block_ptr b);

// Free update function, with ordinary and state parameter lists.
std::unique_ptr<function_decl> make_update_function_decl(std::string n,
                                                         std::vector<parameter> params,
                                                         std::vector<parameter> state,
                                                         block_ptr b);

// Update method: its enclosing impl supplies the implicit state receiver.
std::unique_ptr<function_decl> make_update_method_decl(std::string n,
                                                       std::vector<parameter> params, block_ptr b);

decl_ptr make_struct_decl(std::string n, std::vector<field_decl> f);

decl_ptr make_impl_decl(std::string n, std::vector<std::unique_ptr<function_decl>> m);

decl_ptr make_invariant_decl(expr_ptr c);

std::unique_ptr<program> make_program(std::vector<decl_ptr> d = {});

// ---- Statements ----

block_ptr make_block_stmt(std::vector<stmt_ptr> s = {});

stmt_ptr make_expr_stmt(expr_ptr e);

stmt_ptr make_decl_stmt(decl_ptr d);

stmt_ptr make_return_stmt(expr_ptr v = nullptr);

// First branch is if; remaining branches are elif, in order.
stmt_ptr make_if_stmt(std::vector<conditional_branch> b, block_ptr otherwise = nullptr);

stmt_ptr make_while_stmt(expr_ptr c, block_ptr b);

stmt_ptr make_for_stmt(std::unique_ptr<variable_decl> init, expr_ptr test, expr_ptr next,
                       block_ptr b);

stmt_ptr make_step_stmt(block_ptr b, block_ptr rejection = nullptr);

// ---- Types ----

type_ptr make_primitive_type(primitive_type t);

type_ptr make_named_type(std::string n);

type_ptr make_pointer_type(type_ptr p);

type_ptr make_array_type(type_ptr e, expr_ptr s);
