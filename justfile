default: build

configure:
    cmake --preset debug

build: configure
    cmake --build --preset debug

run: build
    ./build/debug/jaqr

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

lint: configure
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
    find src -type f -name '*.cpp' -exec "$tidy_executable" -p build/debug {} +

clean:
    cmake -E rm -rf build
