# Semantic Specification

## 1. Purpose and scope

This specification defines the semantic rules enforced by the Step compiler after parsing and before code generation.

The semantic analyser is responsible for:

- Resolving identifiers and enforcing lexical scope.
- Validating declarations, expressions, assignments and operators.
- Checking types, implicit conversions and explicit casts.
- Validating function calls, signatures and return paths.
- Enforcing array, pointer and immutability rules.
- Enforcing Step-specific state, update function, invariant and transaction restrictions.
- Reporting semantic errors with source locations.

The analyser must distinguish three classes of behaviour:

| Classification | Meaning |
|---|---|
| Semantic error | The compiler must reject the program |
| Runtime error | The program is valid but execution must report a failure under specified conditions |
| Undefined behaviour (UB) | Step does not guarantee the result; the analyser need not reject the program |

Runtime behaviour is included here as a contract for the code generator and runtime, even when the semantic analyser cannot validate it statically.

## 2. Type system

### 2.1 Primitive types

| Type | Representation | Description |
|---|---|---|
| `int` | Signed 32-bit | Integer values |
| `float` | IEEE 754 binary32 | Floating-point values |
| `char` | Signed 8-bit | Character code |
| `byte` | Unsigned 8-bit | Raw byte value |
| `bool` | Boolean | `true` or `false` |
| `void` | No value | Function return type |

`char` and `byte` remain distinct semantic types despite having identical underlying representations.

### 2.2 Composite types

Step supports:

- Fixed-size arrays: `[N]T`
- Pointers: `->T`
- Function signatures consisting of parameter types and a return type
- The `null` literal for pointer contexts

Types are checked structurally where appropriate. Two array types are equal only if their element types and lengths match. Two pointer types are equal only if their pointee types match.

No implicit array-to-pointer conversion or array decay is permitted.

A string literal represents a fixed-size array of `char`. Its length counts decoded bytes, including
escaped characters after decoding. It has no implicit trailing zero. An empty string needs a
declared `char` array as its initializer, since declared array lengths must be positive.

A `null` literal may be used in a pointer context but not as an ordinary numeric value.

## 3. Declarations, scope and initialization

### 3.1 Identifier resolution

An identifier must have a visible declaration before use.

Redeclaring an identifier in the same lexical scope is prohibited.

Variable shadowing is prohibited. A nested scope cannot declare a variable with the same name as a visible outer variable.

Entering a nested lexical block creates a new scope. Exiting the block removes its local declarations from visibility.

### 3.2 Mutable variables

Mutable variables may be declared without initializers.

The semantic analyser does not perform definite-assignment checking and does not reject reads before initialization.

An uninitialized local variable has C++-style uninitialized storage semantics. Its value is not automatically set to zero; reading an indeterminate value may cause undefined or erroneous behaviour.

### 3.3 Immutable variables

An `imut` variable must be initialized at declaration.

An `imut` binding cannot be reassigned.

An immutable array is deeply immutable. Neither the array nor any of its elements may be modified.

An `imut` pointer is an immutable pointer binding, not a pointer to immutable data. Reassigning the pointer is prohibited, but modifying its mutable pointee is allowed.

Taking the address of an immutable variable or an element of an immutable array is prohibited.

## 4. Type conversion rules

### 4.1 Implicit conversions

Implicit `int ↔ float` conversion is permitted for:

- Variable initialization.
- Ordinary assignment.
- Array initializer elements.

`float → int` truncates towards zero.

Mixed `int` and `float` arithmetic and comparisons promote the integer operand to `float`.

No implicit conversions involving `char`, `byte` or `bool` are permitted in ordinary assignment or initialization.

Function arguments and return expressions must match their declared types exactly. The general assignment conversion rules do not apply at function boundaries.

### 4.2 Boolean contexts

`if`, `while`, `for` and logical operators accept `bool`, `int` or `float` expressions.

Truthiness is defined as follows:

- Numeric zero is false.
- Any nonzero numeric value, including negative values, is true.
- `bool` values retain their normal meaning.

This contextual truthiness does not permit implicit assignment from numeric types to `bool`.

### 4.3 Explicit casts

Explicit casts are permitted between all five primitive types.

Explicit conversion semantics follow standard C conversions with Step's declared primitive representations.

Consequently:

- Floating-point to integer conversion truncates towards zero.
- Integer to floating-point conversion may lose precision.
- Integer to unsigned 8-bit conversion follows C's modulo-256 conversion.
- Numeric to boolean conversion tests whether the value is nonzero.
- Boolean to numeric conversion produces zero or one.
- Out-of-range conversions follow C's rules, including undefined behaviour where applicable.

Explicit casts do not permit arbitrary reinterpretation between pointers, arrays or unrelated composite types.

## 5. Expression and operator semantics

### 5.1 Arithmetic

Ordinary arithmetic operators `+`, `-`, `*`, `/`, `\` and `~` accept `int` and `float` operands only, except for the separately defined pointer operations. The grammar uses `\` for floor division and `~` for exponentiation.

| Operands | Result type |
|---|---|
| `int`, `int` | `int` |
| `int`, `float` | `float` |
| `float`, `int` | `float` |
| `float`, `float` | `float` |

Integer division returns an integer and truncates towards zero.

Examples: `5 / 2 = 2` and `-5 / 2 = -2`.

Division by a compile-time constant zero divisor is a semantic error for both `/` and `\`.

Division by a dynamically evaluated zero divisor must raise a runtime error, including floating-point division by zero.

### 5.2 Comparisons

All comparison operators produce `bool`.

| Operator | Allowed operands |
|---|---|
| `==`, `!=` | Numeric operands with `int/float` promotion; matching `char`, `byte` or `bool`; compatible pointers |
| `<`, `>`, `<=`, `>=` | Numeric operands; two `char` values; two `byte` values |

Equality between `char`, `byte` and `bool` requires both operands to have exactly the same type.

Boolean ordering comparisons are prohibited.

Pointer equality comparisons are allowed only for identical pointer types. Pointer ordering comparisons are prohibited.

### 5.3 Logical operators

`&&`, `||` and `!` accept boolean or numeric operands using the defined truthiness rules.

All logical operators return `bool`.

`&&` and `||` use short-circuit evaluation.

### 5.4 Bitwise and shift operators

`&`, `|`, `^`, `<<` and `>>` take two `int` operands and return `int`. A compile-time shift count must be between zero and 31. Address-of uses `@`.

### 5.5 Other arithmetic restrictions

`char`, `byte` and `bool` cannot participate in ordinary arithmetic without explicit conversion to a numeric type.

## 6. Array semantics

### 6.1 Array types and lengths

Fixed-size array lengths must be strictly positive compile-time constant integer expressions.

Runtime-computed array lengths are prohibited.

Array index expressions must have exactly type `int`. Other types require explicit casting.

Out-of-bounds array access has undefined behaviour. The semantic analyser is not required to reject known out-of-range indices, and code generation does not insert automatic bounds checks.

Negative indices are type-correct but cause undefined behaviour if outside the array bounds.

### 6.2 Array initialization

An array initializer may provide any number of elements up to the declared size.

Providing more elements than the declared capacity is a semantic error.

When an array initializer is present but incomplete, trailing elements are zero-initialized using the zero value of their element type.

A string literal may initialize a larger `char` array. The same trailing zero-initialization rule
applies. Assignment and function arguments still require exactly matching array lengths.

Initializer elements permit the same implicit `int ↔ float` conversions as variable initialization. Other conversions require explicit casts.

An array declared without any initializer retains the agreed uninitialized-variable semantics; it is not automatically zero-initialized by the partial-initializer rule.

### 6.3 Array assignment and function boundaries

Array assignment uses value semantics, performing a deep copy.

Array assignment requires exactly matching element types and array sizes.

Arrays passed to functions are passed by value and copied. Their parameter types must match exactly.

Array return values are permitted when supported by the grammar. The return type must match exactly, and the returned array has value semantics.

Array-to-pointer decay is prohibited.

## 7. Pointer semantics

### 7.1 Pointer compatibility and assignment

Pointer assignment copies the address, not the pointed-to object.

Pointer types must have identical pointee types for assignment and pointer-to-pointer equality.

`null` is supported for pointer initialization, assignment and equality comparisons.

### 7.2 Address-of and dereferencing

The address-of operator `&` requires a mutable, addressable storage location.

Valid operands include mutable variables and mutable array elements.

Taking addresses of temporaries, immutable variables, immutable array elements or state variables is prohibited.

Dereferencing a pointer produces an lvalue of its pointee type, allowing modification when the pointed-to object is mutable.

Dereferencing `null` has undefined behaviour.

### 7.3 Pointer arithmetic

Pointer addition and subtraction with integer offsets are allowed. Offsets operate in units of pointee elements.

Pointer increment and decrement are allowed where the grammar provides these operators and the pointer binding is mutable.

Pointer arithmetic follows C++ array bounds rules. A one-past-the-end pointer may be formed but may not be dereferenced.

Out-of-bounds pointer dereferencing is undefined behaviour. No automatic runtime bounds checks are required.

Subtracting two pointers with identical pointee types is allowed if both refer to the same array or its one-past-end position. The result is an `int` element distance.

Subtracting unrelated pointers has undefined behaviour.

Modifying an `imut` pointer through reassignment or pointer increment/decrement is prohibited.

### 7.4 Pointer and immutability safety

Pointers cannot reference immutable variables or immutable array elements.

Pointers cannot reference state storage. The semantic rules must not permit indirect state mutation through a pointer alias.

## 8. Functions and control flow

### 8.1 Function declarations

A function must be defined before it can be called.

Direct recursion is permitted. A function becomes visible at the beginning of its definition. Methods may call themselves through `this`. Mutual recursion is impossible, because the second function is not visible inside the first.

Function overloading is prohibited. Each function name must be unique regardless of parameter types or return type.

Function calls must have the declared number of arguments, with exact argument type matches.

### 8.2 Return statements

Return expressions must match the declared return type exactly.

A non-`void` function must return a value on every possible execution path.

`void` functions may terminate without an explicit return. An explicit `return;` is also permitted.

A value-returning function cannot use a bare `return;`, and a `void` function cannot return a value.

### 8.3 Control-flow statements

`break` and `continue` are legal only within loops.

`return` is legal only within functions, subject to restrictions imposed by Step transactions.

The semantic analyser must validate the return-path coverage of all non-`void` functions.

Analysis may conservatively assume a loop can terminate unless its nontermination is established.

## 9. Step-specific semantic rules

### 9.1 State variables

State variables may be read wherever their declarations are visible, except where a stricter context imposes restrictions.

State variables may be modified only within a `step` transaction or by an update function executing as part of an active step.

Ordinary functions cannot modify state variables.

Taking the address of state storage is prohibited, preventing mutation through pointer aliases.

State writes inside a step are provisional until transaction validation succeeds.

### 9.2 Step blocks

Nested step blocks are prohibited.

A step establishes a transactional execution context.

State writes made during a step remain pending until the step successfully completes.

On normal completion, all applicable invariants must be evaluated against the proposed state.

If every invariant evaluates to `true`, the pending state changes are committed.

If any invariant evaluates to `false`, all pending state changes from that step are rolled back and a runtime error is raised.

Early exits that bypass step completion and invariant checking are prohibited.

`scan` and `print` operations are prohibited within step blocks.

### 9.3 Update functions

Update functions may be invoked from an active step or from another update function executing within an active step.

An update function must not be invoked from an ordinary execution context outside a step transaction.

Update functions may modify state, but those modifications are part of the enclosing transaction and are not committed independently.

Update functions cannot contain nested step blocks.

`scan` and `print` are prohibited inside update functions.

Ordinary functions cannot modify state, whether called directly or from code executing during a step.

State modifications made by nested update-function calls must participate in the same transaction rollback and commit operation.

### 9.4 Invariants

An `inv` expression must have exactly type `bool`.

Numeric truthiness does not automatically convert a numeric invariant result into a valid boolean type.

Invariant expressions may reference only:

- State variables.
- Literals.
- Pure operators.
- Explicit casts without side effects.

Invariants cannot reference ordinary local or non-state variables.

Function calls are prohibited within invariant expressions.

Pointer dereferencing and mutation are prohibited in invariants.

Invariant evaluation must not change program state or cause observable side effects.

All applicable invariants must hold for a transaction to commit.

### 9.5 I/O restrictions

`scan` is prohibited inside steps and update functions.

`print` is prohibited inside steps and update functions.

I/O remains permitted in ordinary contexts, subject to the language's normal syntax and typing requirements.

The aim is to prevent irreversible I/O operations from occurring within a transaction that may subsequently roll back.

## 10. Diagnostic requirements

Every semantic diagnostic should contain:

- Stable error code.
- Error message.
- Source filename and span.
- Relevant identifier or expression.
- Expected type and actual type, where applicable.

Semantic source spans are half-open and use zero-indexed lines and columns. The CLI includes
the input filename. Lexer and parser errors use the `LEX` and `SYN` prefixes.

### Diagnostic catalogue

Each error reports exactly one of these codes. When a program breaks several rules
at once, the first check to fail decides the code. Type mismatches are found
first, so a wrong argument, return or index type is `SEM003`, not a more specific
code.

| Code | Diagnostic |
|---|---|
| `SEM001` | Unknown name, type, struct or field. Also a call to a function that is not defined yet, including mutual recursion, and `this` outside a method |
| `SEM002` | Duplicate declaration or illegal shadowing, including duplicate functions, methods, fields and structs, and an invalid `main` signature |
| `SEM003` | Value of the wrong type: invalid implicit conversion, wrong argument type, wrong return value type, non-`int` array index |
| `SEM004` | Invalid operand types, condition, conversion, member access or statement |
| `SEM005` | Division by constant zero |
| `SEM006` | Wrong number of arguments, or calling something that is not a function |
| `SEM007` | Missing return value, or an update function that returns a value |
| `SEM008` | Missing return path |
| `SEM009` | Not used. A call before the definition reports `SEM001` |
| `SEM010` | Not used. A duplicate function reports `SEM002` |
| `SEM011` | Not reached from source. The grammar requires initializers for `state` and `imut`, so a missing one is `SYN001` |
| `SEM012` | Modification of immutable storage |
| `SEM013` | Invalid array length or initializer |
| `SEM014` | Indexing something that is not an array or pointer |
| `SEM015` | Incompatible pointer operation |
| `SEM016` | Invalid address-of operand |
| `SEM017` | State mutation outside permitted context |
| `SEM018` | Invalid update-function call |
| `SEM019` | Nested step |
| `SEM020` | Invalid invariant expression |
| `SEM021` | Prohibited I/O operation |
| `SEM022` | Not used. There is no `break` or `continue`, and invalid `return`s report `SEM023` or `SEM007` |
| `SEM023` | Early exit from step |
| `SEM024` | Missing main function |
| `SEM025` | Unsupported expression or type |
