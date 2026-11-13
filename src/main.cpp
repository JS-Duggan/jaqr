#include "code_generator/code_generator.h"
#include "parser/parser.h"
#include "run.h"
#include "semantic/semantic.h"
#include "tokeniser/tokeniser.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: jaqr <source.jaqr>\n";
            return 1;
        }

        std::ifstream file(argv[1]);
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
        auto analysed = analyse(parse(tokenise(data)), argv[1]);
        auto &[root, impls, invs] = analysed;
        return run_generated(code_generator{impls, invs}.gen_code(*root));
    } catch (const semantic_error &error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
