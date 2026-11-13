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
