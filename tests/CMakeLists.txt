include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# keep gtest on the same runtime as the code under test when building with MSVC
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# the compiler front end (everything the tests exercise), built once and shared by every test
add_library(jaqr_front_end STATIC
    ${PROJECT_SOURCE_DIR}/src/tokeniser/tokeniser.cpp
    ${PROJECT_SOURCE_DIR}/src/tokeniser/dfa.cpp
    ${PROJECT_SOURCE_DIR}/src/parser/parser.cpp
    ${PROJECT_SOURCE_DIR}/src/ast/ast_builder.cpp
    ${PROJECT_SOURCE_DIR}/src/ast/declaration.cpp
    ${PROJECT_SOURCE_DIR}/src/ast/expression.cpp
    ${PROJECT_SOURCE_DIR}/src/ast/statement.cpp
    ${PROJECT_SOURCE_DIR}/src/ast/type.cpp
)
target_compile_features(jaqr_front_end PUBLIC cxx_std_20)
set_target_properties(jaqr_front_end PROPERTIES CXX_EXTENSIONS OFF)

# adds a gtest executable named after its source file: jaqr_add_test(foo) builds foo.cpp
function(jaqr_add_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(${name} PRIVATE jaqr_front_end GTest::gtest_main GTest::gmock)
    # with MinGW, link the runtime in so the tests never pick up a different libstdc++ DLL from
    # PATH (e.g. the one Git Bash ships), which crashes on the first thrown exception
    if(MINGW)
        target_link_options(${name} PRIVATE -static)
    endif()
    # tests are found when ctest runs, so a failing test never breaks the build
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

jaqr_add_test(tokeniser_tests)
jaqr_add_test(parser_tests ast_dump.cpp)
jaqr_add_test(tokeniser_parser_integration_tests ast_dump.cpp)
