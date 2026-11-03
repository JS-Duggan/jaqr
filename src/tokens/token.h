#include <string>

enum class token_type { IF, ELSE, ELIF };

struct token {
    token_type type;
    std::string lexeme;
    int line;
    int column;
};