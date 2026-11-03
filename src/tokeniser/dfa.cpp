#include "dfa.h"

namespace {

bool is_letter(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
bool is_digit(char c) {
    return c >= '0' && c <= '9';
}
bool is_binary_digit(char c) {
    return c == '0' || c == '1';
}
bool is_hexadecimal_digit(char c) {
    return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
bool is_identifier_char(char c) {
    return is_letter(c) || is_digit(c) || c == '_';
}
bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
bool not_newline(char c) {
    return c != '\n';
}
bool not_quote(char c) {
    return c != '"';
}

template <char C> bool is(char c) {
    return c == C;
}

// Nodes are defined before anything that points to them.

// ---- identifiers ----

const graph_node identifier{
    token_type::IDENTIFIER,
    {
        {is_identifier_char, &identifier},
    },
};

// ---- keywords ----
// one node per prefix, any other identifier char falls back to identifier

// bool
const graph_node kw_bool{
    token_type::BOOL,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_boo{
    token_type::IDENTIFIER,
    {
        {is<'l'>, &kw_bool},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_bo{
    token_type::IDENTIFIER,
    {
        {is<'o'>, &kw_prefix_boo},
        {is_identifier_char, &identifier},
    },
};

// byte
const graph_node kw_byte{
    token_type::BYTE,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_byt{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_byte},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_by{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_prefix_byt},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_b{
    token_type::IDENTIFIER,
    {
        {is<'o'>, &kw_prefix_bo},
        {is<'y'>, &kw_prefix_by},
        {is_identifier_char, &identifier},
    },
};

// char
const graph_node kw_char{
    token_type::CHAR,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_cha{
    token_type::IDENTIFIER,
    {
        {is<'r'>, &kw_char},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_ch{
    token_type::IDENTIFIER,
    {
        {is<'a'>, &kw_prefix_cha},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_c{
    token_type::IDENTIFIER,
    {
        {is<'h'>, &kw_prefix_ch},
        {is_identifier_char, &identifier},
    },
};

// define
const graph_node kw_define{
    token_type::DEFINE,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_defin{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_define},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_defi{
    token_type::IDENTIFIER,
    {
        {is<'n'>, &kw_prefix_defin},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_def{
    token_type::IDENTIFIER,
    {
        {is<'i'>, &kw_prefix_defi},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_de{
    token_type::IDENTIFIER,
    {
        {is<'f'>, &kw_prefix_def},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_d{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_prefix_de},
        {is_identifier_char, &identifier},
    },
};

// elif
const graph_node kw_elif{
    token_type::ELIF,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_eli{
    token_type::IDENTIFIER,
    {
        {is<'f'>, &kw_elif},
        {is_identifier_char, &identifier},
    },
};

// else
const graph_node kw_else{
    token_type::ELSE,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_els{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_else},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_el{
    token_type::IDENTIFIER,
    {
        {is<'i'>, &kw_prefix_eli},
        {is<'s'>, &kw_prefix_els},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_e{
    token_type::IDENTIFIER,
    {
        {is<'l'>, &kw_prefix_el},
        {is_identifier_char, &identifier},
    },
};

// false
const graph_node kw_false{
    token_type::BOOL_LITERAL,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_fals{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_false},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_fal{
    token_type::IDENTIFIER,
    {
        {is<'s'>, &kw_prefix_fals},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_fa{
    token_type::IDENTIFIER,
    {
        {is<'l'>, &kw_prefix_fal},
        {is_identifier_char, &identifier},
    },
};

// float
const graph_node kw_float{
    token_type::FLOAT,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_floa{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_float},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_flo{
    token_type::IDENTIFIER,
    {
        {is<'a'>, &kw_prefix_floa},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_fl{
    token_type::IDENTIFIER,
    {
        {is<'o'>, &kw_prefix_flo},
        {is_identifier_char, &identifier},
    },
};

// fn
const graph_node kw_fn{
    token_type::FN,
    {
        {is_identifier_char, &identifier},
    },
};

// for
const graph_node kw_for{
    token_type::FOR,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_fo{
    token_type::IDENTIFIER,
    {
        {is<'r'>, &kw_for},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_f{
    token_type::IDENTIFIER,
    {
        {is<'a'>, &kw_prefix_fa},
        {is<'l'>, &kw_prefix_fl},
        {is<'n'>, &kw_fn},
        {is<'o'>, &kw_prefix_fo},
        {is_identifier_char, &identifier},
    },
};

// if
const graph_node kw_if{
    token_type::IF,
    {
        {is_identifier_char, &identifier},
    },
};

// impl
const graph_node kw_impl{
    token_type::IMPL,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_imp{
    token_type::IDENTIFIER,
    {
        {is<'l'>, &kw_impl},
        {is_identifier_char, &identifier},
    },
};

// imut
const graph_node kw_imut{
    token_type::IMUT,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_imu{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_imut},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_im{
    token_type::IDENTIFIER,
    {
        {is<'p'>, &kw_prefix_imp},
        {is<'u'>, &kw_prefix_imu},
        {is_identifier_char, &identifier},
    },
};

// int
const graph_node kw_int{
    token_type::INT,
    {
        {is_identifier_char, &identifier},
    },
};

// inv
const graph_node kw_inv{
    token_type::INV,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_in{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_int},
        {is<'v'>, &kw_inv},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_i{
    token_type::IDENTIFIER,
    {
        {is<'f'>, &kw_if},
        {is<'m'>, &kw_prefix_im},
        {is<'n'>, &kw_prefix_in},
        {is_identifier_char, &identifier},
    },
};

// let
const graph_node kw_let{
    token_type::LET,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_le{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_let},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_l{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_prefix_le},
        {is_identifier_char, &identifier},
    },
};

// new
const graph_node kw_new{
    token_type::NEW,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_ne{
    token_type::IDENTIFIER,
    {
        {is<'w'>, &kw_new},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_n{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_prefix_ne},
        {is_identifier_char, &identifier},
    },
};

// print
const graph_node kw_print{
    token_type::PRINT,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_prin{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_print},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_pri{
    token_type::IDENTIFIER,
    {
        {is<'n'>, &kw_prefix_prin},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_pr{
    token_type::IDENTIFIER,
    {
        {is<'i'>, &kw_prefix_pri},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_p{
    token_type::IDENTIFIER,
    {
        {is<'r'>, &kw_prefix_pr},
        {is_identifier_char, &identifier},
    },
};

// reject
const graph_node kw_reject{
    token_type::REJECT,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_rejec{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_reject},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_reje{
    token_type::IDENTIFIER,
    {
        {is<'c'>, &kw_prefix_rejec},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_rej{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_prefix_reje},
        {is_identifier_char, &identifier},
    },
};

// return
const graph_node kw_return{
    token_type::RETURN,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_retur{
    token_type::IDENTIFIER,
    {
        {is<'n'>, &kw_return},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_retu{
    token_type::IDENTIFIER,
    {
        {is<'r'>, &kw_prefix_retur},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_ret{
    token_type::IDENTIFIER,
    {
        {is<'u'>, &kw_prefix_retu},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_re{
    token_type::IDENTIFIER,
    {
        {is<'j'>, &kw_prefix_rej},
        {is<'t'>, &kw_prefix_ret},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_r{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_prefix_re},
        {is_identifier_char, &identifier},
    },
};

// scan
const graph_node kw_scan{
    token_type::SCAN,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_sca{
    token_type::IDENTIFIER,
    {
        {is<'n'>, &kw_scan},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_sc{
    token_type::IDENTIFIER,
    {
        {is<'a'>, &kw_prefix_sca},
        {is_identifier_char, &identifier},
    },
};

// state
const graph_node kw_state{
    token_type::STATE,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_stat{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_state},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_sta{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_prefix_stat},
        {is_identifier_char, &identifier},
    },
};

// step
const graph_node kw_step{
    token_type::STEP,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_ste{
    token_type::IDENTIFIER,
    {
        {is<'p'>, &kw_step},
        {is_identifier_char, &identifier},
    },
};

// struct
const graph_node kw_struct{
    token_type::STRUCT,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_struc{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_struct},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_stru{
    token_type::IDENTIFIER,
    {
        {is<'c'>, &kw_prefix_struc},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_str{
    token_type::IDENTIFIER,
    {
        {is<'u'>, &kw_prefix_stru},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_st{
    token_type::IDENTIFIER,
    {
        {is<'a'>, &kw_prefix_sta},
        {is<'e'>, &kw_prefix_ste},
        {is<'r'>, &kw_prefix_str},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_s{
    token_type::IDENTIFIER,
    {
        {is<'c'>, &kw_prefix_sc},
        {is<'t'>, &kw_prefix_st},
        {is_identifier_char, &identifier},
    },
};

// this
const graph_node kw_this{
    token_type::THIS,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_thi{
    token_type::IDENTIFIER,
    {
        {is<'s'>, &kw_this},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_th{
    token_type::IDENTIFIER,
    {
        {is<'i'>, &kw_prefix_thi},
        {is_identifier_char, &identifier},
    },
};

// true
const graph_node kw_true{
    token_type::BOOL_LITERAL,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_tru{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_true},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_tr{
    token_type::IDENTIFIER,
    {
        {is<'u'>, &kw_prefix_tru},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_t{
    token_type::IDENTIFIER,
    {
        {is<'h'>, &kw_prefix_th},
        {is<'r'>, &kw_prefix_tr},
        {is_identifier_char, &identifier},
    },
};

// update
const graph_node kw_update{
    token_type::UPDATE,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_updat{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_update},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_upda{
    token_type::IDENTIFIER,
    {
        {is<'t'>, &kw_prefix_updat},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_upd{
    token_type::IDENTIFIER,
    {
        {is<'a'>, &kw_prefix_upda},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_up{
    token_type::IDENTIFIER,
    {
        {is<'d'>, &kw_prefix_upd},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_u{
    token_type::IDENTIFIER,
    {
        {is<'p'>, &kw_prefix_up},
        {is_identifier_char, &identifier},
    },
};

// while
const graph_node kw_while{
    token_type::WHILE,
    {
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_whil{
    token_type::IDENTIFIER,
    {
        {is<'e'>, &kw_while},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_whi{
    token_type::IDENTIFIER,
    {
        {is<'l'>, &kw_prefix_whil},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_wh{
    token_type::IDENTIFIER,
    {
        {is<'i'>, &kw_prefix_whi},
        {is_identifier_char, &identifier},
    },
};

const graph_node kw_prefix_w{
    token_type::IDENTIFIER,
    {
        {is<'h'>, &kw_prefix_wh},
        {is_identifier_char, &identifier},
    },
};

// ---- numbers ----

const graph_node hexadecimal{
    token_type::BASE_16_LITERAL,
    {
        {is_hexadecimal_digit, &hexadecimal},
    },
};

// 16b
const graph_node hexadecimal_prefix{
    std::nullopt,
    {
        {is_hexadecimal_digit, &hexadecimal},
    },
};

const graph_node binary{
    token_type::BASE_2_LITERAL,
    {
        {is_binary_digit, &binary},
    },
};

// 2b
const graph_node binary_prefix{
    std::nullopt,
    {
        {is_binary_digit, &binary},
    },
};

const graph_node float_literal{
    token_type::FLOAT_LITERAL,
    {
        {is_digit, &float_literal},
    },
};

const graph_node access{token_type::ACCESS, {}};

const graph_node integer{
    token_type::INT_LITERAL,
    {
        {is_digit, &integer},
        {is<'.'>, &float_literal},
    },
};

const graph_node sixteen{
    token_type::INT_LITERAL,
    {
        {is<'b'>, &hexadecimal_prefix},
        {is_digit, &integer},
        {is<'.'>, &float_literal},
    },
};

const graph_node one{
    token_type::INT_LITERAL,
    {
        {is<'6'>, &sixteen},
        {is_digit, &integer},
        {is<'.'>, &float_literal},
    },
};

const graph_node two{
    token_type::INT_LITERAL,
    {
        {is<'b'>, &binary_prefix},
        {is_digit, &integer},
        {is<'.'>, &float_literal},
    },
};

// ---- char and string literals ----

const graph_node char_close{token_type::CHAR_LITERAL, {}};

const graph_node char_body{
    std::nullopt,
    {
        {is<'\''>, &char_close},
    },
};

const graph_node char_open{
    std::nullopt,
    {
        {not_newline, &char_body},
    },
};

const graph_node string_close{token_type::STRING_LITERAL, {}};

const graph_node string_body{
    std::nullopt,
    {
        {not_quote, &string_body},
        {is<'"'>, &string_close},
    },
};

// ---- comments and whitespace ----

const graph_node comment{
    token_type::COMMENT,
    {
        {not_newline, &comment},
    },
};

const graph_node whitespace{
    token_type::WHITESPACE,
    {
        {is_whitespace, &whitespace},
    },
};

// ---- operators ----

const graph_node pointer{token_type::POINTER, {}};

const graph_node logical_and{token_type::LOGICAL_AND, {}};

const graph_node logical_or{token_type::LOGICAL_OR, {}};

const graph_node not_equal{token_type::NOT_EQUAL, {}};

const graph_node equal{token_type::EQUAL, {}};

const graph_node dereference{token_type::DEREFERENCE, {}};

const graph_node less_or_equal{token_type::LESS_OR_EQUAL, {}};

const graph_node left_shift{token_type::LEFT_SHIT, {}};

const graph_node greater_or_equal{token_type::GREATER_OR_EQUAL, {}};

const graph_node right_shift{token_type::RIGHT_SHIFT, {}};

const graph_node plus{token_type::PLUS, {}};

const graph_node multiply{token_type::MULTIPLY, {}};

const graph_node divide{token_type::DIVIDE, {}};

const graph_node divide_floor{token_type::DIVIDE_FLOOR, {}};

const graph_node exponent{token_type::EXPONENT, {}};

const graph_node xor_operator{token_type::XOR, {}};

const graph_node address{token_type::ADDRESS, {}};

const graph_node minus{
    token_type::MINUS,
    {
        {is<'>'>, &pointer},
    },
};

const graph_node bool_and{
    token_type::BOOL_AND,
    {
        {is<'&'>, &logical_and},
    },
};

const graph_node bool_or{
    token_type::BOOL_OR,
    {
        {is<'|'>, &logical_or},
    },
};

const graph_node not_operator{
    token_type::NOT,
    {
        {is<'='>, &not_equal},
    },
};

const graph_node assign{
    token_type::ASSIGN,
    {
        {is<'='>, &equal},
    },
};

const graph_node lesser{
    token_type::LESSER,
    {
        {is<'-'>, &dereference},
        {is<'='>, &less_or_equal},
        {is<'<'>, &left_shift},
    },
};

const graph_node greater{
    token_type::GREATER,
    {
        {is<'='>, &greater_or_equal},
        {is<'>'>, &right_shift},
    },
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

const graph_node start{
    std::nullopt,
    {
        {is<'b'>, &kw_prefix_b},
        {is<'c'>, &kw_prefix_c},
        {is<'d'>, &kw_prefix_d},
        {is<'e'>, &kw_prefix_e},
        {is<'f'>, &kw_prefix_f},
        {is<'i'>, &kw_prefix_i},
        {is<'l'>, &kw_prefix_l},
        {is<'n'>, &kw_prefix_n},
        {is<'p'>, &kw_prefix_p},
        {is<'r'>, &kw_prefix_r},
        {is<'s'>, &kw_prefix_s},
        {is<'t'>, &kw_prefix_t},
        {is<'u'>, &kw_prefix_u},
        {is<'w'>, &kw_prefix_w},
        {is_letter, &identifier},
        {is<'1'>, &one},
        {is<'2'>, &two},
        {is_digit, &integer},
        {is<'.'>, &access},
        {is<'\''>, &char_open},
        {is<'"'>, &string_body},
        {is<'#'>, &comment},
        {is_whitespace, &whitespace},
        {is<'+'>, &plus},
        {is<'*'>, &multiply},
        {is<'/'>, &divide},
        {is<'\\'>, &divide_floor},
        {is<'~'>, &exponent},
        {is<'^'>, &xor_operator},
        {is<'@'>, &address},
        {is<'-'>, &minus},
        {is<'&'>, &bool_and},
        {is<'|'>, &bool_or},
        {is<'!'>, &not_operator},
        {is<'='>, &assign},
        {is<'<'>, &lesser},
        {is<'>'>, &greater},
        {is<'{'>, &curly_open},
        {is<'}'>, &curly_close},
        {is<'('>, &parentheses_open},
        {is<')'>, &parentheses_close},
        {is<'['>, &square_open},
        {is<']'>, &square_close},
        {is<';'>, &semi_colon},
        {is<':'>, &colon},
        {is<','>, &comma},
    },
};

} // namespace

const graph_node *const dfa_start = &start;
