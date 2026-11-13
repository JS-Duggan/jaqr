cmake_minimum_required(VERSION 3.21)
project(jaqr LANGUAGES CXX)

file(GLOB_RECURSE JAQR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
add_executable(jaqr ${JAQR_SOURCES})
set_target_properties(jaqr PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(JAQR_CODEGEN_SOURCES
        src/optimizer/optimizer.cpp
        src/semantic/builtins.cpp
        src/code_generator/code_generator.cpp
        src/ast/declaration.cpp
        src/ast/expression.cpp
        src/ast/statement.cpp
        src/ast/type.cpp
    )
    add_executable(optimizer_tests tests/optimizer.cpp ${JAQR_CODEGEN_SOURCES})
    add_executable(io_tests tests/io.cpp ${JAQR_CODEGEN_SOURCES})
    add_executable(name_tests tests/names.cpp ${JAQR_CODEGEN_SOURCES})
    target_include_directories(name_tests PRIVATE src)
    target_compile_features(name_tests PRIVATE cxx_std_20)
    add_test(NAME generated_names
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/optimizer_test.py"
            $<TARGET_FILE:name_tests> "${CMAKE_CXX_COMPILER}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
    target_include_directories(io_tests PRIVATE src)
    target_compile_features(io_tests PRIVATE cxx_std_20)
    add_test(NAME io_semantics
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/io_test.py"
            $<TARGET_FILE:io_tests> "${CMAKE_CXX_COMPILER}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
    target_include_directories(optimizer_tests PRIVATE src)
    target_compile_features(optimizer_tests PRIVATE cxx_std_20)
    add_test(NAME optimizer_semantics
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/optimizer_test.py"
            $<TARGET_FILE:optimizer_tests> "${CMAKE_CXX_COMPILER}")
endif()
