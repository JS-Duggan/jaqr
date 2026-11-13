# Language Features

JAQR (Step) is a statically typed, C-like language that compiles to C++20. Its
main idea is **transactional state**: program state can only change inside a
`step`, and a step is committed only if every invariant still holds afterwards.
Otherwise every change in that step is rolled back.

Full rules and error codes: [`semantic_spec.md`](semantic_spec.md).
Syntax: [`grammar.md`](grammar.md). Examples: [`sample_code.md`](sample_code.md).

## Feature summary

| Area | Features |
|---|---|
| Types | `int`, `float`, `char`, `byte`, `bool`; fixed arrays `[N]T`; pointers `->T`; structs |
| Variables | `let` (mutable), `imut` (immutable), `state` (transactional), `define` (global) |
| Control flow | `if` / `elif` / `else`, `while`, `for`, `return` |
| Functions | Typed parameters and return, recursion, update functions, methods |
| Step system | `state`, `step { } reject { }`, `inv { }`, `update fn` |
| Operators | Arithmetic incl. floor division `\` and power `~`; bitwise; logical; pointer `@` `<-` |
| Literals | Decimal, binary `2b101`, hex `16bff`, float, char, string, bool |
| I/O | Built-in `print` and `scan` |
| Safety | Static type checking, no shadowing, no implicit narrowing, compile-time error codes |
| Output | Readable C++20, with an optimiser |

---

## 1. Types

### 1.1 Primitive types

| Type | Size | Example |
|---|---|---|
| `int` | signed 32-bit | `42`, `2b1010`, `16bff` |
| `float` | IEEE 754 32-bit | `3.14`, `.5`, `2.` |
| `char` | signed 8-bit | `'a'`, `'\n'` |
| `byte` | unsigned 8-bit | `byte(255)` |
| `bool` | `true` / `false` | `true` |

`char` and `byte` are separate types even though both are 8 bits.

### 1.2 Arrays

```text
let a: [3]int = [1, 2, 3];
let b: [5]int = [1, 2];         # rest are zero: [1, 2, 0, 0, 0]
let grid: [2][3]float;          # 2 rows of 3 floats
let s: [6]char = "hello";       # string literal = array of char
```

- Length is a positive compile-time constant.
- Arrays are **values**: assignment, parameters and returns copy the whole array.
- No array-to-pointer decay.
- Index must be `int`. No automatic bounds checks.

### 1.3 Pointers

```text
let x: int = 5;
let p: ->int = @x;       # address-of
<-p = 10;                # dereference: x is now 10
let q: ->int = new(int, 4);   # heap allocation of 4 ints
```

- Pointer types must match exactly.
- Pointer arithmetic (`p + 1`) moves by whole elements.
- You cannot take the address of `imut` or `state` storage, so state can never be
  changed through an alias.

### 1.4 Structs

```text
struct Point {
    x: int;
    y: int;
};

let p: Point = Point{x: 1, y: 2};
p.x = 5;
```

## 2. Variables

| Keyword | Meaning | Must initialise? |
|---|---|---|
| `let` | Mutable local or global | No |
| `imut` | Immutable binding (arrays are deeply immutable) | Yes |
| `state` | Transactional variable, changed only in a step | Yes |
| `imut state` | State that can never change | Yes |
| `define` | Prefix for any global declaration | — |

```text
define state score: int = 0;     # global state
define imut LIMIT: int = 100;    # global constant

fn main() {
    let count: int;              # uninitialised
    imut name: [3]char = "Sam";
}
```

Scoping rules:

- Names must be declared before use.
- No redeclaration in the same scope.
- **No shadowing**: an inner block cannot reuse a visible outer name (`SEM002`).

## 3. Type conversions

- **Implicit** conversion is only `int ↔ float`, and only in initialisation,
  assignment and array literal elements. `float → int` truncates towards zero.
- Mixed `int` / `float` arithmetic gives `float`.
- Function arguments and return values must match **exactly**.
- **Explicit** casts between any primitive types use the type name as a function:

```text
let f: float = float(7) / 2.0;   # 3.5
let i: int = int(3.9);           # 3
let c: int = int('A');           # 65
let b: bool = bool(5);           # true
```

## 4. Operators

| Category | Operators | Notes |
|---|---|---|
| Arithmetic | `+ - * /` | `int / int` truncates: `-5 / 2 = -2` |
| Floor division | `\` | `7 \ 2 = 3` |
| Power | `~` | Right-associative: `2 ~ 3 ~ 2 = 2 ~ 9`; `-2 ~ 2 = -4` |
| Comparison | `== != < > <= >=` | Return `bool` |
| Logical | `&& \|\| !` | Short-circuit; accept `bool` or numbers |
| Bitwise | `& \| ^ << >>` | `int` only; bind tighter than comparisons |
| Pointer | `@` address-of, `<-` dereference | |
| Access | `a[i]`, `s.field`, `s.method()` | |
| Assignment | `=` | Right-associative |

Notable choices:

- `x & 1 == 0` means `(x & 1) == 0` (unlike C).
- `x<-1` means `x < -1`, not a dereference.
- Division by a constant zero is a compile error (`SEM005`).
- There is no `%`, `++`, `--` or compound assignment.

Full precedence table: [`grammar.md`](grammar.md#7-operator-precedence).

## 5. Control flow

```text
if (x > 0) {
    print("positive");
} elif (x < 0) {
    print("negative");
} else {
    print("zero");
}

while (n > 0) {
    n = n - 1;
}

for (let i: int = 0; i < 10; i = i + 1) {
    print(i);
}
```

- Bodies always use braces.
- Conditions accept `bool`, `int` or `float`; zero is false.
- Numbers are still not assignable to `bool` without a cast.

## 6. Functions

```text
fn square(x: int): int {
    return x * x;
}
```

- Ordinary functions declare a return type after `:`.
- `fn main()` takes no parameters, has no return type, and is required exactly once.
- A function must be defined before it is called. Direct recursion is allowed.
- No overloading: every function name is unique.
- Every path of a non-void function must return (`SEM008`).

## 7. The Step system

This is what makes the language different. It gives **all-or-nothing updates** to
program state, checked against declared rules.

### 7.1 State variables

```text
define state balance: int = 100;
```

State can be read anywhere. It can only be written inside a `step` or an update
function running inside a step. A write anywhere else is `SEM017`.

### 7.2 Invariants

```text
inv { balance >= 0 };
```

- Must be exactly `bool`.
- May only use state variables, literals, operators and casts.
- No function calls, locals, pointer dereferences or side effects (`SEM020`).

### 7.3 Steps

```text
step {
    balance = balance - 30;
} reject {
    print("not enough money\n");
}
```

1. All state writes in the body are made to pending copies.
2. When the body finishes, every invariant is checked against the pending state.
3. If all are `true`, the changes are committed together.
4. If any is `false`, every change is discarded and the optional `reject` block runs.

Restrictions inside a step:

- No nested steps (`SEM019`).
- No `return` or other early exit (`SEM023`).
- No `print` / `scan`, because output can't be rolled back.

### 7.4 Update functions

```text
update fn deposit(b: int)(amount: int) {
    b = b + amount;
}

step { deposit(balance)(50); }
```

- The first parameter list is **state**, the second is ordinary arguments.
- Called as `f(state...)(args...)`, only inside an active step (`SEM018`).
- Their writes join the enclosing step's transaction.
- No nested steps and no I/O inside.

### 7.5 Methods and update methods

```text
struct Account { balance: int; };

impl Account {
    fn get(): int { return this.balance; }
    update fn add(n: int) { this.balance = this.balance + n; }
};

define state acc: Account = Account{balance: 0};

fn main() {
    step { acc.add(10); }
}
```

- Methods use `this` for the receiver.
- An update method has one parameter list: the receiver is the state.

## 8. Input and output

| Function | Behaviour |
|---|---|
| `print(value)` | Writes `value` to standard output. No spaces or newline added. Supports `int`, `float`, `char`, `bool`, string literals. |
| `scan(target)` | Reads into a mutable variable. Returns `true` on success, `false` on bad input or EOF. |

```text
let n: int;
if (scan(n)) {
    print(n * 2);
    print("\n");
}
```

`print` and `scan` are ordinary names, so a user function or variable with the same
name shadows them. Details: [`io.md`](io.md).

## 9. Comments

```text
# A comment runs from '#' to the end of the line
let x: int = 1;   # trailing comment
```

## 10. Compiler features

| Feature | Detail |
|---|---|
| Clear diagnostics | One error per run with a stable code: `LEX001`, `SYN001`, `SEM001`–`SEM025`, plus file, line and column |
| Static checking | Types, scopes, return paths, immutability, pointer and state rules checked before any code is generated |
| C++20 output | Generated code is plain C++ and compiles with any C++20 compiler |
| Optimiser | Constant folding, dead-code removal, constant-branch simplification, small loop unrolling; never changes step semantics. See [`optimisation.md`](optimisation.md) |
| Tested | Unit, integration and generated-code tests (`just test`) |
