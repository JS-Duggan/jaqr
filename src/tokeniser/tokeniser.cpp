#include "tokeniser.h"
#include "dfa.h"
#include "edge.h"
#include <format>
#include <stdexcept>
#include <string>

/*
outline of the tokeniser algorithm

maximal munch, with no backtracking

lines keep their '\n', so tokens (e.g. strings) can span lines

starting at line 0, col 0:
    start at start node at token_start
    follow transitions until the next char has no edge, or the end of the input
        an edge to the reject node raises an error at that char
    if stopped in a non-accepting state, raise an error at the char with no edge
        (or at the end of the input)
    add token_start..stop to the token list
    restart at the start node with token_start = stop

stopping in a non-accepting state is always an error, never a fallback to an earlier
accepting state. so 16b is an error, not INT_LITERAL '16' then IDENTIFIER 'b'.
*/

struct position {
    size_t line;
    size_t col;
};

// returns the node reached by following ch, or nullptr if no edge of node matches ch
static const graph_node *next_state(const graph_node *node, char ch) {
    const graph_node *next = nullptr;

    for (const std::unique_ptr<edge> &outgoing_edge : node->edges) {
        if (outgoing_edge->matches(ch)) {
            next = outgoing_edge->to;
            break;
        }
    }

    return next;
}

static char char_at(const std::vector<std::vector<char>> &input, const position &pos) {
    return input[pos.line][pos.col];
}

// moves pos past the end of its line (and any empty lines) onto the next char
static position skip_line_ends(const std::vector<std::vector<char>> &input, const position &start) {
    position pos = start;
    while (pos.line < input.size() && pos.col >= input[pos.line].size()) {
        pos = {pos.line + 1, 0};
    }
    return pos;
}

// the position of the char after pos, which may be on the next line
static position advance(const std::vector<std::vector<char>> &input, const position &pos) {
    return skip_line_ends(input, {pos.line, pos.col + 1});
}

// builds the token covering [start, end), which may span multiple lines
static token make_token(const std::vector<std::vector<char>> &input, token_type type,
                        const position &start, const position &end) {
    std::string lexeme;
    for (position pos = start; pos.line != end.line || pos.col != end.col;
         pos = advance(input, pos)) {
        lexeme += char_at(input, pos);
    }

    return {type, lexeme, static_cast<int>(start.line), static_cast<int>(start.col)};
}

// shows '\n', '\t' and '\r' as escapes so they don't break the error message
static std::string printable(char ch) {
    switch (ch) {
    case '\n':
        return "\\n";
    case '\t':
        return "\\t";
    case '\r':
        return "\\r";
    default:
        return {ch};
    }
}

// positions in error messages are 0-indexed
[[noreturn]] static void report_unexpected_char(char ch, const position &pos) {
    throw std::runtime_error(
        std::format("Lexer error: unexpected character '{}' at line {}, column {}", printable(ch),
                    pos.line, pos.col));
}

[[noreturn]] static void report_unexpected_end(const std::vector<std::vector<char>> &input,
                                               const position &token_start) {
    throw std::runtime_error(std::format(
        "Lexer error: unexpected end of input at line {}, column {} (unfinished token starts at "
        "line {}, column {})",
        input.size() - 1, input.back().size(), token_start.line, token_start.col));
}

std::vector<token> tokenise(const std::vector<std::vector<char>> &input) {
    std::vector<token> tokens;
    position token_start = skip_line_ends(input, {0, 0});

    while (token_start.line < input.size()) {
        const graph_node *current_node = dfa_start;
        // exclusive end of the token
        position pos = token_start;

        while (pos.line < input.size()) {
            const char ch = char_at(input, pos);
            const graph_node *next = next_state(current_node, ch);

            if (next == dfa_reject) {
                report_unexpected_char(ch, pos);
            }
            // no edge for ch, so the token ends before it
            if (next == nullptr) {
                break;
            }

            current_node = next;
            pos = advance(input, pos);
        }

        const std::optional<token_type> &type = current_node->type;
        if (!type.has_value()) {
            if (pos.line < input.size()) {
                report_unexpected_char(char_at(input, pos), pos);
            }
            report_unexpected_end(input, token_start);
        }

        tokens.push_back(make_token(input, *type, token_start, pos));

        // restart directly after the token
        token_start = pos;
    }

    return tokens;
}
