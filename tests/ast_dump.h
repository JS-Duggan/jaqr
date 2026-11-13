#pragma once

#include "../src/ast/declaration.h"
#include "../src/ast/expression_base.h"
#include "../src/ast/statement_base.h"
#include "../src/ast/type.h"

#include <string>

// Writes an AST as a one-line s-expression, so a test can compare a whole tree with one string.
// Source spans are left out.
//
// declarations
//   (program DECL...)
//   (let NAME TYPE [INIT])   also: state, imut, imut-state
//   (fn NAME (PARAM...) RETURN_TYPE BLOCK)          PARAM is (NAME TYPE); main returns void
//   (update-fn NAME (STATE_PARAM...) (PARAM...) BLOCK)   update methods have no state params
//   (struct NAME (FIELD TYPE)...)
//   (impl NAME METHOD...)
//   (inv EXPR)
//
// statements
//   (block STMT...)
//   (expr EXPR)
//   (return [EXPR])
//   (if (COND BLOCK)... [(else BLOCK)])     first pair is if, the rest are elif
//   (while COND BLOCK)
//   (for INIT COND INCREMENT BLOCK)
//   (step BLOCK [(reject BLOCK)])
//   a declaration statement is written as the declaration itself
//
// expressions
//   (OP LEFT RIGHT)     OP is the source operator: + - * / \ ~ ^ & | && || == != < > <= >= << >>
//   (OP OPERAND)        unary: - ! @ <-
//   (= TARGET VALUE)
//   (index OBJECT INDEX)
//   (. OBJECT MEMBER)
//   (call CALLEE ARG...)
//   (update-call CALLEE (STATE_ARG...) (ARG...))
//   (array ELEMENT...)
//   (struct-init NAME (FIELD EXPR)...)
//   (new TYPE COUNT)
//   (convert TYPE EXPR)
//   this
//   NAME                an identifier
//   int:5  float:1.5  byte:255  bool:true  char:'a'  str:"hi"
//       char and string values are the parsed value, with \n \t \r \0 \\ and the quote escaped
//
// types
//   int float char bool byte void
//   NAME                a struct type
//   (-> TYPE)           pointer
//   ([] SIZE TYPE)      array
std::string dump(const program &node);
std::string dump(const decl &node);
std::string dump(const stmt &node);
std::string dump(const expr &node);
std::string dump(const type_syntax &node);
