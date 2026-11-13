#pragma once

#include "../ast/declaration.h"
#include <stdexcept>
#include <string>
#include <string_view>

class semantic_error : public std::runtime_error {
  public:
    std::string filename;
    std::string code;
    source_span span;
    std::string detail;

    semantic_error(std::string filename, std::string code, source_span span, std::string detail);
};

void analyse(const program &root, std::string_view filename = "<input>");
