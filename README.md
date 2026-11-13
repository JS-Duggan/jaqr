# JAQR, Silent R Language (aka Step)

## Tools

- CMake ([installation guide](https://cmake.org/download/))
- Ninja ([installation guide](https://github.com/ninja-build/ninja/releases))
- Just ([installation guide](https://just.systems/man/en/installation.html))
- C++20 compiler
- Python 3 with venv and pip (for the pinned clang-format version)
- clang-tidy

## Commands

- `just` — build (debug)
- `just run` — build and run
- `just release` — build (release)
- `just format` / `just format-check` — format / check formatting
- `just lint` — run clang-tidy
- `just clean` — remove build files

## Source files

CMake automatically includes all `.cpp` files under `src/`, including subdirectories.
Adding or removing a source file is picked up on the next build; no changes to
`CMakeLists.txt` are needed.
