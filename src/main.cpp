#include "code_generator/code_generator.h"
#include "parser/parser.h"
#include "run.h"
#include "semantic/semantic.h"
#include "tokeniser/tokeniser.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char *argv[]) {
    try {
        const char *filename = nullptr;
        const char *output_name = "a.cpp";
        bool run = false;
        bool named_output = false;
        bool invalid = false;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--exec" && !run) {
                run = true;
            } else if (arg == "-o" && !named_output && i + 1 < argc && *argv[i + 1]) {
                named_output = true;
                output_name = argv[++i];
            } else if (!filename && !arg.empty() && arg.front() != '-') {
                filename = argv[i];
            } else {
                invalid = true;
                break;
            }
        }
        if (!filename || invalid) {
            std::cerr << "Usage: jaqr [--exec] <source.jaqr> [-o <output.cpp>]\n";
            return 1;
        }

        std::error_code error;
        if (std::filesystem::equivalent(filename, output_name, error)) {
            std::cerr << "Error: generated C++ output would overwrite the source file\n";
            return 1;
        }
        std::ifstream file(filename);
        if (!file) {
            std::cerr << "Cannot open source file\n";
            return 1;
        }

        // Keep line boundaries for lexer positions and to separate tokens on adjacent lines.
        std::vector<std::vector<char>> data;
        std::string line;
        while (std::getline(file, line)) {
            data.emplace_back(line.begin(), line.end());
            data.back().push_back('\n');
        }
        // Validate before optimising/emitting: unreachable code must still obey JAQR's rules.
        // The analysis result owns the AST while the generator borrows its declaration indexes.
        auto analysed = analyse(parse(tokenise(data)), filename);
        auto &[root, impls, invs] = analysed;
        return run_generated(code_generator{impls, invs}.gen_code(*root), output_name, run);
    } catch (const semantic_error &error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
