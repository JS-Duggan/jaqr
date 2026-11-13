# Sample Code

Each program below is valid JAQR (Step). Save one as `name.jaqr` and run:

```sh
./build/debug/jaqr name.jaqr
```

"Output" shows what the program prints when the generated C++ is compiled and run
(see [`README.md`](README.md#6-compile-generated-c-code-generator)).

---

## 1. Hello world

```text
# Hello world
fn main() {
    print("Hello, Step!\n");
}
```

Output:

```text
Hello, Step!
```

Shows: `fn main()`, string literals with escapes, the built-in `print`.

---

## 2. Reading input

```text
# Read two numbers and print their sum
fn add(a: int, b: int): int {
    return a + b;
}

fn main() {
    let x: int;
    let y: int;
    if (scan(x) && scan(y)) {
        print(add(x, y));
        print("\n");
    } else {
        print("expected two integers\n");
    }
}
```

Input `3 4` → output:

```text
7
```

Shows: functions with typed parameters and return type, uninitialised `let`,
`scan` returning `bool`, short-circuit `&&`, `if` / `else`.

---

## 3. Loops and arrays

```text
# Loops, arrays and conditionals
fn main() {
    let nums: [5]int = [3, 1, 4, 1, 5];
    let total: int = 0;
    for (let i: int = 0; i < 5; i = i + 1) {
        total = total + nums[i];
    }
    print("total = ");
    print(total);
    print("\n");

    let n: int = 10;
    while (n > 0) {
        if ((n & 1) == 0) {
            print(n);
            print(" ");
        }
        n = n - 1;
    }
    print("\n");
}
```

Output:

```text
total = 14
10 8 6 4 2
```

Shows: fixed-size arrays, array literals, indexing, `for`, `while`, bitwise `&`
(there is no `%` operator).

---

## 4. Sorting (arrays by value)

```text
# Bubble sort a fixed-size array
fn sort(a: [5]int): [5]int {
    for (let i: int = 0; i < 5; i = i + 1) {
        for (let j: int = 0; j < 4 - i; j = j + 1) {
            if (a[j] > a[j + 1]) {
                let tmp: int = a[j];
                a[j] = a[j + 1];
                a[j + 1] = tmp;
            }
        }
    }
    return a;
}

fn main() {
    let data: [5]int = [5, 2, 9, 1, 7];
    let sorted: [5]int = sort(data);
    for (let i: int = 0; i < 5; i = i + 1) {
        print(sorted[i]);
        print(" ");
    }
    print("\n");
}
```

Output:

```text
1 2 5 7 9
```

Shows: arrays are passed and returned **by value** (`data` is unchanged), nested loops.

---

## 5. Recursion and `elif`

```text
# Recursion and elif chains
fn factorial(n: int): int {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

fn grade(score: int): char {
    if (score >= 85) {
        return 'A';
    } elif (score >= 70) {
        return 'B';
    } elif (score >= 50) {
        return 'C';
    } else {
        return 'F';
    }
}

fn main() {
    print(factorial(5));
    print("\n");
    print(grade(72));
    print("\n");
}
```

Output:

```text
120
B
```

Shows: direct recursion, `elif` chains, `char` return values. Every path of a
non-void function must return, or the compiler reports `SEM008`.

---

## 6. Pointers, casts and operators

```text
# Pointers, casts, powers and bit operations
fn main() {
    let x: int = 7;
    let p: ->int = @x;
    <-p = <-p + 1;
    print(x);
    print("\n");

    let heap: ->int = new(int, 3);
    <-heap = 42;
    print(<-heap);
    print("\n");

    let f: float = float(x) / 2.0;
    print(f);
    print("\n");
    print(2 ~ 10);
    print("\n");
    print(16bff & 2b1010);
    print("\n");
    print(7 \ 2);
    print("\n");
    print(int('A'));
    print("\n");
}
```

Output:

```text
8
42
4
1024
10
3
65
```

Shows: pointer type `->int`, address-of `@`, dereference `<-`, heap allocation
`new(type, count)`, explicit casts `float(...)` / `int(...)`, power `~`, hex
`16b...` and binary `2b...` literals, floor division `\`.

---

## 7. State, invariants and steps

```text
# State, invariants, update functions and steps
define state balance: int = 100;

inv { balance >= 0 };

update fn withdraw(b: int)(amount: int) {
    b = b - amount;
}

fn main() {
    step {
        withdraw(balance)(30);
    } reject {
        print("rejected\n");
    }
    print(balance);
    print("\n");

    step {
        withdraw(balance)(500);
    } reject {
        print("withdrawal of 500 rejected\n");
    }
    print(balance);
    print("\n");
}
```

Output (per the semantic spec):

```text
70
withdrawal of 500 rejected
70
```

Shows: a global `state` variable, an invariant, an update function with a
state-parameter clause `(b: int)` and a normal clause `(amount: int)`, update
calls `f(state)(args)` inside `step`, and the `reject` handler. The second step
would make `balance` negative, so it is rolled back.

---

## 8. Structs, methods and update methods

```text
# Structs, methods and update methods
struct Counter {
    value: int;
    limit: int;
};

impl Counter {
    fn remaining(): int {
        return this.limit - this.value;
    }
    update fn bump(by: int) {
        this.value = this.value + by;
    }
};

define state c: Counter = Counter{value: 0, limit: 10};

inv { c.value <= c.limit };

fn main() {
    step { c.bump(4); }
    print(c.remaining());
    print("\n");
    step { c.bump(20); } reject { print("over limit\n"); }
    print(c.value);
    print("\n");
}
```

Output (per the semantic spec):

```text
6
over limit
4
```

Shows: `struct` declarations, struct literals `Counter{...}`, `impl` blocks,
`this`, an ordinary method, an update method called with one argument clause,
and an invariant over struct fields.

---

## 9. Programs the compiler rejects

Each program is one error. Line and column numbers are zero-indexed.

| Program | Compiler output |
|---|---|
| `define state s: int = 0;`<br>`fn main() { s = 5; }` | `SEM017: state write outside a step or update context` |
| `fn main() { imut x: int = 1; x = 2; }` | `SEM012: cannot assign to immutable storage` |
| `fn main() { let x: int = 1; { let x: int = 2; } }` | `SEM002: duplicate name 'x'` (no shadowing) |
| `fn main() { let b: bool = 1; }` | `SEM003: expected bool, got int` |
| `fn f(): int { let x: int = 1; }`<br>`fn main() { }` | `SEM008: function has a path without a return value` |
| `fn main() { let x: int = 10 / 0; }` | `SEM005: division by constant zero` |
| `fn main() { let x: int = 1; let p: ->int = @x; let q: ->float = p; }` | `SEM003: expected ->float, got ->int` |
| `define state s: int = 0;`<br>`fn main() { step { step { } } }` | `SEM019: nested or update-function step` |
| `define state s: int = 0;`<br>`fn main() { step { return; } }` | `SEM023: return is not allowed here` |
| `define state s: int = 0;`<br>`update fn u(a: int)() { a = 1; }`<br>`fn main() { u(s)(); }` | `SEM018: update call requires an active step` |
| `fn helper(): int { return 1; }` | `SEM024: exactly one fn main() is required` |
| `fn main() { let x: int = 123abc; }` | `LEX001: unexpected character 'a' ...` |
| `fn main() { let x: int = ; }` | `SYN001: unexpected token ';' ...` |
