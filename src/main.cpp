#include <expected>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char *argv[]) {
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
        std::vector<char> chars(line.begin(), line.end());
        chars.push_back('\n');
        data.push_back(chars);
    }
}
