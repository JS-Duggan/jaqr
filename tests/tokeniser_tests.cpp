#include "../src/tokeniser/tokeniser.h"
#include "../src/tokens/token.h"
#include "test_helpers.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <format>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using tt = token_type;

using testing::AllOf;
using testing::ElementsAre;
using testing::FieldsAre;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::StartsWith;
using testing::ThrowsMessage;

// the printers below are in the global namespace (not the anonymous one) because gtest finds
// them by argument dependent lookup, which only searches the namespace of token_type and token

static std::string_view name_of(const token_type &type) {
    switch (type) {
    case tt::INT:
        return "INT";
    case tt::BOOL:
        return "BOOL";
    case tt::CHAR:
        return "CHAR";
    case tt::BYTE:
        return "BYTE";
    case tt::FLOAT:
        return "FLOAT";
    case tt::IF:
        return "IF";
    case tt::ELIF:
        return "ELIF";
    case tt::ELSE:
        return "ELSE";
    case tt::WHILE:
        return "WHILE";
    case tt::FOR:
        return "FOR";
    case tt::RETURN:
        return "RETURN";
    case tt::STEP:
        return "STEP";
    case tt::REJECT:
        return "REJECT";
    case tt::INV:
        return "INV";
    case tt::UPDATE:
        return "UPDATE";
    case tt::LET:
        return "LET";
    case tt::IMUT:
        return "IMUT";
    case tt::STATE:
        return "STATE";
    case tt::DEFINE:
        return "DEFINE";
    case tt::FN:
        return "FN";
    case tt::STRUCT:
        return "STRUCT";
    case tt::IMPL:
        return "IMPL";
    case tt::THIS:
        return "THIS";
    case tt::NEW:
        return "NEW";
    case tt::IDENTIFIER:
        return "IDENTIFIER";
    case tt::PLUS:
        return "PLUS";
    case tt::MINUS:
        return "MINUS";
    case tt::MULTIPLY:
        return "MULTIPLY";
    case tt::DIVIDE:
        return "DIVIDE";
    case tt::DIVIDE_FLOOR:
        return "DIVIDE_FLOOR";
    case tt::EXPONENT:
        return "EXPONENT";
    case tt::XOR:
        return "XOR";
    case tt::BIT_AND:
        return "BIT_AND";
    case tt::BIT_OR:
        return "BIT_OR";
    case tt::LOGICAL_AND:
        return "LOGICAL_AND";
    case tt::LOGICAL_OR:
        return "LOGICAL_OR";
    case tt::NOT:
        return "NOT";
    case tt::ADDRESS:
        return "ADDRESS";
    case tt::DEREFERENCE:
        return "DEREFERENCE";
    case tt::POINTER:
        return "POINTER";
    case tt::ACCESS:
        return "ACCESS";
    case tt::EQUAL:
        return "EQUAL";
    case tt::ASSIGN:
        return "ASSIGN";
    case tt::NOT_EQUAL:
        return "NOT_EQUAL";
    case tt::LESSER:
        return "LESSER";
    case tt::GREATER:
        return "GREATER";
    case tt::LESS_OR_EQUAL:
        return "LESS_OR_EQUAL";
    case tt::GREATER_OR_EQUAL:
        return "GREATER_OR_EQUAL";
    case tt::LEFT_SHIFT:
        return "LEFT_SHIFT";
    case tt::RIGHT_SHIFT:
        return "RIGHT_SHIFT";
    case tt::INT_LITERAL:
        return "INT_LITERAL";
    case tt::FLOAT_LITERAL:
        return "FLOAT_LITERAL";
    case tt::CHAR_LITERAL:
        return "CHAR_LITERAL";
    case tt::BOOL_LITERAL:
        return "BOOL_LITERAL";
    case tt::BASE_2_LITERAL:
        return "BASE_2_LITERAL";
    case tt::BASE_16_LITERAL:
        return "BASE_16_LITERAL";
    case tt::STRING_LITERAL:
        return "STRING_LITERAL";
    case tt::CURLY_OPEN:
        return "CURLY_OPEN";
    case tt::CURLY_CLOSE:
        return "CURLY_CLOSE";
    case tt::PARENTHESES_OPEN:
        return "PARENTHESES_OPEN";
    case tt::PARENTHESES_CLOSE:
        return "PARENTHESES_CLOSE";
    case tt::SQUARE_OPEN:
        return "SQUARE_OPEN";
    case tt::SQUARE_CLOSE:
        return "SQUARE_CLOSE";
    case tt::SEMI_COLON:
        return "SEMI_COLON";
    case tt::COLON:
        return "COLON";
    case tt::COMMA:
        return "COMMA";
    case tt::WHITESPACE:
        return "WHITESPACE";
    case tt::COMMENT:
        return "COMMENT";
    }
    return "UNKNOWN";
}

// the enum has no printer, so without this a failure shows the raw byte
static void PrintTo(const token_type &type, std::ostream *os) {
    *os << name_of(type);
}

// PrintToString escapes the lexeme, so a '\n' inside one stays on one line of the failure
static void PrintTo(const token &tok, std::ostream *os) {
    *os << name_of(tok.type) << ' ' << testing::PrintToString(tok.lexeme) << " at " << tok.line
        << ':' << tok.column;
}

namespace {

// ---- helpers ----

std::vector<token> tokenise_source(std::string_view source) {
    return tokenise(to_lines(source));
}

std::vector<token_type> types_of(std::string_view source) {
    std::vector<token_type> types;
    for (const token &tok : tokenise_source(source)) {
        types.push_back(tok.type);
    }
    return types;
}

// the types the parser sees: it skips whitespace and comments
std::vector<token_type> significant_types_of(std::string_view source) {
    std::vector<token_type> types;
    for (const token &tok : tokenise_source(source)) {
        if (tok.type != tt::WHITESPACE && tok.type != tt::COMMENT) {
            types.push_back(tok.type);
        }
    }
    return types;
}

std::string joined_lexemes(const std::vector<token> &tokens) {
    std::string joined;
    for (const token &tok : tokens) {
        joined += tok.lexeme;
    }
    return joined;
}

// matches a lexer error that reports an unexpected character at the given 0-indexed position
auto lexer_error_at(int line, int column) {
    return ThrowsMessage<std::runtime_error>(
        AllOf(StartsWith("LEX001:"), HasSubstr("unexpected character"),
              HasSubstr(std::format("at line {}, column {}", line, column))));
}

// matches a lexer error for input that ends inside the token starting at the given position
auto unfinished_token_error(int start_line, int start_column) {
    return ThrowsMessage<std::runtime_error>(
        AllOf(StartsWith("LEX002:"), HasSubstr("unexpected end of input"),
              HasSubstr(std::format("starts at line {}, column {}", start_line, start_column))));
}

// ---- single tokens ----
// each source must come back as exactly one token of the given type, covering the whole source

struct single_token_case {
    std::string_view source;
    token_type type;
};

void PrintTo(const single_token_case &test_case, std::ostream *os) {
    *os << testing::PrintToString(test_case.source) << " -> " << name_of(test_case.type);
}

class SingleToken : public testing::TestWithParam<single_token_case> {};

TEST_P(SingleToken, IsOneTokenOfTheExpectedType) {
    const single_token_case &test_case = GetParam();
    EXPECT_THAT(tokenise_source(test_case.source),
                ElementsAre(FieldsAre(test_case.type, std::string(test_case.source), 0, 0)));
}

// every keyword in the grammar
constexpr single_token_case keyword_cases[] = {
    {"int", tt::INT},           {"bool", tt::BOOL},
    {"char", tt::CHAR},         {"byte", tt::BYTE},
    {"float", tt::FLOAT},       {"if", tt::IF},
    {"elif", tt::ELIF},         {"else", tt::ELSE},
    {"while", tt::WHILE},       {"for", tt::FOR},
    {"return", tt::RETURN},     {"step", tt::STEP},
    {"reject", tt::REJECT},     {"inv", tt::INV},
    {"update", tt::UPDATE},     {"let", tt::LET},
    {"imut", tt::IMUT},         {"state", tt::STATE},
    {"define", tt::DEFINE},     {"fn", tt::FN},
    {"struct", tt::STRUCT},     {"impl", tt::IMPL},
    {"this", tt::THIS},         {"new", tt::NEW},
    {"true", tt::BOOL_LITERAL}, {"false", tt::BOOL_LITERAL},
};
INSTANTIATE_TEST_SUITE_P(Keywords, SingleToken, testing::ValuesIn(keyword_cases));

constexpr std::string_view identifier_sources[] = {
    // plain identifiers
    "x",
    "foo",
    "foo_bar",
    "_foo",
    "_",
    "__",
    "_1",
    "foo123",
    "a1b2",
    "FooBar",
    "camelCase",
    "SCREAMING_CASE",
    "Z",
    // standard library functions and main are not keywords
    "print",
    "scan",
    "main",
    "printf",
    "print_",
    "scanner",
    "mains",
    "Main",
    // keywords are case sensitive
    "If",
    "INT",
    "Int",
    "True",
    "FALSE",
    "Fn",
    "THIS",
    // every proper prefix of a keyword
    "b",
    "bo",
    "boo",
    "by",
    "byt",
    "c",
    "ch",
    "cha",
    "d",
    "de",
    "def",
    "defi",
    "defin",
    "e",
    "el",
    "eli",
    "els",
    "f",
    "fa",
    "fal",
    "fals",
    "fl",
    "flo",
    "floa",
    "fo",
    "i",
    "im",
    "imp",
    "imu",
    "in",
    "l",
    "le",
    "n",
    "ne",
    "r",
    "re",
    "rej",
    "reje",
    "rejec",
    "ret",
    "retu",
    "retur",
    "s",
    "st",
    "sta",
    "stat",
    "ste",
    "str",
    "stru",
    "struc",
    "t",
    "th",
    "thi",
    "tr",
    "tru",
    "u",
    "up",
    "upd",
    "upda",
    "updat",
    "w",
    "wh",
    "whi",
    "whil",
    // a keyword followed by more identifier chars
    "ints",
    "int_",
    "int1",
    "inte",
    "bools",
    "chars",
    "bytes",
    "floats",
    "iff",
    "ifs",
    "elifs",
    "elsewhere",
    "whiles",
    "form",
    "fork",
    "returns",
    "steps",
    "rejects",
    "invs",
    "updates",
    "lets",
    "imuts",
    "states",
    "defined",
    "fns",
    "structs",
    "impls",
    "thiss",
    "news",
    "truee",
    "falsey",
    "true1",
    "false_",
    // two keywords run together
    "ifelse",
    "intint",
    "letx",
};

std::vector<single_token_case> identifier_cases() {
    std::vector<single_token_case> cases;
    for (const std::string_view &source : identifier_sources) {
        cases.push_back({source, tt::IDENTIFIER});
    }
    return cases;
}
INSTANTIATE_TEST_SUITE_P(Identifiers, SingleToken, testing::ValuesIn(identifier_cases()));

// every operator in expression.txt and program.txt
constexpr single_token_case operator_cases[] = {
    {"+", tt::PLUS},
    {"-", tt::MINUS},
    {"*", tt::MULTIPLY},
    {"/", tt::DIVIDE},
    {"\\", tt::DIVIDE_FLOOR},
    {"~", tt::EXPONENT},
    {"^", tt::XOR},
    {"&", tt::BIT_AND},
    {"|", tt::BIT_OR},
    {"&&", tt::LOGICAL_AND},
    {"||", tt::LOGICAL_OR},
    {"!", tt::NOT},
    {"@", tt::ADDRESS},
    {"<-", tt::DEREFERENCE},
    {"->", tt::POINTER},
    {".", tt::ACCESS},
    {"==", tt::EQUAL},
    {"=", tt::ASSIGN},
    {"!=", tt::NOT_EQUAL},
    {"<", tt::LESSER},
    {">", tt::GREATER},
    {"<=", tt::LESS_OR_EQUAL},
    {">=", tt::GREATER_OR_EQUAL},
    {"<<", tt::LEFT_SHIFT},
    {">>", tt::RIGHT_SHIFT},
};
INSTANTIATE_TEST_SUITE_P(Operators, SingleToken, testing::ValuesIn(operator_cases));

constexpr single_token_case punctuator_cases[] = {
    {"{", tt::CURLY_OPEN},        {"}", tt::CURLY_CLOSE}, {"(", tt::PARENTHESES_OPEN},
    {")", tt::PARENTHESES_CLOSE}, {"[", tt::SQUARE_OPEN}, {"]", tt::SQUARE_CLOSE},
    {";", tt::SEMI_COLON},        {":", tt::COLON},       {",", tt::COMMA},
};
INSTANTIATE_TEST_SUITE_P(Punctuators, SingleToken, testing::ValuesIn(punctuator_cases));

constexpr single_token_case number_cases[] = {
    {"0", tt::INT_LITERAL},
    {"7", tt::INT_LITERAL},
    {"42", tt::INT_LITERAL},
    {"007", tt::INT_LITERAL},
    {"1234567890", tt::INT_LITERAL},
    // 1, 2 and 16 start the base prefixes, but are plain ints without the 'b'
    {"1", tt::INT_LITERAL},
    {"2", tt::INT_LITERAL},
    {"16", tt::INT_LITERAL},
    {"12", tt::INT_LITERAL},
    {"160", tt::INT_LITERAL},
    {"161", tt::INT_LITERAL},
    {"21", tt::INT_LITERAL},
    {"20", tt::INT_LITERAL},
    {"26", tt::INT_LITERAL},
    {"216", tt::INT_LITERAL},
    {"116", tt::INT_LITERAL},

    {"1.5", tt::FLOAT_LITERAL},
    {"0.0", tt::FLOAT_LITERAL},
    {"3.14159", tt::FLOAT_LITERAL},
    {"1.", tt::FLOAT_LITERAL},
    {".5", tt::FLOAT_LITERAL},
    {".05", tt::FLOAT_LITERAL},
    {"2.5", tt::FLOAT_LITERAL},
    {"2.", tt::FLOAT_LITERAL},
    {"16.5", tt::FLOAT_LITERAL},
    {"16.", tt::FLOAT_LITERAL},
    {"100.001", tt::FLOAT_LITERAL},

    {"2b0", tt::BASE_2_LITERAL},
    {"2b1", tt::BASE_2_LITERAL},
    {"2b101", tt::BASE_2_LITERAL},
    {"2b00010110", tt::BASE_2_LITERAL},

    {"16b0", tt::BASE_16_LITERAL},
    {"16bff", tt::BASE_16_LITERAL},
    {"16bFF", tt::BASE_16_LITERAL},
    {"16bff14", tt::BASE_16_LITERAL},
    {"16bDeadBeef", tt::BASE_16_LITERAL},
    {"16b0123456789abcdefABCDEF", tt::BASE_16_LITERAL},
    // 'b' is a hex digit as well as the prefix marker
    {"16bb", tt::BASE_16_LITERAL},
};
INSTANTIATE_TEST_SUITE_P(Numbers, SingleToken, testing::ValuesIn(number_cases));

constexpr single_token_case char_cases[] = {
    {"'a'", tt::CHAR_LITERAL},
    {"'Z'", tt::CHAR_LITERAL},
    {"'0'", tt::CHAR_LITERAL},
    {"' '", tt::CHAR_LITERAL},
    {"'\t'", tt::CHAR_LITERAL},
    {"'\"'", tt::CHAR_LITERAL},
    {"'#'", tt::CHAR_LITERAL},
    {"'$'", tt::CHAR_LITERAL},
    // every escape
    {R"('\n')", tt::CHAR_LITERAL},
    {R"('\r')", tt::CHAR_LITERAL},
    {R"('\t')", tt::CHAR_LITERAL},
    {R"('\0')", tt::CHAR_LITERAL},
    {R"('\\')", tt::CHAR_LITERAL},
    {R"('\'')", tt::CHAR_LITERAL},
    {R"('\"')", tt::CHAR_LITERAL},
};
INSTANTIATE_TEST_SUITE_P(Chars, SingleToken, testing::ValuesIn(char_cases));

constexpr single_token_case string_cases[] = {
    {R"("")", tt::STRING_LITERAL},
    {R"("hello")", tt::STRING_LITERAL},
    {R"("hello, world!")", tt::STRING_LITERAL},
    {R"("a\nb\rc\td\0e")", tt::STRING_LITERAL},
    {R"("\\")", tt::STRING_LITERAL},
    {R"("\"")", tt::STRING_LITERAL},
    {R"("say \"hi\" now")", tt::STRING_LITERAL},
    {R"("\'")", tt::STRING_LITERAL},
    {R"("ends with a backslash \\")", tt::STRING_LITERAL},
    // chars that mean something outside a string
    {R"("it's")", tt::STRING_LITERAL},
    {R"("# not a comment")", tt::STRING_LITERAL},
    {R"("$ ? ` %")", tt::STRING_LITERAL},
    {R"("if (x) { return 1; }")", tt::STRING_LITERAL},
};
INSTANTIATE_TEST_SUITE_P(Strings, SingleToken, testing::ValuesIn(string_cases));

// ---- token sequences ----
// maximal munch over adjacent tokens. every case is also checked to round-trip.

struct sequence_case {
    std::string_view source;
    std::vector<token_type> types;
};

void PrintTo(const sequence_case &test_case, std::ostream *os) {
    *os << testing::PrintToString(test_case.source);
}

class TokenSequence : public testing::TestWithParam<sequence_case> {};

TEST_P(TokenSequence, HasTheExpectedTypes) {
    const sequence_case &test_case = GetParam();
    const std::vector<token> tokens = tokenise_source(test_case.source);

    std::vector<token_type> types;
    for (const token &tok : tokens) {
        types.push_back(tok.type);
    }

    EXPECT_EQ(types, test_case.types);
    EXPECT_EQ(joined_lexemes(tokens), test_case.source);
}

INSTANTIATE_TEST_SUITE_P(OperatorMunch, TokenSequence,
                         testing::ValuesIn(std::vector<sequence_case>{
                             {"<<=", {tt::LEFT_SHIFT, tt::ASSIGN}},
                             {"<<<", {tt::LEFT_SHIFT, tt::LESSER}},
                             {"<<<<", {tt::LEFT_SHIFT, tt::LEFT_SHIFT}},
                             {"<<-", {tt::LEFT_SHIFT, tt::MINUS}},
                             {"<--", {tt::DEREFERENCE, tt::MINUS}},
                             {"<-<-", {tt::DEREFERENCE, tt::DEREFERENCE}},
                             {"<->", {tt::DEREFERENCE, tt::GREATER}},
                             {"<-=", {tt::DEREFERENCE, tt::ASSIGN}},
                             {"<==", {tt::LESS_OR_EQUAL, tt::ASSIGN}},
                             {"<=-", {tt::LESS_OR_EQUAL, tt::MINUS}},
                             {"-->", {tt::MINUS, tt::POINTER}},
                             {"->>", {tt::POINTER, tt::GREATER}},
                             {"->->", {tt::POINTER, tt::POINTER}},
                             {"--", {tt::MINUS, tt::MINUS}},
                             {"&&&", {tt::LOGICAL_AND, tt::BIT_AND}},
                             {"&&&&", {tt::LOGICAL_AND, tt::LOGICAL_AND}},
                             {"|||", {tt::LOGICAL_OR, tt::BIT_OR}},
                             {"||||", {tt::LOGICAL_OR, tt::LOGICAL_OR}},
                             {"&|", {tt::BIT_AND, tt::BIT_OR}},
                             {"===", {tt::EQUAL, tt::ASSIGN}},
                             {"====", {tt::EQUAL, tt::EQUAL}},
                             {"!==", {tt::NOT_EQUAL, tt::ASSIGN}},
                             {"!!", {tt::NOT, tt::NOT}},
                             {"!!=", {tt::NOT, tt::NOT_EQUAL}},
                             {">>=", {tt::RIGHT_SHIFT, tt::ASSIGN}},
                             {">>>", {tt::RIGHT_SHIFT, tt::GREATER}},
                             {">==", {tt::GREATER_OR_EQUAL, tt::ASSIGN}},
                             {"=<", {tt::ASSIGN, tt::LESSER}},
                             {"=>", {tt::ASSIGN, tt::GREATER}},
                             {"><", {tt::GREATER, tt::LESSER}},
                             {">-", {tt::GREATER, tt::MINUS}},
                             {"+-*/\\~^@",
                              {tt::PLUS, tt::MINUS, tt::MULTIPLY, tt::DIVIDE, tt::DIVIDE_FLOOR,
                               tt::EXPONENT, tt::XOR, tt::ADDRESS}},
                             {"(){}[];:,",
                              {tt::PARENTHESES_OPEN, tt::PARENTHESES_CLOSE, tt::CURLY_OPEN,
                               tt::CURLY_CLOSE, tt::SQUARE_OPEN, tt::SQUARE_CLOSE, tt::SEMI_COLON,
                               tt::COLON, tt::COMMA}},
                         }));

INSTANTIATE_TEST_SUITE_P(
    AdjacentTokens, TokenSequence,
    testing::ValuesIn(std::vector<sequence_case>{
        // "<-" is always one token, the parser splits it after an operand
        {"x<-1", {tt::IDENTIFIER, tt::DEREFERENCE, tt::INT_LITERAL}},
        {"x< -1", {tt::IDENTIFIER, tt::LESSER, tt::WHITESPACE, tt::MINUS, tt::INT_LITERAL}},
        {"x<=-1", {tt::IDENTIFIER, tt::LESS_OR_EQUAL, tt::MINUS, tt::INT_LITERAL}},
        {"<-p", {tt::DEREFERENCE, tt::IDENTIFIER}},
        {"<-<-p", {tt::DEREFERENCE, tt::DEREFERENCE, tt::IDENTIFIER}},
        {"@<-p", {tt::ADDRESS, tt::DEREFERENCE, tt::IDENTIFIER}},
        {"a->b", {tt::IDENTIFIER, tt::POINTER, tt::IDENTIFIER}},
        {"a-b", {tt::IDENTIFIER, tt::MINUS, tt::IDENTIFIER}},
        {"a--b", {tt::IDENTIFIER, tt::MINUS, tt::MINUS, tt::IDENTIFIER}},
        {"a>-b", {tt::IDENTIFIER, tt::GREATER, tt::MINUS, tt::IDENTIFIER}},
        {"-1", {tt::MINUS, tt::INT_LITERAL}},
        {"-1.5", {tt::MINUS, tt::FLOAT_LITERAL}},
        {"1+2", {tt::INT_LITERAL, tt::PLUS, tt::INT_LITERAL}},
        {"1-2", {tt::INT_LITERAL, tt::MINUS, tt::INT_LITERAL}},
        {"2~-1", {tt::INT_LITERAL, tt::EXPONENT, tt::MINUS, tt::INT_LITERAL}},
        {"2b101+16bff", {tt::BASE_2_LITERAL, tt::PLUS, tt::BASE_16_LITERAL}},
        {"16bff;", {tt::BASE_16_LITERAL, tt::SEMI_COLON}},
        {"2b1)", {tt::BASE_2_LITERAL, tt::PARENTHESES_CLOSE}},
        {"1.5+.5", {tt::FLOAT_LITERAL, tt::PLUS, tt::FLOAT_LITERAL}},
        {"1.;", {tt::FLOAT_LITERAL, tt::SEMI_COLON}},
        {"a\\b/c*d",
         {tt::IDENTIFIER, tt::DIVIDE_FLOOR, tt::IDENTIFIER, tt::DIVIDE, tt::IDENTIFIER,
          tt::MULTIPLY, tt::IDENTIFIER}},
        {"a&b|c^d",
         {tt::IDENTIFIER, tt::BIT_AND, tt::IDENTIFIER, tt::BIT_OR, tt::IDENTIFIER, tt::XOR,
          tt::IDENTIFIER}},
        {"a&&b||!c",
         {tt::IDENTIFIER, tt::LOGICAL_AND, tt::IDENTIFIER, tt::LOGICAL_OR, tt::NOT,
          tt::IDENTIFIER}},
        {"a==b!=c", {tt::IDENTIFIER, tt::EQUAL, tt::IDENTIFIER, tt::NOT_EQUAL, tt::IDENTIFIER}},
        {"a<<b>>c",
         {tt::IDENTIFIER, tt::LEFT_SHIFT, tt::IDENTIFIER, tt::RIGHT_SHIFT, tt::IDENTIFIER}},
        {"p.x", {tt::IDENTIFIER, tt::ACCESS, tt::IDENTIFIER}},
        {"a.b.c", {tt::IDENTIFIER, tt::ACCESS, tt::IDENTIFIER, tt::ACCESS, tt::IDENTIFIER}},
        {"this.x", {tt::THIS, tt::ACCESS, tt::IDENTIFIER}},
        {"xs[0].y",
         {tt::IDENTIFIER, tt::SQUARE_OPEN, tt::INT_LITERAL, tt::SQUARE_CLOSE, tt::ACCESS,
          tt::IDENTIFIER}},
        {"f(s)(x)",
         {tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::PARENTHESES_CLOSE,
          tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::PARENTHESES_CLOSE}},
        {"[3]->int", {tt::SQUARE_OPEN, tt::INT_LITERAL, tt::SQUARE_CLOSE, tt::POINTER, tt::INT}},
        {"->[3]int", {tt::POINTER, tt::SQUARE_OPEN, tt::INT_LITERAL, tt::SQUARE_CLOSE, tt::INT}},
        {"x:int=1;",
         {tt::IDENTIFIER, tt::COLON, tt::INT, tt::ASSIGN, tt::INT_LITERAL, tt::SEMI_COLON}},
        {"float(x)", {tt::FLOAT, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::PARENTHESES_CLOSE}},
        {"if(x)", {tt::IF, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::PARENTHESES_CLOSE}},
        {"else{", {tt::ELSE, tt::CURLY_OPEN}},
        {"return;", {tt::RETURN, tt::SEMI_COLON}},
        {"int x", {tt::INT, tt::WHITESPACE, tt::IDENTIFIER}},
        {"!true&&false", {tt::NOT, tt::BOOL_LITERAL, tt::LOGICAL_AND, tt::BOOL_LITERAL}},
        {"print(x);",
         {tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::PARENTHESES_CLOSE,
          tt::SEMI_COLON}},
        {R"("a""b")", {tt::STRING_LITERAL, tt::STRING_LITERAL}},
        {"'a''b'", {tt::CHAR_LITERAL, tt::CHAR_LITERAL}},
        {R"("a"'b')", {tt::STRING_LITERAL, tt::CHAR_LITERAL}},
        {R"('\'''\\')", {tt::CHAR_LITERAL, tt::CHAR_LITERAL}},
        {R"("\\""")", {tt::STRING_LITERAL, tt::STRING_LITERAL}},
        {"a#b", {tt::IDENTIFIER, tt::COMMENT}},
        {"1#2", {tt::INT_LITERAL, tt::COMMENT}},
    }));

// ---- numbers ----

// documented in dfa.cpp: a letter straight after the '.' ends the float instead of being an error
TEST(TokeniserNumbers, LetterAfterTheDotEndsTheFloat) {
    EXPECT_THAT(tokenise_source("5.foo"), ElementsAre(FieldsAre(tt::FLOAT_LITERAL, "5.", 0, 0),
                                                      FieldsAre(tt::IDENTIFIER, "foo", 0, 2)));
}

TEST(TokeniserNumbers, DotThenDigitIsAFloatNotAnAccess) {
    EXPECT_THAT(tokenise_source(".5"), ElementsAre(FieldsAre(tt::FLOAT_LITERAL, ".5", 0, 0)));
    EXPECT_THAT(tokenise_source(".x"), ElementsAre(FieldsAre(tt::ACCESS, ".", 0, 0),
                                                   FieldsAre(tt::IDENTIFIER, "x", 0, 1)));
}

TEST(TokeniserNumbers, BaseLiteralsKeepTheirPrefixInTheLexeme) {
    EXPECT_THAT(tokenise_source("2b101 16bfF"),
                ElementsAre(FieldsAre(tt::BASE_2_LITERAL, "2b101", 0, 0),
                            FieldsAre(tt::WHITESPACE, " ", 0, 5),
                            FieldsAre(tt::BASE_16_LITERAL, "16bfF", 0, 6)));
}

// ---- char and string literals ----

TEST(TokeniserStrings, EscapedQuoteDoesNotCloseTheString) {
    EXPECT_THAT(tokenise_source(R"("a\"b" c)"),
                ElementsAre(FieldsAre(tt::STRING_LITERAL, R"("a\"b")", 0, 0),
                            FieldsAre(tt::WHITESPACE, " ", 0, 6),
                            FieldsAre(tt::IDENTIFIER, "c", 0, 7)));
}

TEST(TokeniserStrings, EscapedBackslashDoesNotEscapeTheClosingQuote) {
    EXPECT_THAT(tokenise_source(R"("a\\"b)"),
                ElementsAre(FieldsAre(tt::STRING_LITERAL, R"("a\\")", 0, 0),
                            FieldsAre(tt::IDENTIFIER, "b", 0, 5)));
}

TEST(TokeniserStrings, StringCanSpanLines) {
    EXPECT_THAT(tokenise_source("\"one\ntwo\nthree\""),
                ElementsAre(FieldsAre(tt::STRING_LITERAL, "\"one\ntwo\nthree\"", 0, 0)));
}

TEST(TokeniserStrings, EscapedQuoteDoesNotCloseTheChar) {
    EXPECT_THAT(tokenise_source(R"('\'';)"),
                ElementsAre(FieldsAre(tt::CHAR_LITERAL, R"('\'')", 0, 0),
                            FieldsAre(tt::SEMI_COLON, ";", 0, 4)));
}

// ---- whitespace and comments ----

TEST(TokeniserWhitespace, MixedRunIsOneToken) {
    EXPECT_THAT(tokenise_source(" \t\r\n \t"),
                ElementsAre(FieldsAre(tt::WHITESPACE, " \t\r\n \t", 0, 0)));
}

TEST(TokeniserWhitespace, RunAcrossBlankLinesIsOneToken) {
    EXPECT_THAT(tokenise_source("a\n\n\n  b"),
                ElementsAre(FieldsAre(tt::IDENTIFIER, "a", 0, 0),
                            FieldsAre(tt::WHITESPACE, "\n\n\n  ", 0, 1),
                            FieldsAre(tt::IDENTIFIER, "b", 3, 2)));
}

TEST(TokeniserWhitespace, OnlyWhitespace) {
    EXPECT_THAT(types_of("   "), ElementsAre(tt::WHITESPACE));
    EXPECT_THAT(types_of("\n"), ElementsAre(tt::WHITESPACE));
    EXPECT_THAT(types_of("\t\n\n"), ElementsAre(tt::WHITESPACE));
}

TEST(TokeniserWhitespace, SeparatesTokens) {
    EXPECT_THAT(types_of("int\tx"), ElementsAre(tt::INT, tt::WHITESPACE, tt::IDENTIFIER));
    EXPECT_THAT(types_of("int\nx"), ElementsAre(tt::INT, tt::WHITESPACE, tt::IDENTIFIER));
    EXPECT_THAT(types_of("1 2"), ElementsAre(tt::INT_LITERAL, tt::WHITESPACE, tt::INT_LITERAL));
    EXPECT_THAT(types_of("16 b"), ElementsAre(tt::INT_LITERAL, tt::WHITESPACE, tt::IDENTIFIER));
    EXPECT_THAT(types_of("< -"), ElementsAre(tt::LESSER, tt::WHITESPACE, tt::MINUS));
    EXPECT_THAT(types_of("- >"), ElementsAre(tt::MINUS, tt::WHITESPACE, tt::GREATER));
    EXPECT_THAT(types_of("= ="), ElementsAre(tt::ASSIGN, tt::WHITESPACE, tt::ASSIGN));
}

TEST(TokeniserComments, RunsToTheEndOfTheLineAndStopsBeforeTheNewline) {
    EXPECT_THAT(tokenise_source("# a comment\n"),
                ElementsAre(FieldsAre(tt::COMMENT, "# a comment", 0, 0),
                            FieldsAre(tt::WHITESPACE, "\n", 0, 11)));
}

TEST(TokeniserComments, AtTheEndOfInputWithoutANewline) {
    EXPECT_THAT(tokenise_source("# last"), ElementsAre(FieldsAre(tt::COMMENT, "# last", 0, 0)));
    EXPECT_THAT(tokenise_source("#"), ElementsAre(FieldsAre(tt::COMMENT, "#", 0, 0)));
}

TEST(TokeniserComments, AfterCodeOnTheSameLine) {
    EXPECT_THAT(tokenise_source("x; # trailing\n"),
                ElementsAre(FieldsAre(tt::IDENTIFIER, "x", 0, 0),
                            FieldsAre(tt::SEMI_COLON, ";", 0, 1),
                            FieldsAre(tt::WHITESPACE, " ", 0, 2),
                            FieldsAre(tt::COMMENT, "# trailing", 0, 3),
                            FieldsAre(tt::WHITESPACE, "\n", 0, 13)));
}

TEST(TokeniserComments, TokensOnTheNextLineAreNotSwallowed) {
    EXPECT_THAT(
        tokenise_source("# one\nlet\n# two\nx"),
        ElementsAre(FieldsAre(tt::COMMENT, "# one", 0, 0), FieldsAre(tt::WHITESPACE, "\n", 0, 5),
                    FieldsAre(tt::LET, "let", 1, 0), FieldsAre(tt::WHITESPACE, "\n", 1, 3),
                    FieldsAre(tt::COMMENT, "# two", 2, 0), FieldsAre(tt::WHITESPACE, "\n", 2, 5),
                    FieldsAre(tt::IDENTIFIER, "x", 3, 0)));
}

// anything is allowed in a comment, including chars that are errors elsewhere
TEST(TokeniserComments, ContentIsNotTokenised) {
    EXPECT_THAT(types_of("# $ ? ` % \" ' 16b 1.2.3 ## if\n"),
                ElementsAre(tt::COMMENT, tt::WHITESPACE));
}

TEST(TokeniserComments, EmptyCommentLine) {
    EXPECT_THAT(tokenise_source("#\nx"), ElementsAre(FieldsAre(tt::COMMENT, "#", 0, 0),
                                                     FieldsAre(tt::WHITESPACE, "\n", 0, 1),
                                                     FieldsAre(tt::IDENTIFIER, "x", 1, 0)));
}

// ---- positions ----

TEST(TokeniserPositions, LineAndColumnAcrossLinesAndTabs) {
    EXPECT_THAT(
        tokenise_source("let x = 1;\n\tx = x + 2;\n"),
        ElementsAre(FieldsAre(tt::LET, "let", 0, 0), FieldsAre(tt::WHITESPACE, " ", 0, 3),
                    FieldsAre(tt::IDENTIFIER, "x", 0, 4), FieldsAre(tt::WHITESPACE, " ", 0, 5),
                    FieldsAre(tt::ASSIGN, "=", 0, 6), FieldsAre(tt::WHITESPACE, " ", 0, 7),
                    FieldsAre(tt::INT_LITERAL, "1", 0, 8), FieldsAre(tt::SEMI_COLON, ";", 0, 9),
                    // a tab is one column
                    FieldsAre(tt::WHITESPACE, "\n\t", 0, 10), FieldsAre(tt::IDENTIFIER, "x", 1, 1),
                    FieldsAre(tt::WHITESPACE, " ", 1, 2), FieldsAre(tt::ASSIGN, "=", 1, 3),
                    FieldsAre(tt::WHITESPACE, " ", 1, 4), FieldsAre(tt::IDENTIFIER, "x", 1, 5),
                    FieldsAre(tt::WHITESPACE, " ", 1, 6), FieldsAre(tt::PLUS, "+", 1, 7),
                    FieldsAre(tt::WHITESPACE, " ", 1, 8), FieldsAre(tt::INT_LITERAL, "2", 1, 9),
                    FieldsAre(tt::SEMI_COLON, ";", 1, 10), FieldsAre(tt::WHITESPACE, "\n", 1, 11)));
}

// a multi-line token is positioned at its first char, and the next token on the line it ends on
TEST(TokeniserPositions, MultiLineStringAndTheTokenAfterIt) {
    EXPECT_THAT(
        tokenise_source("s = \"ab\ncd\" + t;\n"),
        ElementsAre(FieldsAre(tt::IDENTIFIER, "s", 0, 0), FieldsAre(tt::WHITESPACE, " ", 0, 1),
                    FieldsAre(tt::ASSIGN, "=", 0, 2), FieldsAre(tt::WHITESPACE, " ", 0, 3),
                    FieldsAre(tt::STRING_LITERAL, "\"ab\ncd\"", 0, 4),
                    FieldsAre(tt::WHITESPACE, " ", 1, 3), FieldsAre(tt::PLUS, "+", 1, 4),
                    FieldsAre(tt::WHITESPACE, " ", 1, 5), FieldsAre(tt::IDENTIFIER, "t", 1, 6),
                    FieldsAre(tt::SEMI_COLON, ";", 1, 7), FieldsAre(tt::WHITESPACE, "\n", 1, 8)));
}

TEST(TokeniserPositions, TokenStraightAfterAMultiLineString) {
    EXPECT_THAT(tokenise_source("\"a\n\nb\";"),
                ElementsAre(FieldsAre(tt::STRING_LITERAL, "\"a\n\nb\"", 0, 0),
                            FieldsAre(tt::SEMI_COLON, ";", 2, 2)));
}

// ---- input shape ----

TEST(TokeniserInputShape, NoLines) {
    EXPECT_THAT(tokenise({}), IsEmpty());
    EXPECT_THAT(tokenise_source(""), IsEmpty());
}

TEST(TokeniserInputShape, OnlyEmptyLines) {
    EXPECT_THAT(tokenise({{}}), IsEmpty());
    EXPECT_THAT(tokenise({{}, {}, {}}), IsEmpty());
}

// empty inner vectors still count as lines, so the line numbers after them don't shift
TEST(TokeniserInputShape, EmptyLinesBetweenCode) {
    const std::vector<std::vector<char>> input = {{}, {'a', '\n'}, {}, {'b'}, {}};
    EXPECT_THAT(tokenise(input), ElementsAre(FieldsAre(tt::IDENTIFIER, "a", 1, 0),
                                             FieldsAre(tt::WHITESPACE, "\n", 1, 1),
                                             FieldsAre(tt::IDENTIFIER, "b", 3, 0)));
}

TEST(TokeniserInputShape, LastLineWithoutANewline) {
    EXPECT_THAT(tokenise_source("a\nb"), ElementsAre(FieldsAre(tt::IDENTIFIER, "a", 0, 0),
                                                     FieldsAre(tt::WHITESPACE, "\n", 0, 1),
                                                     FieldsAre(tt::IDENTIFIER, "b", 1, 0)));
    EXPECT_THAT(tokenise_source("return"), ElementsAre(FieldsAre(tt::RETURN, "return", 0, 0)));
    EXPECT_THAT(tokenise_source("16bff"),
                ElementsAre(FieldsAre(tt::BASE_16_LITERAL, "16bff", 0, 0)));
    EXPECT_THAT(tokenise_source("1."), ElementsAre(FieldsAre(tt::FLOAT_LITERAL, "1.", 0, 0)));
}

TEST(TokeniserInputShape, NoEndOfFileToken) {
    EXPECT_THAT(types_of("x\n"), ElementsAre(tt::IDENTIFIER, tt::WHITESPACE));
}

// ---- errors: unexpected character ----

struct error_case {
    std::string_view source;
    int line;
    int column;
};

void PrintTo(const error_case &test_case, std::ostream *os) {
    *os << testing::PrintToString(test_case.source) << " -> error at " << test_case.line << ':'
        << test_case.column;
}

class UnexpectedCharacter : public testing::TestWithParam<error_case> {};

TEST_P(UnexpectedCharacter, IsReportedAtItsPosition) {
    const error_case &test_case = GetParam();
    EXPECT_THAT([&test_case] { tokenise_source(test_case.source); },
                lexer_error_at(test_case.line, test_case.column));
}

constexpr error_case unknown_character_cases[] = {
    {"$", 0, 0},          {"?", 0, 0},     {"`", 0, 0},   {"%", 0, 0},
    {"let x = $;", 0, 8}, {"a ? b", 0, 2}, {"x%2", 0, 1}, {"let a = 1;\nlet b = $;\n", 1, 8},
    {"\n\n\t?", 2, 1},
};
INSTANTIATE_TEST_SUITE_P(UnknownCharacters, UnexpectedCharacter,
                         testing::ValuesIn(unknown_character_cases));

// no backtracking: none of these fall back to a shorter number followed by an identifier
constexpr error_case number_error_cases[] = {
    {"123abc", 0, 3},
    {"1a", 0, 1},
    {"0x1f", 0, 1},
    {"1_000", 0, 1},
    {"1e5", 0, 1},
    {"17b", 0, 2},
    {"17b1", 0, 2},
    {"1b1", 0, 1},
    {"12b1", 0, 2},
    {"116bff", 0, 3},
    {"016bff", 0, 3},
    {"02b1", 0, 2},
    {"2b102", 0, 4},
    {"2b2", 0, 2},
    {"2bx", 0, 2},
    {"2b1x", 0, 3},
    {"2b1_", 0, 3},
    {"2b;", 0, 2},
    {"2b ", 0, 2},
    {"2b\n", 0, 2},
    {"16bfg", 0, 4},
    {"16bg", 0, 3},
    {"16bff_", 0, 5},
    {"16b;", 0, 3},
    {"16b ", 0, 3},
    {"16b\n", 0, 3},
    // the prefix marker is a lower case 'b' only
    {"2B1", 0, 1},
    {"16Bff", 0, 2},
    {"1.2.3", 0, 3},
    {"1..2", 0, 2},
    {".5.5", 0, 2},
    {"1.5foo", 0, 3},
    {"1.5_", 0, 3},
    {".5x", 0, 2},
    {"x = 1;\n\ty = 12ab;\n", 1, 7},
};
INSTANTIATE_TEST_SUITE_P(Numbers, UnexpectedCharacter, testing::ValuesIn(number_error_cases));

constexpr error_case literal_error_cases[] = {
    // a char literal holds exactly one char or one escape
    {"''", 0, 1},
    {"'''", 0, 1},
    {"'ab'", 0, 2},
    {"'a;", 0, 2},
    {"'a\n'", 0, 2},
    {"'\n'", 0, 1},
    {R"('\q')", 0, 2},
    {R"('\x41')", 0, 2},
    {R"('\nn')", 0, 3},
    {R"('\ ')", 0, 2},
    {R"("\q")", 0, 2},
    {R"("abc\xdef")", 0, 5},
    {R"("\ ")", 0, 2},
    // a backslash can't be followed by a real newline
    {"\"abc\\\ndef\"", 0, 5},
    {"x = \"ok\";\ny = \"a\\qb\";\n", 1, 7},
};
INSTANTIATE_TEST_SUITE_P(Literals, UnexpectedCharacter, testing::ValuesIn(literal_error_cases));

TEST(TokeniserErrors, MessageNamesTheCharacter) {
    EXPECT_THAT([] { tokenise_source("x = $;"); },
                ThrowsMessage<std::runtime_error>(HasSubstr("unexpected character '$'")));
    EXPECT_THAT([] { tokenise_source("123abc"); },
                ThrowsMessage<std::runtime_error>(HasSubstr("unexpected character 'a'")));
}

// a raw newline in the message would split it over two lines
TEST(TokeniserErrors, MessageShowsANewlineAsAnEscape) {
    EXPECT_THAT([] { tokenise_source("2b\n"); },
                ThrowsMessage<std::runtime_error>(HasSubstr(R"(unexpected character '\n')")));
}

// ---- errors: unexpected end of input ----

struct unfinished_case {
    std::string_view source;
    int start_line;
    int start_column;
};

void PrintTo(const unfinished_case &test_case, std::ostream *os) {
    *os << testing::PrintToString(test_case.source) << " -> unfinished token at "
        << test_case.start_line << ':' << test_case.start_column;
}

class UnfinishedToken : public testing::TestWithParam<unfinished_case> {};

TEST_P(UnfinishedToken, IsReportedWithWhereItStarts) {
    const unfinished_case &test_case = GetParam();
    EXPECT_THAT([&test_case] { tokenise_source(test_case.source); },
                unfinished_token_error(test_case.start_line, test_case.start_column));
}

constexpr unfinished_case unfinished_cases[] = {
    {"\"", 0, 0},
    {"\"abc", 0, 0},
    {"\"abc\n", 0, 0},
    {R"("abc\)", 0, 0},
    {R"("abc\")", 0, 0},
    {"x = \"abc\ndef;\n", 0, 4},
    {"x;\ny = \"abc", 1, 4},
    {"'", 0, 0},
    {"'a", 0, 0},
    {R"('\)", 0, 0},
    {R"('\n)", 0, 0},
    {"x = 'a", 0, 4},
    {"2b", 0, 0},
    {"16b", 0, 0},
    {"x = 16b", 0, 4},
};
INSTANTIATE_TEST_SUITE_P(Literals, UnfinishedToken, testing::ValuesIn(unfinished_cases));

// the end of input is reported as one past the last char of the last line
TEST(TokeniserErrors, UnfinishedTokenMessageReportsTheEndOfInput) {
    EXPECT_THAT([] { tokenise_source("\"abc"); },
                ThrowsMessage<std::runtime_error>(HasSubstr("at line 0, column 4")));
    EXPECT_THAT([] { tokenise_source("let s = \"abc\ndef"); },
                ThrowsMessage<std::runtime_error>(AllOf(HasSubstr("at line 1, column 3"),
                                                        HasSubstr("starts at line 0, column 8"))));
}

// ---- snippets from the grammar ----

TEST(TokeniserSnippets, FunctionDeclaration) {
    EXPECT_THAT(significant_types_of("fn add(a: int, b: int): int {\n"
                                     "    return a + b;\n"
                                     "}\n"),
                ElementsAre(tt::FN, tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::COLON,
                            tt::INT, tt::COMMA, tt::IDENTIFIER, tt::COLON, tt::INT,
                            tt::PARENTHESES_CLOSE, tt::COLON, tt::INT, tt::CURLY_OPEN, tt::RETURN,
                            tt::IDENTIFIER, tt::PLUS, tt::IDENTIFIER, tt::SEMI_COLON,
                            tt::CURLY_CLOSE));
}

TEST(TokeniserSnippets, MainFunctionCallingPrintAndScan) {
    EXPECT_THAT(significant_types_of("fn main() {\n"
                                     "    print(\"hi\\n\"); # greet\n"
                                     "    scan(@x);\n"
                                     "    return;\n"
                                     "}\n"),
                ElementsAre(tt::FN, tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::PARENTHESES_CLOSE,
                            tt::CURLY_OPEN, tt::IDENTIFIER, tt::PARENTHESES_OPEN,
                            tt::STRING_LITERAL, tt::PARENTHESES_CLOSE, tt::SEMI_COLON,
                            tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::ADDRESS, tt::IDENTIFIER,
                            tt::PARENTHESES_CLOSE, tt::SEMI_COLON, tt::RETURN, tt::SEMI_COLON,
                            tt::CURLY_CLOSE));
}

TEST(TokeniserSnippets, StructDeclaration) {
    EXPECT_THAT(significant_types_of("struct point {\n"
                                     "    x: int;\n"
                                     "    next: ->point;\n"
                                     "    tags: [4]char;\n"
                                     "};\n"),
                ElementsAre(tt::STRUCT, tt::IDENTIFIER, tt::CURLY_OPEN, tt::IDENTIFIER, tt::COLON,
                            tt::INT, tt::SEMI_COLON, tt::IDENTIFIER, tt::COLON, tt::POINTER,
                            tt::IDENTIFIER, tt::SEMI_COLON, tt::IDENTIFIER, tt::COLON,
                            tt::SQUARE_OPEN, tt::INT_LITERAL, tt::SQUARE_CLOSE, tt::CHAR,
                            tt::SEMI_COLON, tt::CURLY_CLOSE, tt::SEMI_COLON));
}

TEST(TokeniserSnippets, ImplWithAnUpdateMethod) {
    EXPECT_THAT(significant_types_of("impl counter {\n"
                                     "    update fn bump(by: int) { this.n = this.n + by; }\n"
                                     "};\n"),
                ElementsAre(tt::IMPL, tt::IDENTIFIER, tt::CURLY_OPEN, tt::UPDATE, tt::FN,
                            tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::COLON,
                            tt::INT, tt::PARENTHESES_CLOSE, tt::CURLY_OPEN, tt::THIS, tt::ACCESS,
                            tt::IDENTIFIER, tt::ASSIGN, tt::THIS, tt::ACCESS, tt::IDENTIFIER,
                            tt::PLUS, tt::IDENTIFIER, tt::SEMI_COLON, tt::CURLY_CLOSE,
                            tt::CURLY_CLOSE, tt::SEMI_COLON));
}

TEST(TokeniserSnippets, UpdateFunctionDeclarationAndCall) {
    EXPECT_THAT(significant_types_of("update fn bump(c: counter)(by: int) {}\n"),
                ElementsAre(tt::UPDATE, tt::FN, tt::IDENTIFIER, tt::PARENTHESES_OPEN,
                            tt::IDENTIFIER, tt::COLON, tt::IDENTIFIER, tt::PARENTHESES_CLOSE,
                            tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::COLON, tt::INT,
                            tt::PARENTHESES_CLOSE, tt::CURLY_OPEN, tt::CURLY_CLOSE));
    EXPECT_THAT(significant_types_of("bump(c)(1, 2.5);"),
                ElementsAre(tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::IDENTIFIER,
                            tt::PARENTHESES_CLOSE, tt::PARENTHESES_OPEN, tt::INT_LITERAL, tt::COMMA,
                            tt::FLOAT_LITERAL, tt::PARENTHESES_CLOSE, tt::SEMI_COLON));
}

TEST(TokeniserSnippets, ForLoop) {
    EXPECT_THAT(significant_types_of("for (let i: int = 0; i < 10; i = i + 1) {\n"
                                     "    total = total + xs[i];\n"
                                     "}\n"),
                ElementsAre(tt::FOR, tt::PARENTHESES_OPEN, tt::LET, tt::IDENTIFIER, tt::COLON,
                            tt::INT, tt::ASSIGN, tt::INT_LITERAL, tt::SEMI_COLON, tt::IDENTIFIER,
                            tt::LESSER, tt::INT_LITERAL, tt::SEMI_COLON, tt::IDENTIFIER, tt::ASSIGN,
                            tt::IDENTIFIER, tt::PLUS, tt::INT_LITERAL, tt::PARENTHESES_CLOSE,
                            tt::CURLY_OPEN, tt::IDENTIFIER, tt::ASSIGN, tt::IDENTIFIER, tt::PLUS,
                            tt::IDENTIFIER, tt::SQUARE_OPEN, tt::IDENTIFIER, tt::SQUARE_CLOSE,
                            tt::SEMI_COLON, tt::CURLY_CLOSE));
}

TEST(TokeniserSnippets, IfElifElseAndWhile) {
    EXPECT_THAT(
        significant_types_of("while (x != 0 && !done) {\n"
                             "    if (x >= 16bff) { x = x >> 1; }\n"
                             "    elif (x <= 2b101) { x = x << 1; }\n"
                             "    else { done = true; }\n"
                             "}\n"),
        ElementsAre(
            tt::WHILE, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::NOT_EQUAL, tt::INT_LITERAL,
            tt::LOGICAL_AND, tt::NOT, tt::IDENTIFIER, tt::PARENTHESES_CLOSE, tt::CURLY_OPEN,
            // if
            tt::IF, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::GREATER_OR_EQUAL, tt::BASE_16_LITERAL,
            tt::PARENTHESES_CLOSE, tt::CURLY_OPEN, tt::IDENTIFIER, tt::ASSIGN, tt::IDENTIFIER,
            tt::RIGHT_SHIFT, tt::INT_LITERAL, tt::SEMI_COLON, tt::CURLY_CLOSE,
            // elif
            tt::ELIF, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::LESS_OR_EQUAL, tt::BASE_2_LITERAL,
            tt::PARENTHESES_CLOSE, tt::CURLY_OPEN, tt::IDENTIFIER, tt::ASSIGN, tt::IDENTIFIER,
            tt::LEFT_SHIFT, tt::INT_LITERAL, tt::SEMI_COLON, tt::CURLY_CLOSE,
            // else
            tt::ELSE, tt::CURLY_OPEN, tt::IDENTIFIER, tt::ASSIGN, tt::BOOL_LITERAL, tt::SEMI_COLON,
            tt::CURLY_CLOSE, tt::CURLY_CLOSE));
}

TEST(TokeniserSnippets, GlobalVariableDeclarations) {
    EXPECT_THAT(significant_types_of("define imut state limit: [3]->int = new(int, 3);\n"
                                     "define let c: char = '\\n';\n"),
                ElementsAre(tt::DEFINE, tt::IMUT, tt::STATE, tt::IDENTIFIER, tt::COLON,
                            tt::SQUARE_OPEN, tt::INT_LITERAL, tt::SQUARE_CLOSE, tt::POINTER,
                            tt::INT, tt::ASSIGN, tt::NEW, tt::PARENTHESES_OPEN, tt::INT, tt::COMMA,
                            tt::INT_LITERAL, tt::PARENTHESES_CLOSE, tt::SEMI_COLON,
                            // second declaration
                            tt::DEFINE, tt::LET, tt::IDENTIFIER, tt::COLON, tt::CHAR, tt::ASSIGN,
                            tt::CHAR_LITERAL, tt::SEMI_COLON));
}

TEST(TokeniserSnippets, StepRejectAndInvariant) {
    EXPECT_THAT(significant_types_of("step { x = x - 1; } reject { print(x); }\n"
                                     "inv { x >= 0 || y == 0 };\n"),
                ElementsAre(tt::STEP, tt::CURLY_OPEN, tt::IDENTIFIER, tt::ASSIGN, tt::IDENTIFIER,
                            tt::MINUS, tt::INT_LITERAL, tt::SEMI_COLON, tt::CURLY_CLOSE, tt::REJECT,
                            tt::CURLY_OPEN, tt::IDENTIFIER, tt::PARENTHESES_OPEN, tt::IDENTIFIER,
                            tt::PARENTHESES_CLOSE, tt::SEMI_COLON, tt::CURLY_CLOSE,
                            // invariant
                            tt::INV, tt::CURLY_OPEN, tt::IDENTIFIER, tt::GREATER_OR_EQUAL,
                            tt::INT_LITERAL, tt::LOGICAL_OR, tt::IDENTIFIER, tt::EQUAL,
                            tt::INT_LITERAL, tt::CURLY_CLOSE, tt::SEMI_COLON));
}

TEST(TokeniserSnippets, PointersStructInitialiserAndConversion) {
    EXPECT_THAT(
        significant_types_of("let p: ->int = @x;\n"
                             "<-p = <-p ~ 2 \\ 3;\n"
                             "let q: point = point{x: 1, y: float(n)};\n"
                             "let xs: [2]byte = [byte(1), byte(2)];\n"),
        ElementsAre(
            tt::LET, tt::IDENTIFIER, tt::COLON, tt::POINTER, tt::INT, tt::ASSIGN, tt::ADDRESS,
            tt::IDENTIFIER, tt::SEMI_COLON,
            // dereference assignment
            tt::DEREFERENCE, tt::IDENTIFIER, tt::ASSIGN, tt::DEREFERENCE, tt::IDENTIFIER,
            tt::EXPONENT, tt::INT_LITERAL, tt::DIVIDE_FLOOR, tt::INT_LITERAL, tt::SEMI_COLON,
            // struct initialiser
            tt::LET, tt::IDENTIFIER, tt::COLON, tt::IDENTIFIER, tt::ASSIGN, tt::IDENTIFIER,
            tt::CURLY_OPEN, tt::IDENTIFIER, tt::COLON, tt::INT_LITERAL, tt::COMMA, tt::IDENTIFIER,
            tt::COLON, tt::FLOAT, tt::PARENTHESES_OPEN, tt::IDENTIFIER, tt::PARENTHESES_CLOSE,
            tt::CURLY_CLOSE, tt::SEMI_COLON,
            // array literal
            tt::LET, tt::IDENTIFIER, tt::COLON, tt::SQUARE_OPEN, tt::INT_LITERAL, tt::SQUARE_CLOSE,
            tt::BYTE, tt::ASSIGN, tt::SQUARE_OPEN, tt::BYTE, tt::PARENTHESES_OPEN, tt::INT_LITERAL,
            tt::PARENTHESES_CLOSE, tt::COMMA, tt::BYTE, tt::PARENTHESES_OPEN, tt::INT_LITERAL,
            tt::PARENTHESES_CLOSE, tt::SQUARE_CLOSE, tt::SEMI_COLON));
}

// ---- lexeme round trip ----
// nothing is dropped or invented: the lexemes joined back together are the source

class LexemeRoundTrip : public testing::TestWithParam<std::string_view> {};

TEST_P(LexemeRoundTrip, JoinedLexemesReproduceTheSource) {
    const std::string_view &source = GetParam();
    EXPECT_EQ(joined_lexemes(tokenise_source(source)), source);
}

constexpr std::string_view round_trip_sources[] = {
    "",
    "\n",
    "x",
    "  \t x \t  ",
    "# only a comment",
    "# comment\n\n# another\n",
    "a\r\nb\r\n",
    "let s: [5]char = \"multi\nline\\n\";\nlet c: char = '\\'';\n",
    "x<-1; y = <-p; z = a->b;",
    "1 2 16 2b101 16bff 1.5 1. .5 5.foo",
    "define imut state limit: [3]->int = new(int, 16bFF);\n"
    "\n"
    "struct point {\n"
    "    x: int;\n"
    "    y: float;\n"
    "};\n"
    "\n"
    "impl point {\n"
    "    fn norm(scale: float): float { return (this.x ~ 2) * scale; }\n"
    "    update fn shift(dx: int) { this.x = this.x + dx; }\n"
    "};\n"
    "\n"
    "update fn tick(p: point)(dx: int) {\n"
    "\tstep { p.shift(dx); } reject { print(\"rejected\\n\"); }\n"
    "}\n"
    "\n"
    "# entry point\n"
    "fn main() {\n"
    "    let p: point = point{x: 1, y: 2.5};\n"
    "    for (let i: int = 0; i < 10; i = i + 1) {\n"
    "        if (i & 1 == 0 || !(i >= 4)) { tick(p)(i); }\n"
    "        elif (i << 2 != 8) { scan(@p.x); }\n"
    "        else { inv { p.x >= 0 }; }\n"
    "    }\n"
    "    while (true && false) { return; } # never runs\n"
    "}",
};
INSTANTIATE_TEST_SUITE_P(Sources, LexemeRoundTrip, testing::ValuesIn(round_trip_sources));

// every line and column is where the lexeme really is in the input
TEST(TokeniserPositions, EveryTokenStartsAtItsLexeme) {
    const std::vector<std::vector<char>> input = to_lines("fn main() {\n"
                                                          "\tlet s: [2]char = \"a\nb\"; # two\n"
                                                          "\n"
                                                          "    x<-1;\n"
                                                          "}");
    for (const token &tok : tokenise(input)) {
        ASSERT_FALSE(tok.lexeme.empty());
        ASSERT_LT(static_cast<std::size_t>(tok.line), input.size());
        ASSERT_LT(static_cast<std::size_t>(tok.column),
                  input[static_cast<std::size_t>(tok.line)].size());
        EXPECT_EQ(input[static_cast<std::size_t>(tok.line)][static_cast<std::size_t>(tok.column)],
                  tok.lexeme.front())
            << testing::PrintToString(tok);
    }
}

} // namespace
