# JAQR (Step) Grammar

This is the complete grammar accepted by the parser. The source of truth is
`src/ast/grammar_rules/` (`program.txt`, `statement.txt`, `expression.txt`).

## Notation

The grammar uses EBNF:

| Notation | Meaning |
|---|---|
| `a = b ;` | Rule `a` is defined as `b` |
| `"x"` | Literal token |
| `UPPER_CASE` | Token produced by the tokeniser |
| `a b` | `a` followed by `b` |
| `a \| b` | `a` or `b` |
| `[ a ]` | Optional `a` (zero or one) |
| `{ a }` | Zero or more `a` |
| `( a )` | Grouping |
| `(* ... *)` | Comment |

The parser is recursive descent with one token of lookahead.

---

## 1. Lexical grammar (tokens)

The tokeniser is a hand-built DFA using **maximal munch with no backtracking**.
If it stops in a non-accepting state, that is an error: `16b` is a `LEX` error, not
`16` followed by `b`.

### 1.1 Whitespace and comments

```ebnf
WHITESPACE = ( " " | "\t" | "\n" | "\r" ) { " " | "\t" | "\n" | "\r" } ;
COMMENT    = "#" { any character except "\n" } ;
```

Whitespace and comments are skipped by the parser. A `#` inside a string literal is
not a comment.

### 1.2 Identifiers and keywords

```ebnf
IDENTIFIER = ( letter | "_" ) { letter | digit | "_" } ;
```

Reserved keywords (cannot be identifiers):

```text
int  float  char  bool  byte
let  imut  state  define
fn  update  struct  impl  this  new
if  elif  else  while  for  return
step  reject  inv
true  false
```

`main` is an `IDENTIFIER` but is reserved for the main function. It is rejected
everywhere else. In the rules below, `IDENT` means an `IDENTIFIER` whose lexeme is
not `main`.

`print` and `scan` are **not** keywords. They are ordinary identifiers bound to
built-in functions and can be shadowed.

### 1.3 Literals

```ebnf
INT_LITERAL     = digit { digit } ;                       (* 42 *)
BASE_2_LITERAL  = "2b" binDigit { binDigit } ;            (* 2b1010 = 10 *)
BASE_16_LITERAL = "16b" hexDigit { hexDigit } ;           (* 16bff  = 255 *)
FLOAT_LITERAL   = digit { digit } "." { digit }           (* 1.5  1. *)
                | "." digit { digit } ;                   (* .5 *)
CHAR_LITERAL    = "'" ( char | escape ) "'" ;             (* 'a'  '\n' *)
STRING_LITERAL  = '"' { char | escape } '"' ;             (* "hi\n" *)
BOOL_LITERAL    = "true" | "false" ;

escape   = "\" ( "n" | "r" | "t" | "0" | "\" | "'" | '"' ) ;
binDigit = "0" | "1" ;
hexDigit = digit | "a".."f" | "A".."F" ;
```

Rules:

- A letter or digit directly after a number is an error: `123abc`, `2b102`, `16bfg`.
- Exception: after a trailing `.`, so `5.foo` is `FLOAT_LITERAL 5.` then `IDENTIFIER foo`.
- A second `.` is an error: `1.2.3`.
- A char literal holds exactly one character or escape. `''` is an error.
- String literals may span lines.

### 1.4 Operators and punctuation

| Token | Lexeme | Token | Lexeme |
|---|---|---|---|
| `PLUS` | `+` | `EQUAL` | `==` |
| `MINUS` | `-` | `NOT_EQUAL` | `!=` |
| `MULTIPLY` | `*` | `LESSER` | `<` |
| `DIVIDE` | `/` | `GREATER` | `>` |
| `DIVIDE_FLOOR` | `\` | `LESS_OR_EQUAL` | `<=` |
| `EXPONENT` | `~` | `GREATER_OR_EQUAL` | `>=` |
| `BIT_AND` | `&` | `LEFT_SHIFT` | `<<` |
| `BIT_OR` | `\|` | `RIGHT_SHIFT` | `>>` |
| `XOR` | `^` | `ASSIGN` | `=` |
| `LOGICAL_AND` | `&&` | `ADDRESS` | `@` |
| `LOGICAL_OR` | `\|\|` | `DEREFERENCE` | `<-` |
| `NOT` | `!` | `POINTER` | `->` |
| `ACCESS` | `.` | | |

Punctuation: `{ } ( ) [ ] ; : ,`

There is no `%` (modulo), `++`, `--`, `+=` or `->` member access.

---

## 2. Program structure

```ebnf
program
    = { topLevelDeclaration } EOF ;

topLevelDeclaration
    = globalVariableDeclaration
    | functionDeclaration
    | updateFunctionDeclaration
    | structDeclaration
    | implDeclaration
    | invariantDeclaration ;

globalVariableDeclaration
    = "define" variableDeclaration ";" ;

variableDeclaration
    = "let" binding [ "=" expression ]
    | "state" binding "=" expression
    | "imut" [ "state" ] binding "=" expression ;

binding
    = IDENT ":" type ;
```

## 3. Types

Types read left to right.

```ebnf
type
    = "->" type                (* pointer *)
    | arrayDimension type      (* fixed-size array *)
    | typeName ;

typeName
    = primitiveType
    | IDENT ;                  (* struct name *)

primitiveType
    = "int" | "float" | "char" | "bool" | "byte" ;

arrayDimension
    = "[" expression "]" ;
```

| Type | Meaning |
|---|---|
| `[3]int` | Array of 3 `int` |
| `[3]->int` | Array of 3 pointers to `int` |
| `->[3]int` | Pointer to an array of 3 `int` |
| `[2][4]float` | Array of 2 arrays of 4 `float` |

## 4. Functions, structs and invariants

```ebnf
(* exactly one main per program, checked in semantic analysis *)
functionDeclaration
    = "fn" ( mainSignature | functionSignature ) block ;

mainSignature
    = "main" "(" ")" ;

functionSignature
    = IDENT parameterClause ":" type ;

(* first clause = state parameters, second clause = non-state parameters *)
updateFunctionDeclaration
    = "update" "fn" IDENT parameterClause parameterClause block ;

parameterClause
    = "(" [ parameterList ] ")" ;

parameterList
    = binding { "," binding } ;

structDeclaration
    = "struct" IDENT "{" { fieldDeclaration } "}" ";" ;

fieldDeclaration
    = binding ";" ;

implDeclaration
    = "impl" IDENT "{" { methodDeclaration } "}" ";" ;

methodDeclaration
    = methodFunctionDeclaration
    | updateMethodDeclaration ;

methodFunctionDeclaration
    = "fn" functionSignature block ;          (* main not allowed *)

(* no state clause: the struct the method is called on is the state *)
updateMethodDeclaration
    = "update" "fn" IDENT parameterClause block ;

invariantDeclaration
    = "inv" "{" expression "}" ";" ;
```

Note: an ordinary function must declare a return type (`: type`). `main` and update
functions have no return type (they return `void`).

## 5. Statements

```ebnf
block
    = "{" { statement } "}" ;

statement
    = variableDeclaration ";"
    | expressionStatement
    | returnStatement
    | ifStatement
    | whileStatement
    | forStatement
    | stepStatement
    | invariantDeclaration
    | block ;

expressionStatement
    = expression ";" ;

returnStatement
    = "return" [ expression ] ";" ;

ifStatement
    = "if" "(" expression ")" block
      { "elif" "(" expression ")" block }
      [ "else" block ] ;

whileStatement
    = "while" "(" expression ")" block ;

forStatement
    = "for" "(" forInitializer ";" expression ";" expression ")" block ;

forInitializer
    = "let" binding "=" expression ;

stepStatement
    = "step" block [ "reject" block ] ;
```

Bodies of `if`, `elif`, `else`, `while`, `for` and `step` must be blocks (`{ ... }`).

## 6. Expressions

```ebnf
expression            = assignmentExpression ;

assignmentExpression  = logicalOrExpression [ "=" assignmentExpression ] ;   (* right assoc *)

logicalOrExpression   = logicalAndExpression { "||" logicalAndExpression } ;
logicalAndExpression  = equalityExpression { "&&" equalityExpression } ;
equalityExpression    = relationalExpression { ( "==" | "!=" ) relationalExpression } ;
relationalExpression  = bitwiseOrExpression { ( "<" | ">" | "<=" | ">=" ) bitwiseOrExpression } ;

bitwiseOrExpression   = bitwiseXorExpression { "|" bitwiseXorExpression } ;
bitwiseXorExpression  = bitwiseAndExpression { "^" bitwiseAndExpression } ;
bitwiseAndExpression  = shiftExpression { "&" shiftExpression } ;
shiftExpression       = additiveExpression { ( "<<" | ">>" ) additiveExpression } ;

additiveExpression        = multiplicativeExpression { ( "+" | "-" ) multiplicativeExpression } ;
multiplicativeExpression  = unaryExpression { ( "*" | "/" | "\" ) unaryExpression } ;

unaryExpression   = ( "-" | "!" ) unaryExpression
                  | powerExpression ;

powerExpression   = prefixExpression [ "~" unaryExpression ] ;               (* right assoc *)

prefixExpression  = ( "@" | "<-" ) prefixExpression
                  | postfixExpression ;

postfixExpression = primaryExpression { postfixSuffix } ;

postfixSuffix     = argumentClause                 (* call *)
                  | "[" expression "]"             (* index *)
                  | "." IDENT ;                    (* field / method *)

argumentClause    = "(" [ argumentList ] ")" ;
argumentList      = expression { "," expression } ;

primaryExpression = literal
                  | IDENT [ structInitializer ]
                  | "this"
                  | "(" expression ")"
                  | arrayLiteral
                  | conversionExpression
                  | allocationExpression ;

literal = INT_LITERAL | BASE_2_LITERAL | BASE_16_LITERAL | FLOAT_LITERAL
        | CHAR_LITERAL | STRING_LITERAL | BOOL_LITERAL ;

arrayLiteral          = "[" [ argumentList ] "]" ;
structInitializer     = "{" [ fieldInitializerList ] "}" ;
fieldInitializerList  = fieldInitializer { "," fieldInitializer } ;
fieldInitializer      = IDENT ":" expression ;

conversionExpression  = primitiveType "(" expression ")" ;      (* float(x) *)
allocationExpression  = "new" "(" type "," expression ")" ;     (* new(int, 4) *)
```

Two argument clauses in a row form one **update call**: `f(s)(x)` passes state
arguments `s` and non-state arguments `x`. An update **method** takes one clause,
so `p.m(x)` parses as an ordinary call.

## 7. Operator precedence

Highest binds tightest.

| Level | Operators | Associativity | Example |
|---|---|---|---|
| 1 | `f(...)` `a[i]` `a.b` | Left | `p.get()` |
| 2 | `@` (address-of), `<-` (dereference) | Right (prefix) | `<-p ~ 2` = `(<-p) ~ 2` |
| 3 | `~` (power) | Right | `2 ~ 3 ~ 2` = `2 ~ (3 ~ 2)` |
| 4 | `-` (negate), `!` | Right (prefix) | `-2 ~ 2` = `-(2 ~ 2)` = `-4` |
| 5 | `*` `/` `\` | Left | |
| 6 | `+` `-` | Left | |
| 7 | `<<` `>>` | Left | |
| 8 | `&` | Left | |
| 9 | `^` | Left | |
| 10 | `\|` | Left | |
| 11 | `<` `>` `<=` `>=` | Left | `x & 1 == 0` = `(x & 1) == 0` |
| 12 | `==` `!=` | Left | |
| 13 | `&&` | Left | |
| 14 | `\|\|` | Left | |
| 15 | `=` | Right | `a = b = 1` |

The exponent of `~` may be signed: `2 ~ -1`.

## 8. Disambiguation rules

| Input | Parsed as | Why |
|---|---|---|
| `x<-1` | `x < -1` | `<-` directly after an operand is split into `<` `-` |
| `<-p` | dereference `p` | `<-` at the start of an operand |
| `16b` | `LEX` error | Maximal munch, no backtracking |
| `5.foo` | `5.` then `foo` | Float literal ends at `.` |
| `fn main()` vs `fn f()` | One-token lookahead on the lexeme after `fn` | `main` is an `IDENTIFIER` checked by lexeme |
| `f(s)(x)` | Update call | Two consecutive argument clauses |
| `P{x: 1}` | Struct literal | `{` directly after an identifier in an expression |
