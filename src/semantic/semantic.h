#pragma once

#include "../ast/declaration.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class semantic_error : public std::runtime_error {
  public:
    std::string filename;
    std::string code;
    source_span span;
    std::string detail;

    semantic_error(std::string filename, std::string code, source_span span, std::string detail);
};

struct analysis_root {
    program_ptr ast;
    std::unordered_map<std::string, std::vector<const function_decl *>> impls;
    std::unordered_map<std::string, std::vector<const invariant_decl *>> invs;
};

analysis_root analyse(program_ptr root, std::string_view filename = "<input>");
