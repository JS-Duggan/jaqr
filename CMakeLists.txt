cmake_minimum_required(VERSION 3.21)
project(jaqr LANGUAGES CXX)

add_executable(jaqr
    src/main.cpp
    src/ast/declaration.cpp
    src/ast/expression.cpp
    src/ast/statement.cpp
    src/ast/type.cpp
    src/tokeniser/dfa.cpp
    src/tokeniser/tokeniser.cpp
    src/parser/parser.cpp
    src/ast/ast_builder.cpp

)
set_target_properties(jaqr PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
