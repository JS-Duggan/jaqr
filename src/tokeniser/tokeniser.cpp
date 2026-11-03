#include "tokeniser.h"
#include "dfa.h"
#include "edge.h"
#include <sstream>
#include <stdexcept>
#include <string>

/*
outline of the tokeniser algorithiim

maximal munch

lines keep their '\n', so tokens (e.g. strings) can span lines

starting at line 0, col 0:
    start at start node at token_start
    follow transitions until a dead state or the end of the input
        if at accepting state
            last_accepting_end = position after this char
            last_token = accepting_state.token
    if last_token = None, raise error at token_start
    add token_start..last_accepting_end to the token list
    restart at the start node with token_start = last_accepting_end
*/

struct position {
    size_t line;
    size_t col;
};

// returns the node reached by following next char, or nullptr if node is a dead state
static const graph_node *next_state(const graph_node *node, char ch) {
    const graph_node *next = nullptr;

    for (edge outgoing_edge : node->edges) {
        if (outgoing_edge.matches(ch)) {
            next = outgoing_edge.to;
            break;
        }
    }

    return next;
}

static char char_at(const std::vector<std::vector<char>> &input, position pos) {
    return input[pos.line][pos.col];
}

// moves pos past the end of its line (and any empty lines) onto the next char
static position skip_line_ends(const std::vector<std::vector<char>> &input, position pos) {
    while (pos.line < input.size() && pos.col >= input[pos.line].size()) {
        pos = {pos.line + 1, 0};
    }
    return pos;
}

// the position of the char after pos, which may be on the next line
static position advance(const std::vector<std::vector<char>> &input, position pos) {
    return skip_line_ends(input, {pos.line, pos.col + 1});
}

// builds the token covering [start, end), which may span multiple lines
static token make_token(const std::vector<std::vector<char>> &input, token_type type,
                        position start, position end) {
    std::string lexeme;
    for (position pos = start; pos.line != end.line || pos.col != end.col;
         pos = advance(input, pos)) {
        lexeme += char_at(input, pos);
    }

    return {type, lexeme, static_cast<int>(start.line), static_cast<int>(start.col)};
}

// TODO: check if this is good code
[[noreturn]] static void report_unexpected_char(char ch, size_t line, size_t col) {
    std::ostringstream message;
    message << "Lexer error: unexpected character '" << ch << "' at line " << line << ", column "
            << col;
    throw std::runtime_error(message.str());
}

std::vector<token> tokenise(const std::vector<std::vector<char>> &input) {
    std::vector<token> tokens;
    position token_start = skip_line_ends(input, {0, 0});

    while (token_start.line < input.size()) {
        const graph_node *current_node = dfa_start;
        // exclusive of end
        position last_accepting_end{0, 0};
        token_type last_accepting_token = token_type::WHITESPACE;
        bool has_last_accepting_token = false;

        for (position pos = token_start; pos.line < input.size(); pos = advance(input, pos)) {
            current_node = next_state(current_node, char_at(input, pos));

            // dead state
            if (current_node == nullptr) {
                break;
            }

            if (current_node->type.has_value()) {
                last_accepting_end = advance(input, pos);
                last_accepting_token = *current_node->type;
                has_last_accepting_token = true;
            }
        }

        if (has_last_accepting_token) {
            tokens.push_back(
                make_token(input, last_accepting_token, token_start, last_accepting_end));
        } else {
            report_unexpected_char(char_at(input, token_start), token_start.line, token_start.col);
        }

        // restart directly after the last token
        token_start = last_accepting_end;
    }

    return tokens;
}
