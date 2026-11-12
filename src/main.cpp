#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "parser/parser.h"
#include "semantic/semantic.h"
#include "tokeniser/tokeniser.h"

int main(int argc, char *argv[]) {
    try {
        std::cout << "JAQR, Silent R Language (aka Step)\n";
        if (argc != 2) {
            std::cerr << "Error: no supplied file\n";
            return 1;
        }

        std::ifstream file(argv[1]);

        if (!file.is_open()) {
            std::cerr << "Error: cannot open file\n";
            return 1;
        }

        std::vector<std::vector<char>> data;

        std::string line;
        while (std::getline(file, line)) {
            data.emplace_back(line.begin(), line.end());
            data.back().push_back('\n');
        }
        ast_root_ptr root = parse(tokenise(data));
        analyse(*root);
    } catch (const std::exception &error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
