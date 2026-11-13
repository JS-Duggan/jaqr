# Code-generation optimisation

`code_generator::gen_code(program, options)` builds a structured optimisation plan
before emitting C++. It leaves the input AST unchanged, preserving the declaration
identities used by invariants and impl indexes. Pass a semantically validated AST;
this pass does not replace type checking or diagnostics for invalid source.

```cpp
auto optimized = generator.gen_code(ast);
auto original = generator.gen_code(ast, {.enabled = false});
auto bounded = generator.gen_code(ast, {
    .enabled = true,
    .max_unroll_iterations = 4,
    .max_unrolled_statements = 32,
});
```

The current command-line `main` only reads a source file; these options belong to
the generation API, ready for the parser/semantic-analysis pipeline to call.

## Implemented passes

- Fold integer addition, subtraction, multiplication and comparisons, plus boolean
  operators and integer negation, when their results are known and in range.
  Preserve short-circuit evaluation and the 32-bit type of folded integer results.
- Omit unreachable statements after returns (including conditionals whose every
  reachable path returns). Simplify constant branches while retaining block scopes.
- Remove constant-false while loops. For a constant-false for loop, retain its
  initializer in a scope, including any side effects.
- Remove unused integer locals with constant initializers and dead constant stores
  in simple blocks. Declarations must precede execution. Calls, address-taking,
  complex control flow and invariant declarations prevent this analysis. Surviving
  assignments retain their variable declarations. State/global stores are retained.
- Unroll small integer for loops with literal/folded bounds and a constant additive
  increment. The body may contain only simple identifier assignments with no calls,
  pointer accesses or changes to the induction variable. Preserve the initializer,
  each body scope, and each increment. Check the final increment for overflow too.
  Defaults: at most eight iterations and 64 expanded body/increment statements.
  A zero iteration limit disables positive-trip unrolling.

## Deliberately retained

Floating-point arithmetic, division, shifts and uncertain expressions stay at
runtime. Unknown calls are not assumed pure. Transaction bodies and rejection
handlers remain untouched; invariant validation and commits remain at the step
boundary. Copying only modified state, interprocedural effects/alias analysis,
constant propagation across variables, unused-function removal and general
control-flow liveness are future work.

Unrolling can enlarge executables and is not a promise of faster execution. Compare
runtime and binary size with optimisation disabled on representative workloads.
The generated C++ can independently be compiled with, for example:

```sh
c++ -std=c++20 -O2 generated.cpp -o generated
```

Building JAQR in release mode optimises JAQR itself, not subsequent generated files.

## Verification

Run `just test`. The regression fixture checks the optimisation plan, generates C++
with optimisation enabled and disabled, and compiles/runs both versions at `-O0`
and `-O2`. Checks include side effects, scopes, loop bounds/budgets, overflow,
constant folding, dead assignments, and step commit/rejection behaviour.
