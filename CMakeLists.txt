cmake_minimum_required(VERSION 3.21)
project(jaqr LANGUAGES CXX)

file(GLOB_RECURSE JAQR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER JAQR_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
add_library(jaqr_frontend STATIC ${JAQR_SOURCES})
add_executable(jaqr src/main.cpp)
target_link_libraries(jaqr PRIVATE jaqr_frontend)
set_target_properties(jaqr jaqr_frontend PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

