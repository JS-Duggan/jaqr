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

format:
    find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -exec clang-format -i {} +

format-check:
    find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -exec clang-format --dry-run --Werror {} +

lint: configure
    #!/bin/sh
    set -eu
    if command -v xcrun >/dev/null 2>&1; then
        SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"
        export SDKROOT
    fi
    find src -type f -name '*.cpp' -exec clang-tidy -p build/debug {} +

clean:
    cmake -E rm -rf build
