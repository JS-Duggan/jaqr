# Generated C++ names

The code generator prefixes user-defined identifiers with `jaqr_` at emission time:

- Variables, function/method names and ordinary/state parameters.
- Struct names, named type references, fields and designated initializers.
- References and calls, including invariant parameters and step copies/commits.

For example, source `class` becomes `jaqr_class`. A source name already beginning
with `jaqr_` receives the prefix too: `jaqr_class` becomes `jaqr_jaqr_class`.
This avoids merging two distinct source names. The AST, diagnostics and lookup
maps retain the original JAQR spelling; string literals are not changed.

The top-level entry point remains C++ `main`. A method called main is a regular
prefixed method. Method definitions use the same prefixed struct/method names as
their declarations, such as `jaqr_Record::jaqr_update`.

Built-in calls retain their `jaqr_runtime::print` / `jaqr_runtime::scan` names.
Compiler-generated step and invariant names use `jaqrInternal_`, which cannot
collide with the `jaqr_` prefix used for user names.

A few names need more than a literal prefix: leading underscores or double
underscores could still produce reserved C++ identifiers, and source `runtime`
would collide with the runtime namespace. These are emitted as
`jaqr_escaped_` followed by the hexadecimal source bytes. Source names beginning
with `escaped_` use the same encoding to keep the mapping collision-free.

`just test` compiles and runs generated programs exercising keyword names,
methods, fields, invariant/step references, already-prefixed names and escaped
names, with optimisation both enabled and disabled.
