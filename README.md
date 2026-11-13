# JAQR, Silent R Language (aka Step)

## Tools

- CMake ([installation guide](https://cmake.org/download/))
- Ninja ([installation guide](https://github.com/ninja-build/ninja/releases))
- Just (optional for dev) ([installation guide](https://just.systems/man/en/installation.html))
- C++20 compiler
- Python 3 with venv and pip (for the pinned clang-format version)
- clang-tidy

## Usage (without Just)

Configure and build from the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
```

Run a JAQR program:

```sh
./build/debug/jaqr file.jaqr
```

The command saves generated C++ as `a.cpp` in the current directory, then compiles
and runs it. Use `./build/debug/jaqr file.jaqr -o custom.cpp` to change that name.
Your program's output goes to stdout. On Windows, use `build\debug\jaqr.exe`.

For a release build, replace `debug` with `release` in the commands above.

## Dev Commands

- `just` — build (debug)
- `just run file.jaqr` — build and run a program
- `just release` — build (release)
- `just format` / `just format-check` — format / check formatting
- `just lint` — run clang-tidy
- `just clean` — remove build files

## Source files

CMake automatically includes all `.cpp` files under `src/`, including subdirectories.
Adding or removing a source file is picked up on the next build; no changes to
`CMakeLists.txt` are needed.
