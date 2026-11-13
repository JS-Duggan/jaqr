#pragma once

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
#include <vector>

// Builders centralise node construction and source-span attachment, leaving the parser
// to describe grammar rules and transfer ownership of their children.

// ---- Root ----
ast_root_ptr make_ast_root(std::vector<decl_ptr> decls = {});

// ---- Expressions ----

expr_ptr make_binary_expr(const source_span &span, binary_op o, expr_ptr lhs, expr_ptr rhs);

expr_ptr make_unary_expr(const source_span &span, unary_op o, expr_ptr rhs);

// object[index]
expr_ptr make_subscript_expr(const source_span &span, expr_ptr object, expr_ptr index);
// object.member
expr_ptr make_member_access_expr(const source_span &span, expr_ptr object, std::string member);

// callee(arguments)
expr_ptr make_call_expr(const source_span &span, expr_ptr callee, arg_list arguments);

// callee(state arguments)(ordinary arguments)
expr_ptr make_update_call_expr(const source_span &span, expr_ptr callee,
                               update_arguments arguments);

expr_ptr make_literal(const source_span &span, literal_val v);

identifier_ptr make_identifier(const source_span &span, std::string l);

expr_ptr make_assignment_expr(const source_span &span, expr_ptr lhs, expr_ptr rhs);

expr_ptr make_array_literal(const source_span &span, arg_list e);

expr_ptr make_struct_literal(const source_span &span, std::string st, std::vector<field_init> il);
expr_ptr make_dynamic_alloc(const source_span &span, type_ptr type, expr_ptr size);
expr_ptr make_conversion_expr(const source_span &span, type_ptr type, expr_ptr expression);

expr_ptr make_this_expr(const source_span &span);

// ---- Declarations ----

// Returns the concrete type so for_stmt can take it; converts to decl_ptr implicitly.
variable_decl_ptr make_variable_decl(const source_span &span, identifier_ptr n, type_ptr type,
                                     expr_ptr init = nullptr,
                                     variable_storage s = variable_storage::ORDINARY,
                                     bool imut = false);

// Ordinary function or method, with an explicit return type.
// Returns the concrete type so impl_decl can take it; converts to decl_ptr implicitly.
function_decl_ptr make_function_decl(const source_span &span, identifier_ptr n,
                                     std::vector<parameter> params, type_ptr result, block_ptr b);

// Free update function, with ordinary and state parameter lists.
function_decl_ptr make_update_function_decl(const source_span &span, identifier_ptr n,
                                            std::vector<parameter> params,
                                            std::vector<parameter> state, block_ptr b);

// Update method: its enclosing impl supplies the implicit state receiver.
function_decl_ptr make_update_method_decl(const source_span &span, identifier_ptr n,
                                          std::vector<parameter> params, block_ptr b);

decl_ptr make_struct_decl(const source_span &span, identifier_ptr n, std::vector<field_decl> f);

decl_ptr make_impl_decl(const source_span &span, identifier_ptr n,
                        std::vector<function_decl_ptr> m);

decl_ptr make_invariant_decl(const source_span &span, expr_ptr c);

program_ptr make_program(std::vector<decl_ptr> d = {});

// ---- Statements ----

block_ptr make_block_stmt(const source_span &span, std::vector<stmt_ptr> s = {});

stmt_ptr make_expr_stmt(const source_span &span, expr_ptr e);

stmt_ptr make_decl_stmt(const source_span &span, decl_ptr d);

stmt_ptr make_return_stmt(const source_span &span, expr_ptr v = nullptr);

// First branch is if; remaining branches are elif, in order.
stmt_ptr make_if_stmt(const source_span &span, std::vector<conditional_branch> b,
                      block_ptr otherwise = nullptr);

stmt_ptr make_while_stmt(const source_span &span, expr_ptr c, block_ptr b);

stmt_ptr make_for_stmt(const source_span &span, variable_decl_ptr init, expr_ptr test,
                       expr_ptr next, block_ptr b);

stmt_ptr make_step_stmt(const source_span &span, block_ptr b, block_ptr rejection = nullptr);

// ---- Types ----

type_ptr make_primitive_type(const source_span &span, primitive_type t);

type_ptr make_named_type(const source_span &span, std::string n);

type_ptr make_pointer_type(const source_span &span, type_ptr p);

type_ptr make_array_type(const source_span &span, type_ptr e, expr_ptr s);
