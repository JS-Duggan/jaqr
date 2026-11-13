#include "dfa.h"

#include <initializer_list>
#include <string_view>
#include <utility>

namespace {

// ---- matchers ----

constexpr auto is(char expected) {
    return [expected](char c) { return c == expected; };
}
constexpr auto is_not(char excluded) {
    return [excluded](char c) { return c != excluded; };
}
constexpr auto in_range(char lo, char hi) {
    return [lo, hi](char c) { return c >= lo && c <= hi; };
}
constexpr auto one_of(std::string_view chars) {
    return [chars](char c) { return chars.find(c) != std::string_view::npos; };
}
constexpr auto none_of(std::string_view chars) {
    return [chars](char c) { return chars.find(c) == std::string_view::npos; };
}
template <typename... Matchers> constexpr auto any(Matchers... matchers) {
    return [matchers...](char c) { return (matchers(c) || ...); };
}

constexpr auto is_digit = in_range('0', '9');
constexpr auto is_letter = any(in_range('a', 'z'), in_range('A', 'Z'));
constexpr auto is_binary_digit = one_of("01");
constexpr auto is_hexadecimal_digit = any(is_digit, in_range('a', 'f'), in_range('A', 'F'));
constexpr auto is_identifier_char = any(is_letter, is_digit, is('_'));
constexpr auto is_whitespace = one_of(" \t\n\r");
// the char after a backslash in a char or string literal: \n \r \t \0 \\ \' \"
constexpr auto is_escape_char = one_of("nrt0\\'\"");

// ---- edge list builder ----
// graph_node owns its edges through unique_ptr, which can't be copied out of a brace list,
// so each edge is copied into its own allocation here.
std::vector<std::unique_ptr<edge>> edges(std::initializer_list<edge> list) {
    std::vector<std::unique_ptr<edge>> result;
    result.reserve(list.size());
    for (const edge &e : list) {
        result.push_back(std::make_unique<edge>(e));
    }
    return result;
}

// Nodes are defined before anything that points to them.

// The graph is built at startup. Building it only throws if allocation fails, which can't be
// recovered from, so the check is turned off for the whole graph.
// NOLINTBEGIN(bugprone-throwing-static-initialization)

// ---- identifiers ----

const graph_node identifier{
    token_type::IDENTIFIER,
    edges({
        {is_identifier_char, &identifier},
    }),
};

// ---- keywords ----
// One node per prefix. Any other identifier char falls back to identifier.

// A complete keyword.
graph_node keyword(token_type type) {
    return {type, edges({{is_identifier_char, &identifier}})};
}

// A partial keyword: an identifier that can continue into one or more keywords.
graph_node prefix(std::initializer_list<std::pair<char, const graph_node *>> next) {
    graph_node node{token_type::IDENTIFIER, {}};
    for (const auto &[ch, to] : next) {
        node.edges.push_back(std::make_unique<edge>(edge{is(ch), to}));
    }
    node.edges.push_back(std::make_unique<edge>(edge{is_identifier_char, &identifier}));
    return node;
}

// bool, byte
const graph_node kw_bool = keyword(token_type::BOOL);
const graph_node kw_prefix_boo = prefix({{'l', &kw_bool}});
const graph_node kw_prefix_bo = prefix({{'o', &kw_prefix_boo}});
const graph_node kw_byte = keyword(token_type::BYTE);
const graph_node kw_prefix_byt = prefix({{'e', &kw_byte}});
const graph_node kw_prefix_by = prefix({{'t', &kw_prefix_byt}});
const graph_node kw_prefix_b = prefix({{'o', &kw_prefix_bo}, {'y', &kw_prefix_by}});

// char
const graph_node kw_char = keyword(token_type::CHAR);
const graph_node kw_prefix_cha = prefix({{'r', &kw_char}});
const graph_node kw_prefix_ch = prefix({{'a', &kw_prefix_cha}});
const graph_node kw_prefix_c = prefix({{'h', &kw_prefix_ch}});

// define
const graph_node kw_define = keyword(token_type::DEFINE);
const graph_node kw_prefix_defin = prefix({{'e', &kw_define}});
const graph_node kw_prefix_defi = prefix({{'n', &kw_prefix_defin}});
const graph_node kw_prefix_def = prefix({{'i', &kw_prefix_defi}});
const graph_node kw_prefix_de = prefix({{'f', &kw_prefix_def}});
const graph_node kw_prefix_d = prefix({{'e', &kw_prefix_de}});

// elif, else
const graph_node kw_elif = keyword(token_type::ELIF);
const graph_node kw_prefix_eli = prefix({{'f', &kw_elif}});
const graph_node kw_else = keyword(token_type::ELSE);
const graph_node kw_prefix_els = prefix({{'e', &kw_else}});
const graph_node kw_prefix_el = prefix({{'i', &kw_prefix_eli}, {'s', &kw_prefix_els}});
const graph_node kw_prefix_e = prefix({{'l', &kw_prefix_el}});

// false, float, fn, for
const graph_node kw_false = keyword(token_type::BOOL_LITERAL);
const graph_node kw_prefix_fals = prefix({{'e', &kw_false}});
const graph_node kw_prefix_fal = prefix({{'s', &kw_prefix_fals}});
const graph_node kw_prefix_fa = prefix({{'l', &kw_prefix_fal}});
const graph_node kw_float = keyword(token_type::FLOAT);
const graph_node kw_prefix_floa = prefix({{'t', &kw_float}});
const graph_node kw_prefix_flo = prefix({{'a', &kw_prefix_floa}});
const graph_node kw_prefix_fl = prefix({{'o', &kw_prefix_flo}});
const graph_node kw_fn = keyword(token_type::FN);
const graph_node kw_for = keyword(token_type::FOR);
const graph_node kw_prefix_fo = prefix({{'r', &kw_for}});
const graph_node kw_prefix_f = prefix({
    {'a', &kw_prefix_fa},
    {'l', &kw_prefix_fl},
    {'n', &kw_fn},
    {'o', &kw_prefix_fo},
});

// if, impl, imut, int, inv
const graph_node kw_if = keyword(token_type::IF);
const graph_node kw_impl = keyword(token_type::IMPL);
const graph_node kw_prefix_imp = prefix({{'l', &kw_impl}});
const graph_node kw_imut = keyword(token_type::IMUT);
const graph_node kw_prefix_imu = prefix({{'t', &kw_imut}});
const graph_node kw_prefix_im = prefix({{'p', &kw_prefix_imp}, {'u', &kw_prefix_imu}});
const graph_node kw_int = keyword(token_type::INT);
const graph_node kw_inv = keyword(token_type::INV);
const graph_node kw_prefix_in = prefix({{'t', &kw_int}, {'v', &kw_inv}});
const graph_node kw_prefix_i = prefix({
    {'f', &kw_if},
    {'m', &kw_prefix_im},
    {'n', &kw_prefix_in},
});

// let
const graph_node kw_let = keyword(token_type::LET);
const graph_node kw_prefix_le = prefix({{'t', &kw_let}});
const graph_node kw_prefix_l = prefix({{'e', &kw_prefix_le}});

// new
const graph_node kw_new = keyword(token_type::NEW);
const graph_node kw_prefix_ne = prefix({{'w', &kw_new}});
const graph_node kw_prefix_n = prefix({{'e', &kw_prefix_ne}});

// reject, return
const graph_node kw_reject = keyword(token_type::REJECT);
const graph_node kw_prefix_rejec = prefix({{'t', &kw_reject}});
const graph_node kw_prefix_reje = prefix({{'c', &kw_prefix_rejec}});
const graph_node kw_prefix_rej = prefix({{'e', &kw_prefix_reje}});
const graph_node kw_return = keyword(token_type::RETURN);
const graph_node kw_prefix_retur = prefix({{'n', &kw_return}});
const graph_node kw_prefix_retu = prefix({{'r', &kw_prefix_retur}});
const graph_node kw_prefix_ret = prefix({{'u', &kw_prefix_retu}});
const graph_node kw_prefix_re = prefix({{'j', &kw_prefix_rej}, {'t', &kw_prefix_ret}});
const graph_node kw_prefix_r = prefix({{'e', &kw_prefix_re}});

// state, step, struct
const graph_node kw_state = keyword(token_type::STATE);
const graph_node kw_prefix_stat = prefix({{'e', &kw_state}});
const graph_node kw_prefix_sta = prefix({{'t', &kw_prefix_stat}});
const graph_node kw_step = keyword(token_type::STEP);
const graph_node kw_prefix_ste = prefix({{'p', &kw_step}});
const graph_node kw_struct = keyword(token_type::STRUCT);
const graph_node kw_prefix_struc = prefix({{'t', &kw_struct}});
const graph_node kw_prefix_stru = prefix({{'c', &kw_prefix_struc}});
const graph_node kw_prefix_str = prefix({{'u', &kw_prefix_stru}});
const graph_node kw_prefix_st = prefix({
    {'a', &kw_prefix_sta},
    {'e', &kw_prefix_ste},
    {'r', &kw_prefix_str},
});
const graph_node kw_prefix_s = prefix({{'t', &kw_prefix_st}});

// this, true
const graph_node kw_this = keyword(token_type::THIS);
const graph_node kw_prefix_thi = prefix({{'s', &kw_this}});
const graph_node kw_prefix_th = prefix({{'i', &kw_prefix_thi}});
const graph_node kw_true = keyword(token_type::BOOL_LITERAL);
const graph_node kw_prefix_tru = prefix({{'e', &kw_true}});
const graph_node kw_prefix_tr = prefix({{'u', &kw_prefix_tru}});
const graph_node kw_prefix_t = prefix({{'h', &kw_prefix_th}, {'r', &kw_prefix_tr}});

// update
const graph_node kw_update = keyword(token_type::UPDATE);
const graph_node kw_prefix_updat = prefix({{'e', &kw_update}});
const graph_node kw_prefix_upda = prefix({{'t', &kw_prefix_updat}});
const graph_node kw_prefix_upd = prefix({{'a', &kw_prefix_upda}});
const graph_node kw_prefix_up = prefix({{'d', &kw_prefix_upd}});
const graph_node kw_prefix_u = prefix({{'p', &kw_prefix_up}});

// while
const graph_node kw_while = keyword(token_type::WHILE);
const graph_node kw_prefix_whil = prefix({{'e', &kw_while}});
const graph_node kw_prefix_whi = prefix({{'l', &kw_prefix_whil}});
const graph_node kw_prefix_wh = prefix({{'i', &kw_prefix_whi}});
const graph_node kw_prefix_w = prefix({{'h', &kw_prefix_wh}});

// ---- reject ----
// Following an edge to reject is a lexer error at that char, e.g. the 'a' in 123abc.
const graph_node reject{std::nullopt, {}};

// ---- numbers ----
// A digit or letter straight after a number is an error (123abc, 17b, 2b102, 16bfg).
// The exception is right after the '.', so 5.foo is FLOAT_LITERAL '5.' then IDENTIFIER 'foo'.

// 16bff14
const graph_node hexadecimal{
    token_type::BASE_16_LITERAL,
    edges({
        {is_hexadecimal_digit, &hexadecimal},
        {is_identifier_char, &reject},
    }),
};
const graph_node hexadecimal_prefix{std::nullopt,
                                    edges({{is_hexadecimal_digit, &hexadecimal}})}; // 16b

// 2b100
const graph_node binary{
    token_type::BASE_2_LITERAL,
    edges({
        {is_binary_digit, &binary},
        {is_identifier_char, &reject},
    }),
};
const graph_node binary_prefix{std::nullopt, edges({{is_binary_digit, &binary}})}; // 2b

// 1.5 .5 (a second '.' is an error: 1.2.3)
const graph_node float_fraction{
    token_type::FLOAT_LITERAL,
    edges({
        {is_digit, &float_fraction},
        {is_identifier_char, &reject},
        {is('.'), &reject},
    }),
};
// 1. (no edge for letters, so 5.foo ends the float at the '.')
const graph_node float_dot{
    token_type::FLOAT_LITERAL,
    edges({
        {is_digit, &float_fraction},
        {is('.'), &reject},
    }),
};

// '.' then a digit is a float: .5
const graph_node access{token_type::ACCESS, edges({{is_digit, &float_fraction}})};

const graph_node integer{
    token_type::INT_LITERAL,
    edges({
        {is_digit, &integer},
        {is('.'), &float_dot},
        {is_identifier_char, &reject},
    }),
};

const graph_node sixteen{
    token_type::INT_LITERAL,
    edges({
        {is('b'), &hexadecimal_prefix},
        {is_digit, &integer},
        {is('.'), &float_dot},
        {is_identifier_char, &reject},
    }),
};

const graph_node one{
    token_type::INT_LITERAL,
    edges({
        {is('6'), &sixteen},
        {is_digit, &integer},
        {is('.'), &float_dot},
        {is_identifier_char, &reject},
    }),
};

const graph_node two{
    token_type::INT_LITERAL,
    edges({
        {is('b'), &binary_prefix},
        {is_digit, &integer},
        {is('.'), &float_dot},
        {is_identifier_char, &reject},
    }),
};

// ---- char and string literals ----
// A backslash moves to an escape node, which accepts one escape char and goes back to the body.
// So an escaped quote is part of the literal instead of closing it.

// exactly one char or one escape between the quotes: 'a' '\n' '\''
const graph_node char_close{token_type::CHAR_LITERAL, {}};
const graph_node char_body{std::nullopt, edges({{is('\''), &char_close}})};
const graph_node char_escape{std::nullopt, edges({{is_escape_char, &char_body}})};
const graph_node char_open{
    std::nullopt,
    edges({
        {is('\\'), &char_escape},
        // '' and ''' are errors
        {none_of("'\n"), &char_body},
    }),
};

// string_body and string_escape point at each other, so one needs declaring first
const graph_node string_close{token_type::STRING_LITERAL, {}};
extern const graph_node string_body;
const graph_node string_escape{std::nullopt, edges({{is_escape_char, &string_body}})};
const graph_node string_body{
    std::nullopt,
    edges({
        {is('\\'), &string_escape}, // before the catch-all below, which would also match '\'
        {is('"'), &string_close},
        {is_not('"'), &string_body},
    }),
};

// ---- comments and whitespace ----

const graph_node comment{token_type::COMMENT, edges({{is_not('\n'), &comment}})};
const graph_node whitespace{token_type::WHITESPACE, edges({{is_whitespace, &whitespace}})};

// ---- operators ----

const graph_node pointer{token_type::POINTER, {}};
const graph_node logical_and{token_type::LOGICAL_AND, {}};
const graph_node logical_or{token_type::LOGICAL_OR, {}};
const graph_node not_equal{token_type::NOT_EQUAL, {}};
const graph_node equal{token_type::EQUAL, {}};
const graph_node dereference{token_type::DEREFERENCE, {}};
const graph_node less_or_equal{token_type::LESS_OR_EQUAL, {}};
const graph_node left_shift{token_type::LEFT_SHIFT, {}};
const graph_node greater_or_equal{token_type::GREATER_OR_EQUAL, {}};
const graph_node right_shift{token_type::RIGHT_SHIFT, {}};
const graph_node plus{token_type::PLUS, {}};
const graph_node multiply{token_type::MULTIPLY, {}};
const graph_node divide{token_type::DIVIDE, {}};
const graph_node divide_floor{token_type::DIVIDE_FLOOR, {}};
const graph_node exponent{token_type::EXPONENT, {}};
const graph_node xor_operator{token_type::XOR, {}};
const graph_node address{token_type::ADDRESS, {}};

const graph_node minus{token_type::MINUS, edges({{is('>'), &pointer}})};
const graph_node bit_and{token_type::BIT_AND, edges({{is('&'), &logical_and}})};
const graph_node bit_or{token_type::BIT_OR, edges({{is('|'), &logical_or}})};
const graph_node not_operator{token_type::NOT, edges({{is('='), &not_equal}})};
const graph_node assign{token_type::ASSIGN, edges({{is('='), &equal}})};

const graph_node lesser{
    token_type::LESSER,
    edges({
        {is('-'), &dereference},
        {is('='), &less_or_equal},
        {is('<'), &left_shift},
    }),
};

const graph_node greater{
    token_type::GREATER,
    edges({
        {is('='), &greater_or_equal},
        {is('>'), &right_shift},
    }),
};

// ---- punctuators ----

const graph_node curly_open{token_type::CURLY_OPEN, {}};
const graph_node curly_close{token_type::CURLY_CLOSE, {}};
const graph_node parentheses_open{token_type::PARENTHESES_OPEN, {}};
const graph_node parentheses_close{token_type::PARENTHESES_CLOSE, {}};
const graph_node square_open{token_type::SQUARE_OPEN, {}};
const graph_node square_close{token_type::SQUARE_CLOSE, {}};
const graph_node semi_colon{token_type::SEMI_COLON, {}};
const graph_node colon{token_type::COLON, {}};
const graph_node comma{token_type::COMMA, {}};

// ---- start ----

const graph_node start{
    std::nullopt,
    edges({
        // keywords
        {is('b'), &kw_prefix_b},
        {is('c'), &kw_prefix_c},
        {is('d'), &kw_prefix_d},
        {is('e'), &kw_prefix_e},
        {is('f'), &kw_prefix_f},
        {is('i'), &kw_prefix_i},
        {is('l'), &kw_prefix_l},
        {is('n'), &kw_prefix_n},
        {is('r'), &kw_prefix_r},
        {is('s'), &kw_prefix_s},
        {is('t'), &kw_prefix_t},
        {is('u'), &kw_prefix_u},
        {is('w'), &kw_prefix_w},
        {is_letter, &identifier},
        {is('_'), &identifier},

        // numbers
        {is('1'), &one},
        {is('2'), &two},
        {is_digit, &integer},
        {is('.'), &access},

        // literals, comments, whitespace
        {is('\''), &char_open},
        {is('"'), &string_body},
        {is('#'), &comment},
        {is_whitespace, &whitespace},

        // operators
        {is('+'), &plus},
        {is('*'), &multiply},
        {is('/'), &divide},
        {is('\\'), &divide_floor},
        {is('~'), &exponent},
        {is('^'), &xor_operator},
        {is('@'), &address},
        {is('-'), &minus},
        {is('&'), &bit_and},
        {is('|'), &bit_or},
        {is('!'), &not_operator},
        {is('='), &assign},
        {is('<'), &lesser},
        {is('>'), &greater},

        // punctuators
        {is('{'), &curly_open},
        {is('}'), &curly_close},
        {is('('), &parentheses_open},
        {is(')'), &parentheses_close},
        {is('['), &square_open},
        {is(']'), &square_close},
        {is(';'), &semi_colon},
        {is(':'), &colon},
        {is(','), &comma},
    }),
};

// NOLINTEND(bugprone-throwing-static-initialization)

} // namespace

const graph_node *const dfa_start = &start;
const graph_node *const dfa_reject = &reject;
