default: build

configure:
    cmake --preset debug

build: configure
    cmake --build --preset debug

run: build
    ./build/debug/jaqr

# Generate C++ from a JAQR source file into output.cpp.
[positional-arguments]
analyse source:
    #!/bin/sh
    set -eu
    if [ ! -x build/debug/jaqr ]; then
        just build
    fi
    generated_file="$(mktemp .jaqr-generated.XXXXXX)"
    trap 'rm -f "$generated_file"' EXIT
    ./build/debug/jaqr "$1" > "$generated_file"
    mv "$generated_file" output.cpp

# Compile a JAQR source file into output, using a temporary C++ file.
[positional-arguments]
compile source:
    #!/bin/sh
    set -eu
    if [ ! -x build/debug/jaqr ]; then
        just build
    fi
    build_directory="$(mktemp -d .jaqr-compile.XXXXXX)"
    trap 'rm -rf "$build_directory"' EXIT
    ./build/debug/jaqr "$1" > "$build_directory/generated.cpp"
    "${CXX:-c++}" -std=c++20 -O2 -I runtime "$build_directory/generated.cpp" -o "$build_directory/output"
    mv "$build_directory/output" output

release:
    cmake --preset release
    cmake --build --preset release

format-tool:
    #!/bin/sh
    set -eu
    if [ ! -x .format-venv/bin/python ]; then
        python3 -m venv .format-venv
    fi
    .format-venv/bin/python -m pip install --disable-pip-version-check 'clang-format==18.1.6'

format: format-tool
    find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -exec .format-venv/bin/clang-format -i {} +

format-check: format-tool
    find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -exec .format-venv/bin/clang-format --dry-run --Werror {} +

lint *args: configure
    #!/bin/sh
    set -eu
    if command -v clang-tidy >/dev/null 2>&1; then
        tidy_executable="$(command -v clang-tidy)"
    elif command -v brew >/dev/null 2>&1; then
        tidy_executable="$(brew --prefix llvm)/bin/clang-tidy"
    else
        echo "clang-tidy was not found. Install LLVM and add its bin directory to PATH." >&2
        exit 1
    fi
    if [ ! -x "$tidy_executable" ]; then
        echo "clang-tidy was not found at $tidy_executable. Install it with: brew install llvm" >&2
        exit 1
    fi
    if command -v xcrun >/dev/null 2>&1; then
        SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"
        export SDKROOT
    fi
    find src -type f -name '*.cpp' -exec "$tidy_executable" -p build/debug {{ args }} {} +

clean:
    cmake -E rm -rf build
