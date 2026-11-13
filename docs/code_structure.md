# Code Structure

The JAQR compiler is a classic multi-pass compiler written in C++20. Each pass
lives in its own folder under `src/` and hands a single data structure to the next.

## 1. Pipeline

```text
 source.jaqr
     │  main.cpp reads the file into one vector<char> per line (each keeps its '\n')
     ▼
┌──────────────┐  vector<token>     src/tokeniser/   DFA, maximal munch
│  Tokeniser   │──────────────────▶                  errors: LEX001
└──────────────┘
     ▼
┌──────────────┐  program (AST)     src/parser/      recursive descent, 1-token lookahead
│    Parser    │──────────────────▶ src/ast/         errors: SYN001
└──────────────┘
     ▼
┌──────────────┐  checked AST       src/semantic/    scopes, types, Step rules
│   Semantic   │──────────────────▶ src/symbol_table/ errors: SEM001–SEM025
│   analysis   │
└──────────────┘
     ▼                                               ── code generation API ──
┌──────────────┐  builtin_calls     src/semantic/builtins.*   print / scan binding
│   Built-in   │
│   resolver   │
└──────────────┘
     ▼
┌──────────────┐  optimization_plan src/optimizer/   folding, dead code, unrolling
│  Optimiser   │
└──────────────┘
     ▼
┌──────────────┐  vector<string>    src/code_generator/  emits C++20
│ Code         │──────────────────▶ generated.cpp + runtime/jaqr/runtime/io.h
│ generator    │
└──────────────┘
```

`src/main.cpp` runs **tokenise → parse → analyse** on the file given on the command
line. The built-in resolver, optimiser and code generator are called through
`code_generator::gen_code(program, options)`, which `gen_code` runs in that order.

## 2. Folder map

```text
jaqr/
├── CMakeLists.txt          builds every src/**/*.cpp into `jaqr`; defines CTest tests
├── CMakePresets.json       debug / release presets (Ninja, build/<preset>/)
├── justfile                build, run, test, format, lint, clean shortcuts
├── .clang-format / .clang-tidy / .clangd
├── .github/workflows/checks.yml   CI: format-check + clang-tidy on pull requests
├── runtime/jaqr/runtime/io.h      runtime header included by generated C++
├── docs/                   this documentation
├── tests/                  unit, integration and generated-code tests
└── src/
    ├── main.cpp            command-line driver
    ├── tokens/             token type enum + token struct
    ├── tokeniser/          DFA graph and tokenise()
    ├── parser/             Parser class and parse()
    ├── ast/                AST node types, builders, grammar rules
    ├── visitor/            abstract visitor interface over the AST
    ├── symbol_table/       generic scoped symbol table
    ├── semantic/           semantic analyser + built-in resolver
    ├── optimizer/          optimisation plan builder
    └── code_generator/     C++ emitter
```

## 3. Modules

### 3.1 `main.cpp` — driver

- Prints the banner, checks there is exactly one argument and opens the file.
- Splits the file into lines, keeping `'\n'` so tokens such as strings can span lines.
- Calls `parse(tokenise(data))`, then `analyse(*root, filename)`.
- Catches `semantic_error` (prints it as-is) and any other `std::exception`
  (prints `Error: <file>: <message>`), returning exit code `1`.

### 3.2 `tokens/` and `tokeniser/` — lexical analysis

| File | Role |
|---|---|
| `tokens/token.h` | `enum class token_type` (keywords, operators, literals, punctuation, `WHITESPACE`, `COMMENT`) and `struct token { type, lexeme, line, column }` |
| `tokeniser/graph_node.h`, `edge.h` | DFA node (optional accepting `token_type` + outgoing edges) and edge (character predicate → target node) |
| `tokeniser/dfa.cpp` | The whole DFA as `const graph_node` objects, built at startup: identifiers, keyword prefix chains, numbers (decimal, `2b`, `16b`, float), char/string literals with escapes, comments, operators, punctuation |
| `tokeniser/tokeniser.cpp` | `tokenise()`: from each position, follow edges until none match; emit the token if the state is accepting, otherwise throw `LEX001` |

Keywords are recognised inside the DFA itself: there is one node per keyword
prefix, and any other identifier character falls back to the identifier node.

### 3.3 `parser/` — syntax analysis

`parser.cpp` contains a `Parser` class with one method per grammar rule
(`parse_program`, `parse_statement`, `parse_logical_or_expression`, …), mirroring the
EBNF one-to-one. It:

- Skips `WHITESPACE` and `COMMENT` tokens (`skip_trivia`).
- Uses one token of lookahead, plus the lexeme check that tells `fn main()` from `fn f()`.
- Splits `<-` into `<` `-` when it appears directly after an operand (`x<-1`).
- Records a `source_span` on every node for diagnostics.
- Throws `SYN001: unexpected token ...` on the first error.

The grammar it implements is written out in `src/ast/grammar_rules/*.txt` and
in [`grammar.md`](grammar.md).

### 3.4 `ast/` — abstract syntax tree

Nodes are owned through `std::unique_ptr` and every node has an `accept(visitor&)`.

| File | Nodes |
|---|---|
| `declaration.h` | `program`, `variable_decl` (`ORDINARY` / `STATE`, `immutable`), `function_decl` (`ORDINARY` / `UPDATE`, state + normal params), `struct_decl`, `impl_decl`, `invariant_decl` |
| `statement.h` | `block_stmt`, `expr_stmt`, `decl_stmt`, `return_stmt`, `if_stmt` (with `elif` branches), `while_stmt`, `for_stmt`, `step_stmt` (body + optional reject block) |
| `expression.h` | `binary_expr`, `unary_expr`, `postfix_expr` (`SUBSCRIPT`, `MEMBER_ACCESS`, `FUNCT_CALL`, `UPDATE_CALL`), `literal`, `identifier`, `assignment_expr`, `array_literal`, `struct_literal`, `dynamic_alloc`, `conversion_expr`, `this_expr` |
| `type.h` | `primitive_type_syntax`, `named_type_syntax`, `pointer_type_syntax`, `array_type_syntax` |
| `source.h` | `source_span` (start/end line and column, zero-indexed) |
| `ast_builder.h/.cpp` | `make_*` factory functions the parser uses to build nodes |
| `ast_root.h` | `ast_root` = `program` alias |
| `grammar_rules/` | EBNF for programs, statements and expressions |

### 3.5 `visitor/` — traversal

`visitor.h` declares a pure virtual `visit` overload for every AST node type.
The semantic analyser and the code generator are both visitors, so adding a node
type forces every pass to handle it at compile time.

### 3.6 `symbol_table/` — scopes

`symbol_table<Variable, Structure, Function>` is a header-only template with:

- A stack of variable scopes (index 0 holds globals).
- Separate maps for structs, functions and per-struct methods.
- An RAII `scope_guard`: entering a block returns a guard that pops the scope when
  it goes out of scope, so scopes cannot leak on early returns or exceptions.

### 3.7 `semantic/` — semantic analysis

| File | Role |
|---|---|
| `semantic.h/.cpp` | `analyse(program, filename)`: a visitor that resolves names, computes a `value_type` for every expression, and enforces the rules in [`semantic_spec.md`](semantic_spec.md) |
| `builtins.h/.cpp` | `resolve_builtins(program)`: binds `print` / `scan` calls by lexical scope, checks arity and argument types, and rejects I/O inside steps, update functions and invariants (directly or through called helpers) |

`analyse` throws a `semantic_error` carrying the filename, a stable code such as
`SEM017`, the `source_span` and a message. Checks include:

- Declaration before use, no duplicates, no shadowing (`SEM001`, `SEM002`).
- Type rules, implicit `int ↔ float` only, exact types at call/return (`SEM003`–`SEM007`).
- All paths of a non-void function return (`SEM008`).
- `imut` initialisation and immutability (`SEM011`, `SEM012`).
- Array lengths/initialisers/indexes and pointer rules (`SEM013`–`SEM016`).
- State writes only in steps / update functions, update calls only in an active
  step, no nested steps, pure invariants, no early exit from a step
  (`SEM017`–`SEM020`, `SEM023`).
- Exactly one `fn main()` (`SEM024`).

### 3.8 `optimizer/` — optimisation plan

`optimize(program, options)` does **not** modify the AST. It returns an
`optimization_plan`:

- `constants`: expressions folded to a known literal value.
- `omitted`: statements to skip (unreachable code, dead stores, false loops).
- `unrolled`: `for` loops to emit unrolled, with their trip count.

Keeping the AST intact preserves the node identities that invariant and impl
indexes point to. Details: [`optimisation.md`](optimisation.md).

### 3.9 `code_generator/` — C++ emission

`code_generator` is a visitor that writes C++20 lines into a `vector<string>`:

1. Runs `resolve_builtins`, then `optimize` (unless `options.enabled` is false).
2. Emits headers, globals/state, structs, invariants as `bool` lambdas, functions
   and `main`.
3. Lowers each `step` into a transaction: copy every touched state variable,
   run the body on the copies, evaluate the invariants, then commit the copies
   only if all pass; otherwise run the `reject` block.
4. Emits `print` / `scan` as `jaqr_runtime::print` / `jaqr_runtime::scan`, and
   includes `<jaqr/runtime/io.h>` only when they are used.

### 3.10 `runtime/` — support library

`runtime/jaqr/runtime/io.h` defines `jaqr_runtime::print` (`std::cout << value`)
and `jaqr_runtime::scan` (`std::cin >> value`, returns success as `bool`).
See [`io.md`](io.md).

## 4. Tests

| File | Type | What it covers |
|---|---|---|
| `tests/tokeniser_tests.cpp` | GoogleTest | Every token kind, literal edge cases, `LEX` errors |
| `tests/parser_tests.cpp` | GoogleTest | Every grammar rule, precedence, literal decoding, `SYN` errors |
| `tests/tokeniser_parser_integration_tests.cpp` | GoogleTest | Real source text through both stages, AST dumps, error positions |
| `tests/ast_dump.*`, `tests/test_helpers.h` | Helpers | S-expression AST printer and token builders |
| `tests/optimizer.cpp` + `optimizer_test.py` | CTest | Generates C++ with and without optimisation, compiles at `-O0`/`-O2`, compares behaviour |
| `tests/io.cpp` + `io_test.py` | CTest | Generated I/O with piped input, EOF, failures, shadowing, rejected contexts |

The GoogleTest suites are configured in `tests/CMakeLists.txt`, which fetches
GoogleTest 1.15.2 and builds the front end as a static library `jaqr_front_end`.

## 5. Build system

- `CMakeLists.txt` uses `file(GLOB_RECURSE ... CONFIGURE_DEPENDS src/*.cpp)`, so new
  `.cpp` files under `src/` are picked up on the next build without editing CMake.
- `CMakePresets.json` defines `debug` and `release` with the Ninja generator and
  exports `compile_commands.json` for clangd and clang-tidy.
- CI (`.github/workflows/checks.yml`) runs `just format-check` and `just lint` on
  every pull request.

## 6. Adding a language feature

1. Add tokens to `tokens/token.h` and DFA nodes to `tokeniser/dfa.cpp`.
2. Update the rule in `ast/grammar_rules/*.txt`.
3. Add or change AST nodes in `ast/` and a `make_*` builder.
4. Parse it in `parser/parser.cpp`.
5. Add a `visit` overload to `visitor/visitor.h`; the compiler then lists every
   pass that must handle it.
6. Type-check it in `semantic/semantic.cpp`.
7. Emit it in `code_generator/code_generator.cpp`.
8. Add tests in `tests/`.
