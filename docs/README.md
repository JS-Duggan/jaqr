# JAQR (Step) — Install and Run Guide

JAQR ("Silent R Language", also called **Step**) is a small statically typed, C-like
language with built-in **transactional state**: state variables change only inside
`step` blocks, and a step is committed only if every invariant (`inv`) still holds.

The compiler is written in C++20. It reads a `.jaqr` source file, tokenises it,
parses it into an AST and runs semantic analysis. A code generator turns a checked
AST into C++.

Other docs in this folder:

| File | Contents |
|---|---|
| [`grammar.md`](grammar.md) | The full language grammar (EBNF) |
| [`features.md`](features.md) | Language features and rules |
| [`sample_code.md`](sample_code.md) | Example programs |
| [`code_structure.md`](code_structure.md) | How the compiler source is organised |
| [`semantic_spec.md`](semantic_spec.md) | Full semantic specification and error codes |
| [`io.md`](io.md) | `print` / `scan` standard I/O |
| [`optimisation.md`](optimisation.md) | Code-generation optimisations |

---

## 1. Install the tools

You need these tools:

| Tool | Version | Needed for |
|---|---|---|
| C++ compiler | C++20 with `<format>`: GCC 13+, Clang 17+, Apple Clang 15+ | Building |
| CMake | 3.21+ | Building |
| Ninja | any | Building |
| Git | any | Getting the code; Git Bash on Windows |
| Just | any | Short commands (optional) |
| Python 3 | with `venv` and `pip` | Tests, `just format` |
| clang-tidy | any | `just lint` only |

Pick your OS below and run every command in order. Total time: about 5–15 minutes,
mostly downloads.

### 1.1 Windows 10 / 11

Run in **PowerShell** (no admin needed for most packages; accept any UAC prompts):

```powershell
winget install -e --id Git.Git
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Casey.Just
winget install -e --id Python.Python.3.12
winget install -e --id LLVM.LLVM
winget install -e --id BrechtSanders.WinLibs.POSIX.UCRT
```

The last line installs GCC (MinGW-w64), the compiler the project is tested with on
Windows. `LLVM.LLVM` provides `clang-tidy`.

**Close and reopen your terminal** so the new `PATH` is loaded. Then open
**Git Bash** (Start menu → "Git Bash") and tell CMake to use GCC:

```sh
echo 'export CXX=g++' >> ~/.bashrc
source ~/.bashrc
```

Run all later commands in Git Bash: the `justfile` recipes need `sh`.

Notes:

- If you configured the project before with another compiler, run `just clean` once.
- `just format` and `just format-check` expect a Unix-style Python venv and may not
  work on Windows. They are only needed for CI formatting checks.

### 1.2 Linux — Ubuntu 24.04 / Debian 13

```sh
sudo apt update
sudo apt install -y build-essential g++ cmake ninja-build just git \
    python3 python3-venv python3-pip clang-tidy
```

Ubuntu 24.04 ships GCC 13, which supports `<format>`.

### 1.3 Linux — Ubuntu 22.04

The default GCC 11 is too old and `just` is not packaged, so add a newer GCC and
install `just` from its install script:

```sh
sudo apt update
sudo apt install -y software-properties-common
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install -y build-essential g++-13 cmake ninja-build git curl \
    python3 python3-venv python3-pip clang-tidy
mkdir -p ~/.local/bin
curl --proto '=https' --tlsv1.2 -sSf https://just.systems/install.sh | bash -s -- --to ~/.local/bin
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
echo 'export CXX=g++-13' >> ~/.bashrc
source ~/.bashrc
```

### 1.4 Linux — Fedora

```sh
sudo dnf install -y gcc-c++ cmake ninja-build just git \
    python3 python3-pip clang-tools-extra
```

### 1.5 Linux — Arch

```sh
sudo pacman -S --needed base-devel cmake ninja just git python python-pip clang
```

### 1.6 macOS

Install Apple's command-line tools (includes Apple Clang and Git):

```sh
xcode-select --install
```

Install [Homebrew](https://brew.sh) if you don't have it:

```sh
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Install the rest (`llvm` provides `clang-tidy`; the `justfile` finds it automatically):

```sh
brew install cmake ninja just python llvm
```

If the build later fails with `'format' file not found`, your Xcode is too old.
Update Xcode, or build with Homebrew's Clang:

```sh
echo 'export CXX="$(brew --prefix llvm)/bin/clang++"' >> ~/.zshrc
source ~/.zshrc
just clean
```

### 1.7 Check the install

Every command should print a version (about 1 minute):

```sh
cmake --version      # 3.21 or newer
ninja --version
g++ --version        # Windows / Linux (use g++-13 on Ubuntu 22.04)
clang++ --version    # macOS
just --version
python3 --version    # Windows: python --version
```

## 2. Get the code

```sh
git clone <repository-url> jaqr
cd jaqr
```

## 3. Build the compiler

Debug build (about 1–2 minutes the first time):

```sh
just
```

Without `just`:

```sh
cmake --preset debug
cmake --build --preset debug
```

The compiler is created at:

- Linux / macOS: `build/debug/jaqr`
- Windows: `build\debug\jaqr.exe`

For an optimised build of the compiler itself, use `just release` (or
`cmake --preset release && cmake --build --preset release`). Output goes to
`build/release/`.

## 4. Write a program

Create `hello.jaqr`:

```text
# my first Step program
fn main() {
    let x: int = 6;
    let y: int = x * 7;
}
```

## 5. Run the compiler on it

```sh
./build/debug/jaqr hello.jaqr          # Linux / macOS / Git Bash
build\debug\jaqr.exe hello.jaqr        # Windows PowerShell / cmd
```

Note: `just run` builds and runs the compiler **without** a file, so it only prints
`Error: no supplied file`. Pass the file path yourself as shown above.

### Reading the result

A valid program prints the banner and exits with code `0`:

```text
JAQR, Silent R Language (aka Step)
```

An invalid program prints one error and exits with code `1`. Line and column
numbers are **zero-indexed**.

| Prefix | Stage | Example |
|---|---|---|
| `LEX` | Tokeniser | `Error: bad.jaqr: LEX001: unexpected character 'a' at line 1, column 20` |
| `SYN` | Parser | `Error: bad.jaqr: SYN001: unexpected token ';' at line 1, column 17` |
| `SEM` | Semantic analysis | `bad.jaqr:2:4-2:5: SEM017: state write outside a step or update context` |

Semantic errors use the format `file:startLine:startCol-endLine:endCol: CODE: message`.
The full list of `SEM` codes is in [`semantic_spec.md`](semantic_spec.md#10-diagnostic-requirements).

## 6. Compile generated C++ (code generator)

The code generator is available as a C++ API,
`code_generator::gen_code(program, options)`, and is exercised by the test suite.
It emits a C++20 translation unit. Compile that output with the runtime include
directory:

```sh
c++ -std=c++20 -O2 -I runtime generated.cpp -o generated
./generated
```

See [`optimisation.md`](optimisation.md) and [`io.md`](io.md) for the generator options.

## 7. Run the tests

```sh
just test
```

Without `just`:

```sh
ctest --test-dir build/debug --output-on-failure
```

This runs the `io_semantics` and `optimizer_semantics` tests. They generate C++,
compile it with your C++ compiler at `-O0` and `-O2`, run it and compare the output.

## 8. Other commands

| Command | What it does |
|---|---|
| `just` | Build (debug) |
| `just release` | Build (release) |
| `just test` | Build, then run the tests |
| `just format` | Format all sources with the pinned clang-format 18.1.6 |
| `just format-check` | Check formatting without changing files (used in CI) |
| `just lint` | Run clang-tidy on `src/` (used in CI) |
| `just clean` | Delete the `build/` folder |

## Troubleshooting

| Problem | Fix |
|---|---|
| `fatal error: format: No such file or directory` | Compiler too old. Follow the install steps for your OS in section 1. |
| `CMake Error: ... Ninja` not found | Install Ninja and add it to `PATH`. |
| `just` fails on Windows with a shell error | Run it from Git Bash, or use the CMake commands directly. |
| `Error: no supplied file` | Pass exactly one `.jaqr` file path to the compiler. |
| `Error: cannot open file` | Check the path; it is relative to your current directory. |
