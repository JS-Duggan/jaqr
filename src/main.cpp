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
#include <vector>

int main(int argc, char *argv[]) {
    try {
        const char *filename = nullptr;
        const char *output_name = "a.cpp";
        if (argc == 2) {
            filename = argv[1];
        } else if (argc == 4 && std::string(argv[1]) == "-o") {
            output_name = argv[2];
            filename = argv[3];
        } else if (argc == 4 && std::string(argv[2]) == "-o") {
            filename = argv[1];
            output_name = argv[3];
        }
        if (!filename || !*output_name) {
            std::cerr << "Usage: jaqr <source.jaqr> [-o <output.cpp>]\n";
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

        std::vector<std::vector<char>> data;
        std::string line;
        while (std::getline(file, line)) {
            data.emplace_back(line.begin(), line.end());
            data.back().push_back('\n');
        }
        auto analysed = analyse(parse(tokenise(data)), filename);
        auto &[root, impls, invs] = analysed;
        return run_generated(code_generator{impls, invs}.gen_code(*root), output_name);
    } catch (const semantic_error &error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
