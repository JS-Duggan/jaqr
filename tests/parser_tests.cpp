#include "../src/parser/parser.h"

#include "../src/ast/ast_root.h"
#include "../src/ast/declaration.h"
#include "../src/ast/expression.h"
#include "../src/ast/expression_base.h"
#include "../src/ast/source.h"
#include "../src/ast/statement.h"
#include "../src/ast/statement_base.h"
#include "../src/tokens/token.h"
#include "ast_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// parser only: every input is a hand built token vector, the tokeniser is never called

namespace {

using enum token_type;

// ---------------------------------------------------------------------------------------------
// building tokens
// tokens are made without a position, then on_one_line (or at, for explicit positions) places
// them. every test states its token types itself, nothing here guesses a type from text
// ---------------------------------------------------------------------------------------------

token unplaced(const token_type &type, std::string_view lexeme) {
    return {type, std::string(lexeme), 0, 0};
}

// the lexeme of a token type that only has one spelling
std::string_view fixed_lexeme(const token_type &type) {
    switch (type) {
    case INT:
        return "int";
    case BOOL:
        return "bool";
    case CHAR:
        return "char";
    case BYTE:
        return "byte";
    case FLOAT:
        return "float";
    case IF:
        return "if";
    case ELIF:
        return "elif";
    case ELSE:
        return "else";
    case WHILE:
        return "while";
    case FOR:
        return "for";
    case RETURN:
        return "return";
    case STEP:
        return "step";
    case REJECT:
        return "reject";
    case INV:
        return "inv";
    case UPDATE:
        return "update";
    case LET:
        return "let";
    case IMUT:
        return "imut";
    case STATE:
        return "state";
    case DEFINE:
        return "define";
    case FN:
        return "fn";
    case STRUCT:
        return "struct";
    case IMPL:
        return "impl";
    case THIS:
        return "this";
    case NEW:
        return "new";
    case PLUS:
        return "+";
    case MINUS:
        return "-";
    case MULTIPLY:
        return "*";
    case DIVIDE:
        return "/";
    case DIVIDE_FLOOR:
        return "\\";
    case EXPONENT:
        return "~";
    case XOR:
        return "^";
    case BIT_AND:
        return "&";
    case BIT_OR:
        return "|";
    case LOGICAL_AND:
        return "&&";
    case LOGICAL_OR:
        return "||";
    case NOT:
        return "!";
    case ADDRESS:
        return "@";
    case DEREFERENCE:
        return "<-";
    case POINTER:
        return "->";
    case ACCESS:
        return ".";
    case EQUAL:
        return "==";
    case ASSIGN:
        return "=";
    case NOT_EQUAL:
        return "!=";
    case LESSER:
        return "<";
    case GREATER:
        return ">";
    case LESS_OR_EQUAL:
        return "<=";
    case GREATER_OR_EQUAL:
        return ">=";
    case LEFT_SHIFT:
        return "<<";
    case RIGHT_SHIFT:
        return ">>";
    case CURLY_OPEN:
        return "{";
    case CURLY_CLOSE:
        return "}";
    case PARENTHESES_OPEN:
        return "(";
    case PARENTHESES_CLOSE:
        return ")";
    case SQUARE_OPEN:
        return "[";
    case SQUARE_CLOSE:
        return "]";
    case SEMI_COLON:
        return ";";
    case COLON:
        return ":";
    case COMMA:
        return ",";
    case IDENTIFIER:
    case INT_LITERAL:
    case FLOAT_LITERAL:
    case CHAR_LITERAL:
    case BOOL_LITERAL:
    case BASE_2_LITERAL:
    case BASE_16_LITERAL:
    case STRING_LITERAL:
    case WHITESPACE:
    case COMMENT:
        break;
    }
    ADD_FAILURE() << "token type " << static_cast<int>(type) << " has no fixed lexeme";
    return "";
}

// a keyword, operator or punctuator
token tok(const token_type &type) {
    return unplaced(type, fixed_lexeme(type));
}

token ident(std::string_view name) {
    return unplaced(IDENTIFIER, name);
}

token int_lit(std::string_view lexeme) {
    return unplaced(INT_LITERAL, lexeme);
}

token base2_lit(std::string_view lexeme) {
    return unplaced(BASE_2_LITERAL, lexeme);
}

token base16_lit(std::string_view lexeme) {
    return unplaced(BASE_16_LITERAL, lexeme);
}

token float_lit(std::string_view lexeme) {
    return unplaced(FLOAT_LITERAL, lexeme);
}

// the lexeme keeps its quotes and escapes, as the tokeniser leaves them
token char_lit(std::string_view lexeme) {
    return unplaced(CHAR_LITERAL, lexeme);
}

token string_lit(std::string_view lexeme) {
    return unplaced(STRING_LITERAL, lexeme);
}

token bool_lit(std::string_view lexeme) {
    return unplaced(BOOL_LITERAL, lexeme);
}

token whitespace(std::string_view text) {
    return unplaced(WHITESPACE, text);
}

token comment(std::string_view text) {
    return unplaced(COMMENT, text);
}

// the same token at an explicit position, for the span and error position tests
token at(const token &unplaced_token, int line, int column) {
    return {unplaced_token.type, unplaced_token.lexeme, line, column};
}

// places the tokens as if written on line 0 with one space between each
std::vector<token> on_one_line(const std::vector<token> &unplaced_tokens) {
    std::vector<token> placed;
    placed.reserve(unplaced_tokens.size());
    int column = 0;
    for (const token &current : unplaced_tokens) {
        placed.push_back(at(current, 0, column));
        column += static_cast<int>(current.lexeme.size()) + 1;
    }
    return placed;
}

// the tokens in_main puts before the body: fn main ( ) {
constexpr std::size_t main_prefix_length = 5;

// wraps statement tokens as fn main() { body }, so statements and expressions can be parsed
std::vector<token> in_main(const std::vector<token> &body) {
    std::vector<token> tokens = {tok(FN), ident("main"), tok(PARENTHESES_OPEN),
                                 tok(PARENTHESES_CLOSE), tok(CURLY_OPEN)};
    tokens.insert(tokens.end(), body.begin(), body.end());
    tokens.push_back(tok(CURLY_CLOSE));
    return tokens;
}

// expression tokens as the statement: expression ;
std::vector<token> as_statement(const std::vector<token> &expression) {
    std::vector<token> tokens = expression;
    tokens.push_back(tok(SEMI_COLON));
    return tokens;
}

// ---------------------------------------------------------------------------------------------
// reading the tree
// ---------------------------------------------------------------------------------------------

// node as its concrete type. throws std::bad_cast, failing the test, if it is another type
template <typename node_type, typename base_type> const node_type &as(const base_type &node) {
    return dynamic_cast<const node_type &>(node);
}

// the body of the only declaration, which must be a function
const block_stmt &only_function_body(const program &root) {
    if (root.declarations.size() != 1) {
        throw std::logic_error("expected exactly one declaration");
    }
    return *as<function_decl>(*root.declarations.front()).body;
}

const stmt &only_statement(const program &root) {
    const block_stmt &body = only_function_body(root);
    if (body.statements.size() != 1) {
        throw std::logic_error("expected exactly one statement");
    }
    return *body.statements.front();
}

const expr &only_expression(const program &root) {
    return *as<expr_stmt>(only_statement(root)).expression;
}

ast_root_ptr parse_statement_tokens(const std::vector<token> &statement) {
    return parse(on_one_line(in_main(statement)));
}

ast_root_ptr parse_expression_tokens(const std::vector<token> &expression) {
    return parse_statement_tokens(as_statement(expression));
}

std::string program_dump(const std::vector<token> &unplaced_tokens) {
    return dump(*parse(on_one_line(unplaced_tokens)));
}

// the dump of one statement, parsed as the whole body of main
std::string statement_dump(const std::vector<token> &statement) {
    return dump(only_statement(*parse_statement_tokens(statement)));
}

// the dump of one expression, parsed as an expression statement in main
std::string expression_dump(const std::vector<token> &expression) {
    return dump(only_expression(*parse_expression_tokens(expression)));
}

// the dump of a type, parsed as the type of a global variable
std::string type_dump(const std::vector<token> &type) {
    std::vector<token> tokens = {tok(DEFINE), tok(LET), ident("x"), tok(COLON)};
    tokens.insert(tokens.end(), type.begin(), type.end());
    tokens.push_back(tok(SEMI_COLON));
    const ast_root_ptr root = parse(on_one_line(tokens));
    return dump(*as<variable_decl>(*root->declarations.at(0)).declared_type);
}

// the value the parser gives a literal token, or nullopt if it is of another type
template <typename value_type> std::optional<value_type> literal_value(const token &literal_token) {
    const ast_root_ptr root = parse_expression_tokens({literal_token});
    const literal &node = as<literal>(only_expression(*root));
    if (!std::holds_alternative<value_type>(node.value)) {
        return std::nullopt;
    }
    return std::get<value_type>(node.value);
}

std::string span_text(const source_span &span) {
    return std::format("{}:{}-{}:{}", span.start_line, span.start_col, span.end_line, span.end_col);
}

// ---------------------------------------------------------------------------------------------
// reading errors
// ---------------------------------------------------------------------------------------------

// the message goes on to give the position just past the last token
constexpr std::string_view end_of_input_message = "SYN001: unexpected end of input at line ";

// the message of the syntax error the tokens cause
std::string parse_error(const std::vector<token> &placed_tokens) {
    try {
        const ast_root_ptr root = parse(placed_tokens);
        return "<no error, parsed as " + dump(*root) + ">";
    } catch (const std::runtime_error &error) {
        return error.what();
    }
}

testing::AssertionResult is_error_at(std::string_view message, std::string_view description,
                                     const token &offender) {
    const std::string start = std::format("{} '{}'", description, offender.lexeme);
    const std::string end = std::format("at line {}, column {}", offender.line, offender.column);
    if (message.starts_with(start) && message.ends_with(end)) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << "expected \"" << start << " ... " << end << "\", got \"" << message << '"';
}

// the message reports offender as an unexpected token, at the position of offender
testing::AssertionResult is_unexpected_token(std::string_view message, const token &offender) {
    return is_error_at(message, "SYN001: unexpected token", offender);
}

// parsing fails on the token at index offender, reporting its lexeme and position
testing::AssertionResult rejected_at(const std::vector<token> &unplaced_tokens,
                                     std::size_t offender) {
    const std::vector<token> tokens = on_one_line(unplaced_tokens);
    return is_unexpected_token(parse_error(tokens), tokens.at(offender));
}

// as rejected_at, for statement tokens in the body of main. offender is an index into body, and
// body.size() is the "}" that closes main
testing::AssertionResult rejected_in_main_at(const std::vector<token> &body, std::size_t offender) {
    return rejected_at(in_main(body), main_prefix_length + offender);
}

// parsing fails because the tokens stop too early
testing::AssertionResult rejected_at_end(const std::vector<token> &unplaced_tokens) {
    const std::string message = parse_error(on_one_line(unplaced_tokens));
    if (message.starts_with(end_of_input_message)) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << "expected \"" << end_of_input_message << "...\", got \"" << message << '"';
}

// the literal is rejected as out of range, reporting its lexeme and position
testing::AssertionResult literal_out_of_range(const token &literal_token) {
    const std::vector<token> tokens = on_one_line(in_main(as_statement({literal_token})));
    const std::string message = parse_error(tokens);
    const token &offender = tokens.at(main_prefix_length);
    const testing::AssertionResult result = is_error_at(message, "SYN002: literal", offender);
    if (result && message.find("is out of range") == std::string::npos) {
        return testing::AssertionFailure() << "not an out of range error: \"" << message << '"';
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// the helpers themselves
// a wrong helper would invalidate every test below
// ---------------------------------------------------------------------------------------------

TEST(ParserTestHelpers, OnOneLineSeparatesTokensWithOneSpace) {
    // define let xy : int ;
    const std::vector<token> tokens =
        on_one_line({tok(DEFINE), tok(LET), ident("xy"), tok(COLON), tok(INT), tok(SEMI_COLON)});
    ASSERT_EQ(tokens.size(), 6U);
    const std::array<int, 6> columns = {0, 7, 11, 14, 16, 20};
    for (std::size_t i = 0; i < tokens.size(); i++) {
        EXPECT_EQ(tokens[i].line, 0) << "token " << i;
        EXPECT_EQ(tokens[i].column, columns.at(i)) << "token " << i;
    }
    EXPECT_EQ(tokens[2].type, IDENTIFIER);
    EXPECT_EQ(tokens[2].lexeme, "xy");
    EXPECT_EQ(tokens[5].lexeme, ";");
}

TEST(ParserTestHelpers, AtKeepsTypeAndLexeme) {
    const token placed = at(tok(DEREFERENCE), 3, 9);
    EXPECT_EQ(placed.type, DEREFERENCE);
    EXPECT_EQ(placed.lexeme, "<-");
    EXPECT_EQ(placed.line, 3);
    EXPECT_EQ(placed.column, 9);
}

TEST(ParserTestHelpers, InMainPrefixLengthMatchesItsTokens) {
    const std::vector<token> tokens = in_main({ident("x"), tok(SEMI_COLON)});
    ASSERT_EQ(tokens.size(), main_prefix_length + 3);
    EXPECT_EQ(tokens[main_prefix_length].lexeme, "x");
    EXPECT_EQ(tokens.back().type, CURLY_CLOSE);
}

// ---------------------------------------------------------------------------------------------
// program.txt: program and trivia
// ---------------------------------------------------------------------------------------------

TEST(ParserProgram, EmptyTokenVectorIsEmptyProgram) {
    EXPECT_EQ(program_dump({}), "(program)");
}

TEST(ParserProgram, OnlyTriviaIsEmptyProgram) {
    EXPECT_EQ(program_dump({whitespace("  "), comment("# nothing"), whitespace("\n")}),
              "(program)");
}

TEST(ParserProgram, WhitespaceAndCommentsAreSkippedEverywhere) {
    EXPECT_EQ(
        program_dump({comment("# first"), whitespace("\n"), tok(DEFINE), whitespace(" "), tok(LET),
                      comment("# mid"), ident("x"), whitespace(" "), tok(COLON), whitespace(" "),
                      tok(INT), whitespace(" "), tok(ASSIGN), comment("# value"), int_lit("1"),
                      whitespace(" "), tok(SEMI_COLON), whitespace("\n"), comment("# last")}),
        "(program (let x int int:1))");
}

TEST(ParserProgram, DeclarationsKeepSourceOrder) {
    EXPECT_EQ(program_dump({
                  // define let g : int ;
                  tok(DEFINE),
                  tok(LET),
                  ident("g"),
                  tok(COLON),
                  tok(INT),
                  tok(SEMI_COLON),
                  // struct P { } ;
                  tok(STRUCT),
                  ident("P"),
                  tok(CURLY_OPEN),
                  tok(CURLY_CLOSE),
                  tok(SEMI_COLON),
                  // impl P { } ;
                  tok(IMPL),
                  ident("P"),
                  tok(CURLY_OPEN),
                  tok(CURLY_CLOSE),
                  tok(SEMI_COLON),
                  // fn f ( ) : int { }
                  tok(FN),
                  ident("f"),
                  tok(PARENTHESES_OPEN),
                  tok(PARENTHESES_CLOSE),
                  tok(COLON),
                  tok(INT),
                  tok(CURLY_OPEN),
                  tok(CURLY_CLOSE),
                  // update fn t ( ) ( ) { }
                  tok(UPDATE),
                  tok(FN),
                  ident("t"),
                  tok(PARENTHESES_OPEN),
                  tok(PARENTHESES_CLOSE),
                  tok(PARENTHESES_OPEN),
                  tok(PARENTHESES_CLOSE),
                  tok(CURLY_OPEN),
                  tok(CURLY_CLOSE),
                  // inv { g } ;
                  tok(INV),
                  tok(CURLY_OPEN),
                  ident("g"),
                  tok(CURLY_CLOSE),
                  tok(SEMI_COLON),
                  // fn main ( ) { }
                  tok(FN),
                  ident("main"),
                  tok(PARENTHESES_OPEN),
                  tok(PARENTHESES_CLOSE),
                  tok(CURLY_OPEN),
                  tok(CURLY_CLOSE),
              }),
              "(program (let g int) (struct P) (impl P) (fn f () int (block)) "
              "(update-fn t () () (block)) (inv g) (fn main () void (block)))");
}

// ---------------------------------------------------------------------------------------------
// program.txt: globalVariableDeclaration, variableDeclaration, binding
// ---------------------------------------------------------------------------------------------

TEST(ParserGlobalVariable, LetWithoutInitialiser) {
    EXPECT_EQ(
        program_dump({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON)}),
        "(program (let x int))");
}

TEST(ParserGlobalVariable, LetWithInitialiser) {
    EXPECT_EQ(program_dump({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(ASSIGN),
                            int_lit("1"), tok(PLUS), int_lit("2"), tok(SEMI_COLON)}),
              "(program (let x int (+ int:1 int:2)))");
}

TEST(ParserGlobalVariable, State) {
    EXPECT_EQ(program_dump({tok(DEFINE), tok(STATE), ident("s"), tok(COLON), tok(INT), tok(ASSIGN),
                            int_lit("0"), tok(SEMI_COLON)}),
              "(program (state s int int:0))");
}

TEST(ParserGlobalVariable, Imut) {
    EXPECT_EQ(program_dump({tok(DEFINE), tok(IMUT), ident("k"), tok(COLON), tok(FLOAT), tok(ASSIGN),
                            float_lit("1.5"), tok(SEMI_COLON)}),
              "(program (imut k float float:1.5))");
}

TEST(ParserGlobalVariable, ImutState) {
    EXPECT_EQ(program_dump({tok(DEFINE), tok(IMUT), tok(STATE), ident("k"), tok(COLON), tok(INT),
                            tok(ASSIGN), int_lit("3"), tok(SEMI_COLON)}),
              "(program (imut-state k int int:3))");
}

TEST(ParserGlobalVariable, StorageAndMutabilityAreRecorded) {
    const ast_root_ptr root = parse(on_one_line({
        tok(DEFINE),
        tok(LET),
        ident("a"),
        tok(COLON),
        tok(INT),
        tok(SEMI_COLON),
        tok(DEFINE),
        tok(IMUT),
        tok(STATE),
        ident("b"),
        tok(COLON),
        tok(INT),
        tok(ASSIGN),
        int_lit("1"),
        tok(SEMI_COLON),
    }));
    ASSERT_EQ(root->declarations.size(), 2U);
    const variable_decl &plain = as<variable_decl>(*root->declarations[0]);
    EXPECT_EQ(plain.storage, variable_storage::ORDINARY);
    EXPECT_FALSE(plain.immutable);
    EXPECT_EQ(plain.initializer, nullptr);
    const variable_decl &imut_state = as<variable_decl>(*root->declarations[1]);
    EXPECT_EQ(imut_state.storage, variable_storage::STATE);
    EXPECT_TRUE(imut_state.immutable);
    EXPECT_NE(imut_state.initializer, nullptr);
}

TEST(ParserGlobalVariable, StateRequiresInitialiser) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(STATE), ident("s"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 5));
}

TEST(ParserGlobalVariable, ImutRequiresInitialiser) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(IMUT), ident("k"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 5));
}

TEST(ParserGlobalVariable, ImutStateRequiresInitialiser) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(IMUT), tok(STATE), ident("k"), tok(COLON), tok(INT), tok(SEMI_COLON)},
        6));
}

TEST(ParserGlobalVariable, StateImutIsTheWrongOrder) {
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(STATE), tok(IMUT), ident("k"), tok(COLON), tok(INT),
                             tok(ASSIGN), int_lit("1"), tok(SEMI_COLON)},
                            2));
}

TEST(ParserGlobalVariable, DefineNeedsLetStateOrImut) {
    EXPECT_TRUE(rejected_at({tok(DEFINE), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(FN)}, 1));
}

TEST(ParserGlobalVariable, MissingSemiColon) {
    // define let x : int fn ...
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(FN)}, 5));
    EXPECT_TRUE(rejected_at_end({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT)}));
}

TEST(ParserGlobalVariable, MissingInitialiserAfterAssign) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(ASSIGN), tok(SEMI_COLON)},
        6));
}

TEST(ParserGlobalVariable, BindingNeedsNameColonAndType) {
    // define let : int ;
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), tok(COLON), tok(INT), tok(SEMI_COLON)}, 2));
    // define let x int ;
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(INT), tok(SEMI_COLON)}, 3));
    // define let x : ;
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(SEMI_COLON)}, 4));
    // define let x = 1 ;
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(LET), ident("x"), tok(ASSIGN), int_lit("1"), tok(SEMI_COLON)}, 3));
}

TEST(ParserGlobalVariable, VariableDeclarationNeedsDefineAtTopLevel) {
    EXPECT_TRUE(rejected_at({tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 0));
    EXPECT_TRUE(rejected_at(
        {tok(STATE), ident("x"), tok(COLON), tok(INT), tok(ASSIGN), int_lit("1"), tok(SEMI_COLON)},
        0));
}

// ---------------------------------------------------------------------------------------------
// program.txt: type
// ---------------------------------------------------------------------------------------------

struct primitive_case {
    token_type type;
    std::string_view text;
};

void PrintTo(const primitive_case &value, std::ostream *out) {
    *out << value.text;
}

constexpr std::array primitive_cases = {
    primitive_case{INT, "int"},   primitive_case{FLOAT, "float"}, primitive_case{CHAR, "char"},
    primitive_case{BOOL, "bool"}, primitive_case{BYTE, "byte"},
};

std::string primitive_name(const testing::TestParamInfo<primitive_case> &info) {
    return std::string(info.param.text);
}

class ParserPrimitiveType : public testing::TestWithParam<primitive_case> {};

TEST_P(ParserPrimitiveType, IsAType) {
    EXPECT_EQ(type_dump({tok(GetParam().type)}), GetParam().text);
}

// conversionExpression = primitiveType "(" expression ")"
TEST_P(ParserPrimitiveType, ConvertsAnExpression) {
    EXPECT_EQ(expression_dump({tok(GetParam().type), tok(PARENTHESES_OPEN), ident("x"),
                               tok(PARENTHESES_CLOSE)}),
              std::format("(convert {} x)", GetParam().text));
}

TEST_P(ParserPrimitiveType, IsNotAnExpressionOnItsOwn) {
    EXPECT_TRUE(rejected_in_main_at({tok(GetParam().type), tok(SEMI_COLON)}, 1));
}

INSTANTIATE_TEST_SUITE_P(AllPrimitives, ParserPrimitiveType, testing::ValuesIn(primitive_cases),
                         primitive_name);

TEST(ParserType, NamedType) {
    EXPECT_EQ(type_dump({ident("Point")}), "Point");
}

TEST(ParserType, Pointer) {
    EXPECT_EQ(type_dump({tok(POINTER), tok(INT)}), "(-> int)");
    EXPECT_EQ(type_dump({tok(POINTER), tok(POINTER), ident("Point")}), "(-> (-> Point))");
}

TEST(ParserType, Array) {
    EXPECT_EQ(type_dump({tok(SQUARE_OPEN), int_lit("3"), tok(SQUARE_CLOSE), tok(INT)}),
              "([] int:3 int)");
}

// types read left to right: [3]->int = array of 3 pointers to int
TEST(ParserType, ArrayOfPointers) {
    EXPECT_EQ(
        type_dump({tok(SQUARE_OPEN), int_lit("3"), tok(SQUARE_CLOSE), tok(POINTER), tok(INT)}),
        "([] int:3 (-> int))");
}

// ->[3]int = pointer to array of 3 int
TEST(ParserType, PointerToArray) {
    EXPECT_EQ(
        type_dump({tok(POINTER), tok(SQUARE_OPEN), int_lit("3"), tok(SQUARE_CLOSE), tok(INT)}),
        "(-> ([] int:3 int))");
}

TEST(ParserType, ArrayOfArrays) {
    EXPECT_EQ(type_dump({tok(SQUARE_OPEN), int_lit("2"), tok(SQUARE_CLOSE), tok(SQUARE_OPEN),
                         int_lit("3"), tok(SQUARE_CLOSE), ident("Point")}),
              "([] int:2 ([] int:3 Point))");
}

TEST(ParserType, ArraySizeIsAnyExpression) {
    // [ n + 1 ] int
    EXPECT_EQ(type_dump({tok(SQUARE_OPEN), ident("n"), tok(PLUS), int_lit("1"), tok(SQUARE_CLOSE),
                         tok(INT)}),
              "([] (+ n int:1) int)");
    // [ a [ 0 ] ] int
    EXPECT_EQ(type_dump({tok(SQUARE_OPEN), ident("a"), tok(SQUARE_OPEN), int_lit("0"),
                         tok(SQUARE_CLOSE), tok(SQUARE_CLOSE), tok(INT)}),
              "([] (index a int:0) int)");
    // [ size ( ) ] int
    EXPECT_EQ(type_dump({tok(SQUARE_OPEN), ident("size"), tok(PARENTHESES_OPEN),
                         tok(PARENTHESES_CLOSE), tok(SQUARE_CLOSE), tok(INT)}),
              "([] (call size) int)");
}

TEST(ParserType, ArrayNeedsSizeAndElementType) {
    // define let x : [ ] int ;
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(SQUARE_OPEN),
                             tok(SQUARE_CLOSE), tok(INT), tok(SEMI_COLON)},
                            5));
    // define let x : [ 3 ] ;
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(SQUARE_OPEN),
                             int_lit("3"), tok(SQUARE_CLOSE), tok(SEMI_COLON)},
                            7));
    // define let x : [ 3 int ;
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(SQUARE_OPEN),
                             int_lit("3"), tok(INT), tok(SEMI_COLON)},
                            6));
}

TEST(ParserType, PointerNeedsPointee) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(POINTER), tok(SEMI_COLON)}, 5));
}

TEST(ParserType, ArraySuffixIsNotAType) {
    // define let x : int [ 3 ] ;   the dimension comes first
    EXPECT_TRUE(rejected_at({tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT),
                             tok(SQUARE_OPEN), int_lit("3"), tok(SQUARE_CLOSE), tok(SEMI_COLON)},
                            5));
}

// ---------------------------------------------------------------------------------------------
// program.txt: functionDeclaration, updateFunctionDeclaration
// ---------------------------------------------------------------------------------------------

TEST(ParserFunction, Main) {
    EXPECT_EQ(program_dump({tok(FN), ident("main"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                            tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(program (fn main () void (block)))");
}

TEST(ParserFunction, MainTakesNoParameters) {
    // fn main ( x : int ) { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("main"), tok(PARENTHESES_OPEN), ident("x"), tok(COLON),
                             tok(INT), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            3));
}

TEST(ParserFunction, MainHasNoReturnType) {
    // fn main ( ) : int { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("main"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            4));
}

TEST(ParserFunction, NoParameters) {
    EXPECT_EQ(program_dump({tok(FN), ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                            tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(program (fn f () int (block)))");
}

TEST(ParserFunction, OneParameter) {
    EXPECT_EQ(program_dump({tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COLON),
                            tok(INT), tok(PARENTHESES_CLOSE), tok(COLON), tok(BOOL),
                            tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(program (fn f ((a int)) bool (block)))");
}

TEST(ParserFunction, ManyParametersAndCompoundTypes) {
    // fn f ( a : int , b : -> char , c : [ 2 ] Point ) : -> int { return a ; }
    EXPECT_EQ(program_dump({tok(FN),
                            ident("f"),
                            tok(PARENTHESES_OPEN),
                            ident("a"),
                            tok(COLON),
                            tok(INT),
                            tok(COMMA),
                            ident("b"),
                            tok(COLON),
                            tok(POINTER),
                            tok(CHAR),
                            tok(COMMA),
                            ident("c"),
                            tok(COLON),
                            tok(SQUARE_OPEN),
                            int_lit("2"),
                            tok(SQUARE_CLOSE),
                            ident("Point"),
                            tok(PARENTHESES_CLOSE),
                            tok(COLON),
                            tok(POINTER),
                            tok(INT),
                            tok(CURLY_OPEN),
                            tok(RETURN),
                            ident("a"),
                            tok(SEMI_COLON),
                            tok(CURLY_CLOSE)}),
              "(program (fn f ((a int) (b (-> char)) (c ([] int:2 Point))) (-> int) "
              "(block (return a))))");
}

TEST(ParserFunction, OrdinaryFunctionRecordsKindAndReturnType) {
    const ast_root_ptr root = parse(on_one_line(
        {tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COLON), tok(INT),
         tok(PARENTHESES_CLOSE), tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)}));
    const function_decl &function = as<function_decl>(*root->declarations.at(0));
    EXPECT_EQ(function.kind, function_kind::ORDINARY);
    EXPECT_EQ(function.name->lexeme, "f");
    ASSERT_EQ(function.ordinary_parameters.size(), 1U);
    EXPECT_EQ(function.ordinary_parameters[0].name, "a");
    EXPECT_TRUE(function.state_parameters.empty());
    EXPECT_NE(function.return_type, nullptr);
}

TEST(ParserFunction, ReturnTypeIsRequired) {
    // fn f ( ) { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            4));
    // fn f ( ) : { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(COLON), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            5));
}

TEST(ParserFunction, NeedsNameParameterClauseAndBody) {
    // fn ( ) : int { }
    EXPECT_TRUE(rejected_at({tok(FN), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(COLON),
                             tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            1));
    // fn f : int { }
    EXPECT_TRUE(rejected_at(
        {tok(FN), ident("f"), tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 2));
    // fn f ( ) : int ;
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(COLON), tok(INT), tok(SEMI_COLON)},
                            6));
}

TEST(ParserFunction, ParameterListErrors) {
    // fn f ( a : int , ) : int { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COLON),
                             tok(INT), tok(COMMA), tok(PARENTHESES_CLOSE), tok(COLON), tok(INT),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            7));
    // fn f ( a ) : int { }
    EXPECT_TRUE(
        rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(PARENTHESES_CLOSE),
                     tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                    4));
    // fn f ( a : int b : int ) : int { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COLON),
                             tok(INT), ident("b"), tok(COLON), tok(INT), tok(PARENTHESES_CLOSE),
                             tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            6));
    // fn f ( a : int : int { }
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COLON),
                             tok(INT), tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            6));
}

TEST(ParserUpdateFunction, FirstClauseIsStateSecondIsOrdinary) {
    // update fn tick ( c : Counter ) ( n : int , m : int ) { }
    const std::vector<token> tokens = on_one_line({
        tok(UPDATE),
        tok(FN),
        ident("tick"),
        tok(PARENTHESES_OPEN),
        ident("c"),
        tok(COLON),
        ident("Counter"),
        tok(PARENTHESES_CLOSE),
        tok(PARENTHESES_OPEN),
        ident("n"),
        tok(COLON),
        tok(INT),
        tok(COMMA),
        ident("m"),
        tok(COLON),
        tok(INT),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        tok(CURLY_CLOSE),
    });
    const ast_root_ptr root = parse(tokens);
    EXPECT_EQ(dump(*root), "(program (update-fn tick ((c Counter)) ((n int) (m int)) (block)))");

    const function_decl &function = as<function_decl>(*root->declarations.at(0));
    EXPECT_EQ(function.kind, function_kind::UPDATE);
    EXPECT_EQ(function.return_type, nullptr);
    ASSERT_EQ(function.state_parameters.size(), 1U);
    EXPECT_EQ(function.state_parameters[0].name, "c");
    ASSERT_EQ(function.ordinary_parameters.size(), 2U);
    EXPECT_EQ(function.ordinary_parameters[0].name, "n");
    EXPECT_EQ(function.ordinary_parameters[1].name, "m");
}

TEST(ParserUpdateFunction, BothClausesMayBeEmpty) {
    EXPECT_EQ(program_dump({tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN),
                            tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                            tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(program (update-fn t () () (block)))");
}

TEST(ParserUpdateFunction, OnlyStateClauseFilled) {
    EXPECT_EQ(program_dump({tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN), ident("s"),
                            tok(COLON), tok(INT), tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN),
                            tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(program (update-fn t ((s int)) () (block)))");
}

TEST(ParserUpdateFunction, OnlyOrdinaryClauseFilled) {
    EXPECT_EQ(program_dump({tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN),
                            tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN), ident("n"), tok(COLON),
                            tok(INT), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(program (update-fn t () ((n int)) (block)))");
}

TEST(ParserUpdateFunction, TopLevelUpdateNeedsTwoClauses) {
    // update fn t ( ) { }
    EXPECT_TRUE(rejected_at({tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN),
                             tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            5));
}

TEST(ParserUpdateFunction, HasNoReturnType) {
    // update fn t ( ) ( ) : int { }
    EXPECT_TRUE(rejected_at({tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN),
                             tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            7));
}

TEST(ParserUpdateFunction, UpdateNeedsFnAndName) {
    EXPECT_TRUE(rejected_at({tok(UPDATE), ident("t"), tok(PARENTHESES_OPEN)}, 1));
    EXPECT_TRUE(rejected_at({tok(UPDATE), tok(FN), tok(PARENTHESES_OPEN)}, 2));
}

// ---------------------------------------------------------------------------------------------
// program.txt: structDeclaration, implDeclaration
// ---------------------------------------------------------------------------------------------

TEST(ParserStruct, NoFields) {
    EXPECT_EQ(
        program_dump({tok(STRUCT), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}),
        "(program (struct P))");
}

TEST(ParserStruct, ManyFields) {
    // struct Node { value : int ; next : -> Node ; grid : [ 2 ] [ 2 ] float ; } ;
    EXPECT_EQ(
        program_dump({tok(STRUCT),       ident("Node"),    tok(CURLY_OPEN),  ident("value"),
                      tok(COLON),        tok(INT),         tok(SEMI_COLON),  ident("next"),
                      tok(COLON),        tok(POINTER),     ident("Node"),    tok(SEMI_COLON),
                      ident("grid"),     tok(COLON),       tok(SQUARE_OPEN), int_lit("2"),
                      tok(SQUARE_CLOSE), tok(SQUARE_OPEN), int_lit("2"),     tok(SQUARE_CLOSE),
                      tok(FLOAT),        tok(SEMI_COLON),  tok(CURLY_CLOSE), tok(SEMI_COLON)}),
        "(program (struct Node (value int) (next (-> Node)) "
        "(grid ([] int:2 ([] int:2 float)))))");
}

TEST(ParserStruct, TrailingSemiColonIsRequired) {
    EXPECT_TRUE(rejected_at_end({tok(STRUCT), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}));
    // struct P { } fn ...
    EXPECT_TRUE(
        rejected_at({tok(STRUCT), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(FN)}, 4));
}

TEST(ParserStruct, FieldErrors) {
    // struct P { x : int } ;   field needs its ";"
    EXPECT_TRUE(rejected_at({tok(STRUCT), ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON),
                             tok(INT), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            6));
    // struct P { x : int = 1 ; } ;   fields have no initialiser
    EXPECT_TRUE(
        rejected_at({tok(STRUCT), ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), tok(INT),
                     tok(ASSIGN), int_lit("1"), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                    6));
    // struct P { x ; } ;
    EXPECT_TRUE(rejected_at({tok(STRUCT), ident("P"), tok(CURLY_OPEN), ident("x"), tok(SEMI_COLON),
                             tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            4));
    // struct P { let x : int ; } ;
    EXPECT_TRUE(
        rejected_at({tok(STRUCT), ident("P"), tok(CURLY_OPEN), tok(LET), ident("x"), tok(COLON),
                     tok(INT), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                    3));
}

TEST(ParserStruct, NeedsNameAndBraces) {
    EXPECT_TRUE(rejected_at({tok(STRUCT), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_at({tok(STRUCT), ident("P"), tok(SEMI_COLON)}, 2));
}

TEST(ParserImpl, NoMethods) {
    EXPECT_EQ(
        program_dump({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}),
        "(program (impl P))");
}

TEST(ParserImpl, MethodsAndUpdateMethods) {
    // impl P { fn get ( ) : int { return this . x ; } update fn set ( v : int ) { } } ;
    const ast_root_ptr root = parse(on_one_line({
        tok(IMPL),
        ident("P"),
        tok(CURLY_OPEN),
        tok(FN),
        ident("get"),
        tok(PARENTHESES_OPEN),
        tok(PARENTHESES_CLOSE),
        tok(COLON),
        tok(INT),
        tok(CURLY_OPEN),
        tok(RETURN),
        tok(THIS),
        tok(ACCESS),
        ident("x"),
        tok(SEMI_COLON),
        tok(CURLY_CLOSE),
        tok(UPDATE),
        tok(FN),
        ident("set"),
        tok(PARENTHESES_OPEN),
        ident("v"),
        tok(COLON),
        tok(INT),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        tok(CURLY_CLOSE),
        tok(CURLY_CLOSE),
        tok(SEMI_COLON),
    }));
    EXPECT_EQ(dump(*root), "(program (impl P (fn get () int (block (return (. this x)))) "
                           "(update-fn set () ((v int)) (block))))");

    // the one clause of an update method is its ordinary parameters
    const impl_decl &impl = as<impl_decl>(*root->declarations.at(0));
    ASSERT_EQ(impl.methods.size(), 2U);
    const function_decl &setter = *impl.methods[1];
    EXPECT_EQ(setter.kind, function_kind::UPDATE);
    EXPECT_TRUE(setter.state_parameters.empty());
    ASSERT_EQ(setter.ordinary_parameters.size(), 1U);
    EXPECT_EQ(setter.ordinary_parameters[0].name, "v");
    EXPECT_EQ(setter.return_type, nullptr);
}

TEST(ParserImpl, TrailingSemiColonIsRequired) {
    EXPECT_TRUE(rejected_at_end({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}));
    EXPECT_TRUE(
        rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(STRUCT)}, 4));
}

TEST(ParserImpl, UpdateMethodTakesOneClause) {
    // impl P { update fn set ( ) ( ) { } } ;
    EXPECT_TRUE(rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(UPDATE), tok(FN),
                             ident("set"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN),
                             tok(CURLY_CLOSE), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            8));
}

TEST(ParserImpl, MethodNeedsReturnType) {
    // impl P { fn get ( ) { } } ;
    EXPECT_TRUE(rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(FN), ident("get"),
                             tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN),
                             tok(CURLY_CLOSE), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            7));
}

TEST(ParserImpl, OnlyMethodsAreAllowedInside) {
    // impl P { define let x : int ; } ;
    EXPECT_TRUE(
        rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(DEFINE), tok(LET), ident("x"),
                     tok(COLON), tok(INT), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                    3));
    // impl P { x : int ; } ;
    EXPECT_TRUE(rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON),
                             tok(INT), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            3));
    // impl P { inv { x } ; } ;
    EXPECT_TRUE(
        rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(INV), tok(CURLY_OPEN), ident("x"),
                     tok(CURLY_CLOSE), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                    3));
}

TEST(ParserImpl, NeedsNameAndBraces) {
    EXPECT_TRUE(rejected_at({tok(IMPL), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_at({tok(IMPL), ident("P"), tok(SEMI_COLON)}, 2));
}

// ---------------------------------------------------------------------------------------------
// program.txt: invariantDeclaration at top level
// ---------------------------------------------------------------------------------------------

TEST(ParserInvariant, TopLevel) {
    EXPECT_EQ(program_dump({tok(INV), tok(CURLY_OPEN), ident("x"), tok(GREATER), int_lit("0"),
                            tok(CURLY_CLOSE), tok(SEMI_COLON)}),
              "(program (inv (> x int:0)))");
}

TEST(ParserInvariant, TopLevelErrors) {
    // inv { x }   no ";"
    EXPECT_TRUE(rejected_at_end({tok(INV), tok(CURLY_OPEN), ident("x"), tok(CURLY_CLOSE)}));
    // inv { } ;
    EXPECT_TRUE(rejected_at({tok(INV), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 2));
    // inv x ;
    EXPECT_TRUE(rejected_at({tok(INV), ident("x"), tok(SEMI_COLON)}, 1));
    // inv { x ; } ;   holds an expression, not statements
    EXPECT_TRUE(rejected_at(
        {tok(INV), tok(CURLY_OPEN), ident("x"), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
        3));
    // inv ( x ) ;
    EXPECT_TRUE(rejected_at(
        {tok(INV), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}, 1));
}

// ---------------------------------------------------------------------------------------------
// program.txt: "main" is reserved for fn main() and rejected wherever an IDENT is expected
// ---------------------------------------------------------------------------------------------

TEST(ParserMainIsReserved, AsGlobalVariableName) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(LET), ident("main"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 2));
}

TEST(ParserMainIsReserved, AsLocalVariableName) {
    EXPECT_TRUE(
        rejected_in_main_at({tok(LET), ident("main"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 1));
}

TEST(ParserMainIsReserved, AsStructName) {
    EXPECT_TRUE(rejected_at(
        {tok(STRUCT), ident("main"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 1));
}

TEST(ParserMainIsReserved, AsImplName) {
    EXPECT_TRUE(rejected_at(
        {tok(IMPL), ident("main"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 1));
}

TEST(ParserMainIsReserved, AsFieldName) {
    EXPECT_TRUE(rejected_at({tok(STRUCT), ident("P"), tok(CURLY_OPEN), ident("main"), tok(COLON),
                             tok(INT), tok(SEMI_COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            3));
}

TEST(ParserMainIsReserved, AsParameterName) {
    EXPECT_TRUE(rejected_at({tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("main"), tok(COLON),
                             tok(INT), tok(PARENTHESES_CLOSE), tok(COLON), tok(INT),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            3));
}

TEST(ParserMainIsReserved, AsTypeName) {
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(LET), ident("x"), tok(COLON), ident("main"), tok(SEMI_COLON)}, 4));
}

TEST(ParserMainIsReserved, AsMethodInImpl) {
    // impl P { fn main ( ) { } } ;
    EXPECT_TRUE(rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(FN), ident("main"),
                             tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN),
                             tok(CURLY_CLOSE), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            4));
    // impl P { update fn main ( ) { } } ;
    EXPECT_TRUE(rejected_at({tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(UPDATE), tok(FN),
                             ident("main"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                            5));
}

TEST(ParserMainIsReserved, AsUpdateFunctionName) {
    EXPECT_TRUE(rejected_at({tok(UPDATE), tok(FN), ident("main"), tok(PARENTHESES_OPEN),
                             tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            2));
}

TEST(ParserMainIsReserved, InExpressions) {
    // main ( ) ;
    EXPECT_TRUE(rejected_in_main_at(
        {ident("main"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}, 0));
    // x = main ;
    EXPECT_TRUE(rejected_in_main_at({ident("x"), tok(ASSIGN), ident("main"), tok(SEMI_COLON)}, 2));
    // p . main ;
    EXPECT_TRUE(rejected_in_main_at({ident("p"), tok(ACCESS), ident("main"), tok(SEMI_COLON)}, 2));
    // P { main : 1 } ;
    EXPECT_TRUE(rejected_in_main_at({ident("P"), tok(CURLY_OPEN), ident("main"), tok(COLON),
                                     int_lit("1"), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                                    2));
}

TEST(ParserMainIsReserved, OnlyTheExactLexeme) {
    EXPECT_EQ(program_dump({tok(DEFINE), tok(LET), ident("Main"), tok(COLON), ident("main2"),
                            tok(SEMI_COLON)}),
              "(program (let Main main2))");
}

// ---------------------------------------------------------------------------------------------
// statement.txt: local variable declarations
// ---------------------------------------------------------------------------------------------

TEST(ParserLocalVariable, LetWithoutInitialiser) {
    EXPECT_EQ(statement_dump({tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON)}),
              "(let x int)");
}

TEST(ParserLocalVariable, LetWithInitialiser) {
    EXPECT_EQ(statement_dump({tok(LET), ident("p"), tok(COLON), tok(POINTER), tok(INT), tok(ASSIGN),
                              tok(ADDRESS), ident("x"), tok(SEMI_COLON)}),
              "(let p (-> int) (@ x))");
}

TEST(ParserLocalVariable, State) {
    EXPECT_EQ(statement_dump({tok(STATE), ident("s"), tok(COLON), tok(INT), tok(ASSIGN),
                              int_lit("0"), tok(SEMI_COLON)}),
              "(state s int int:0)");
}

TEST(ParserLocalVariable, Imut) {
    EXPECT_EQ(statement_dump({tok(IMUT), ident("k"), tok(COLON), tok(INT), tok(ASSIGN),
                              int_lit("3"), tok(SEMI_COLON)}),
              "(imut k int int:3)");
}

TEST(ParserLocalVariable, ImutState) {
    EXPECT_EQ(statement_dump({tok(IMUT), tok(STATE), ident("k"), tok(COLON), tok(INT), tok(ASSIGN),
                              int_lit("3"), tok(SEMI_COLON)}),
              "(imut-state k int int:3)");
}

TEST(ParserLocalVariable, IsADeclarationStatement) {
    const ast_root_ptr root =
        parse_statement_tokens({tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON)});
    const decl_stmt &statement = as<decl_stmt>(only_statement(*root));
    const variable_decl &declaration = as<variable_decl>(*statement.declaration);
    EXPECT_EQ(declaration.name->lexeme, "x");
    EXPECT_EQ(declaration.initializer, nullptr);
}

TEST(ParserLocalVariable, StateAndImutRequireInitialiser) {
    EXPECT_TRUE(
        rejected_in_main_at({tok(STATE), ident("s"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 4));
    EXPECT_TRUE(
        rejected_in_main_at({tok(IMUT), ident("k"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 4));
    EXPECT_TRUE(rejected_in_main_at(
        {tok(IMUT), tok(STATE), ident("k"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 5));
}

TEST(ParserLocalVariable, MissingSemiColon) {
    // the offender is the "}" that closes main
    EXPECT_TRUE(rejected_in_main_at({tok(LET), ident("x"), tok(COLON), tok(INT)}, 4));
}

TEST(ParserLocalVariable, DefineIsNotAStatement) {
    EXPECT_TRUE(rejected_in_main_at(
        {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON)}, 0));
}

// ---------------------------------------------------------------------------------------------
// statement.txt: expressionStatement, returnStatement
// ---------------------------------------------------------------------------------------------

TEST(ParserExpressionStatement, Assignment) {
    EXPECT_EQ(statement_dump({ident("x"), tok(ASSIGN), int_lit("1"), tok(SEMI_COLON)}),
              "(expr (= x int:1))");
}

// print and scan are not keywords, just identifiers that get called
TEST(ParserExpressionStatement, PrintAndScanAreOrdinaryCalls) {
    EXPECT_EQ(statement_dump({ident("print"), tok(PARENTHESES_OPEN), ident("x"),
                              tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}),
              "(expr (call print x))");
    EXPECT_EQ(statement_dump({ident("scan"), tok(PARENTHESES_OPEN), tok(ADDRESS), ident("x"),
                              tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}),
              "(expr (call scan (@ x)))");
}

TEST(ParserExpressionStatement, MissingSemiColon) {
    EXPECT_TRUE(rejected_in_main_at({ident("x"), tok(ASSIGN), int_lit("1")}, 3));
    // x y ;
    EXPECT_TRUE(rejected_in_main_at({ident("x"), ident("y"), tok(SEMI_COLON)}, 1));
}

TEST(ParserExpressionStatement, EmptyStatementIsNotInTheGrammar) {
    EXPECT_TRUE(rejected_in_main_at({tok(SEMI_COLON)}, 0));
}

TEST(ParserReturn, WithoutValue) {
    const ast_root_ptr root = parse_statement_tokens({tok(RETURN), tok(SEMI_COLON)});
    EXPECT_EQ(dump(only_statement(*root)), "(return)");
    EXPECT_EQ(as<return_stmt>(only_statement(*root)).value, nullptr);
}

TEST(ParserReturn, WithValue) {
    EXPECT_EQ(statement_dump({tok(RETURN), ident("x"), tok(PLUS), int_lit("1"), tok(SEMI_COLON)}),
              "(return (+ x int:1))");
}

TEST(ParserReturn, MissingSemiColon) {
    EXPECT_TRUE(rejected_in_main_at({tok(RETURN), ident("x")}, 2));
    EXPECT_TRUE(rejected_in_main_at({tok(RETURN)}, 1));
}

// ---------------------------------------------------------------------------------------------
// statement.txt: ifStatement
// ---------------------------------------------------------------------------------------------

TEST(ParserIf, IfOnly) {
    const ast_root_ptr root =
        parse_statement_tokens({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                                tok(CURLY_OPEN), tok(CURLY_CLOSE)});
    EXPECT_EQ(dump(only_statement(*root)), "(if (x (block)))");
    const if_stmt &statement = as<if_stmt>(only_statement(*root));
    EXPECT_EQ(statement.branches.size(), 1U);
    EXPECT_EQ(statement.else_body, nullptr);
}

TEST(ParserIf, IfElse) {
    // if ( x ) { return ; } else { y = 1 ; }
    EXPECT_EQ(statement_dump({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(RETURN), tok(SEMI_COLON), tok(CURLY_CLOSE),
                              tok(ELSE), tok(CURLY_OPEN), ident("y"), tok(ASSIGN), int_lit("1"),
                              tok(SEMI_COLON), tok(CURLY_CLOSE)}),
              "(if (x (block (return))) (else (block (expr (= y int:1)))))");
}

TEST(ParserIf, ElifChainWithoutElse) {
    // if ( a ) { } elif ( b ) { } elif ( c ) { }
    EXPECT_EQ(statement_dump({tok(IF), tok(PARENTHESES_OPEN), ident("a"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(ELIF), tok(PARENTHESES_OPEN),
                              ident("b"), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE),
                              tok(ELIF), tok(PARENTHESES_OPEN), ident("c"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(if (a (block)) (b (block)) (c (block)))");
}

TEST(ParserIf, ElifChainWithElse) {
    // if ( a ) { } elif ( b ) { } else { }
    EXPECT_EQ(statement_dump({tok(IF), tok(PARENTHESES_OPEN), ident("a"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(ELIF), tok(PARENTHESES_OPEN),
                              ident("b"), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE),
                              tok(ELSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(if (a (block)) (b (block)) (else (block)))");
}

TEST(ParserIf, ConditionNeedsParentheses) {
    // if x { }
    EXPECT_TRUE(rejected_in_main_at({tok(IF), ident("x"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 1));
    // if ( x + 1 { }
    EXPECT_TRUE(rejected_in_main_at({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PLUS),
                                     int_lit("1"), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                                    5));
    // if ( ) { }
    EXPECT_TRUE(rejected_in_main_at(
        {tok(IF), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
        2));
}

TEST(ParserIf, BodyMustBeABlock) {
    // if ( x ) return ;
    EXPECT_TRUE(rejected_in_main_at({tok(IF), tok(PARENTHESES_OPEN), ident("x"),
                                     tok(PARENTHESES_CLOSE), tok(RETURN), tok(SEMI_COLON)},
                                    4));
    // if ( x ) { } else if ( y ) { }   it is spelt elif
    EXPECT_TRUE(rejected_in_main_at({tok(IF), tok(PARENTHESES_OPEN), ident("x"),
                                     tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE),
                                     tok(ELSE), tok(IF), tok(PARENTHESES_OPEN), ident("y"),
                                     tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                                    7));
}

TEST(ParserIf, ElifAndElseNeedAnIf) {
    EXPECT_TRUE(rejected_in_main_at({tok(ELSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(ELIF), tok(PARENTHESES_OPEN), ident("x"),
                                     tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                                    0));
    // if ( x ) { } else { } elif ( y ) { }
    EXPECT_TRUE(
        rejected_in_main_at({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(ELSE), tok(CURLY_OPEN),
                             tok(CURLY_CLOSE), tok(ELIF), tok(PARENTHESES_OPEN), ident("y"),
                             tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            9));
    // if ( x ) { } else { } else { }
    EXPECT_TRUE(
        rejected_in_main_at({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                             tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(ELSE), tok(CURLY_OPEN),
                             tok(CURLY_CLOSE), tok(ELSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            9));
}

// ---------------------------------------------------------------------------------------------
// statement.txt: whileStatement, forStatement
// ---------------------------------------------------------------------------------------------

TEST(ParserWhile, ConditionAndBody) {
    // while ( x < 10 ) { x = x + 1 ; }
    EXPECT_EQ(
        statement_dump({tok(WHILE), tok(PARENTHESES_OPEN), ident("x"), tok(LESSER), int_lit("10"),
                        tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), ident("x"), tok(ASSIGN),
                        ident("x"), tok(PLUS), int_lit("1"), tok(SEMI_COLON), tok(CURLY_CLOSE)}),
        "(while (< x int:10) (block (expr (= x (+ x int:1)))))");
}

TEST(ParserWhile, Errors) {
    // while x { }
    EXPECT_TRUE(
        rejected_in_main_at({tok(WHILE), ident("x"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 1));
    // while ( ) { }
    EXPECT_TRUE(rejected_in_main_at({tok(WHILE), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                                     tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                                    2));
    // while ( x ) ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(WHILE), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
        4));
    // while ( x { }   here "x { }" is a struct initialiser, so the ")" is what is missing
    EXPECT_TRUE(rejected_in_main_at(
        {tok(WHILE), tok(PARENTHESES_OPEN), ident("x"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 5));
}

// the tokens of: for ( <initialiser> ; i < n ; i = i + 1 ) { }
std::vector<token> for_with_initialiser(const std::vector<token> &initialiser) {
    std::vector<token> tokens = {tok(FOR), tok(PARENTHESES_OPEN)};
    tokens.insert(tokens.end(), initialiser.begin(), initialiser.end());
    const std::vector<token> rest = {
        tok(SEMI_COLON),
        ident("i"),
        tok(LESSER),
        ident("n"),
        tok(SEMI_COLON),
        ident("i"),
        tok(ASSIGN),
        ident("i"),
        tok(PLUS),
        int_lit("1"),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        tok(CURLY_CLOSE),
    };
    tokens.insert(tokens.end(), rest.begin(), rest.end());
    return tokens;
}

TEST(ParserFor, LetInitialiserConditionAndIncrement) {
    EXPECT_EQ(statement_dump(for_with_initialiser(
                  {tok(LET), ident("i"), tok(COLON), tok(INT), tok(ASSIGN), int_lit("0")})),
              "(for (let i int int:0) (< i n) (= i (+ i int:1)) (block))");
}

TEST(ParserFor, InitialiserMustBeLet) {
    // for ( i = 0 ; ...
    EXPECT_TRUE(
        rejected_in_main_at(for_with_initialiser({ident("i"), tok(ASSIGN), int_lit("0")}), 2));
    // for ( state i : int = 0 ; ...
    EXPECT_TRUE(rejected_in_main_at(for_with_initialiser({tok(STATE), ident("i"), tok(COLON),
                                                          tok(INT), tok(ASSIGN), int_lit("0")}),
                                    2));
    // for ( imut i : int = 0 ; ...
    EXPECT_TRUE(rejected_in_main_at(for_with_initialiser({tok(IMUT), ident("i"), tok(COLON),
                                                          tok(INT), tok(ASSIGN), int_lit("0")}),
                                    2));
    // for ( ; ...
    EXPECT_TRUE(rejected_in_main_at(for_with_initialiser({}), 2));
}

TEST(ParserFor, InitialiserMustHaveAValue) {
    // for ( let i : int ; ...
    EXPECT_TRUE(
        rejected_in_main_at(for_with_initialiser({tok(LET), ident("i"), tok(COLON), tok(INT)}), 6));
}

TEST(ParserFor, AllThreeClausesAreRequired) {
    // for ( let i : int = 0 ; ; i ) { }
    EXPECT_TRUE(
        rejected_in_main_at({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"), tok(COLON),
                             tok(INT), tok(ASSIGN), int_lit("0"), tok(SEMI_COLON), tok(SEMI_COLON),
                             ident("i"), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            9));
    // for ( let i : int = 0 ; i ; ) { }
    EXPECT_TRUE(rejected_in_main_at({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"),
                                     tok(COLON), tok(INT), tok(ASSIGN), int_lit("0"),
                                     tok(SEMI_COLON), ident("i"), tok(SEMI_COLON),
                                     tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                                    11));
    // for ( let i : int = 0 ; i ) { }
    EXPECT_TRUE(
        rejected_in_main_at({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"), tok(COLON),
                             tok(INT), tok(ASSIGN), int_lit("0"), tok(SEMI_COLON), ident("i"),
                             tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                            10));
}

TEST(ParserFor, NeedsParenthesesAndBlock) {
    EXPECT_TRUE(rejected_in_main_at({tok(FOR), tok(LET)}, 1));
    // for ( let i : int = 0 ; i ; i ) ;
    EXPECT_TRUE(
        rejected_in_main_at({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"), tok(COLON),
                             tok(INT), tok(ASSIGN), int_lit("0"), tok(SEMI_COLON), ident("i"),
                             tok(SEMI_COLON), ident("i"), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                            13));
}

// ---------------------------------------------------------------------------------------------
// statement.txt: stepStatement, invariantDeclaration, block
// ---------------------------------------------------------------------------------------------

TEST(ParserStep, WithoutReject) {
    const ast_root_ptr root =
        parse_statement_tokens({tok(STEP), tok(CURLY_OPEN), tok(CURLY_CLOSE)});
    EXPECT_EQ(dump(only_statement(*root)), "(step (block))");
    EXPECT_EQ(as<step_stmt>(only_statement(*root)).reject_body, nullptr);
}

TEST(ParserStep, WithReject) {
    // step { x = 1 ; } reject { return ; }
    EXPECT_EQ(statement_dump({tok(STEP), tok(CURLY_OPEN), ident("x"), tok(ASSIGN), int_lit("1"),
                              tok(SEMI_COLON), tok(CURLY_CLOSE), tok(REJECT), tok(CURLY_OPEN),
                              tok(RETURN), tok(SEMI_COLON), tok(CURLY_CLOSE)}),
              "(step (block (expr (= x int:1))) (reject (block (return))))");
}

TEST(ParserStep, Errors) {
    // step ;
    EXPECT_TRUE(rejected_in_main_at({tok(STEP), tok(SEMI_COLON)}, 1));
    // reject { }
    EXPECT_TRUE(rejected_in_main_at({tok(REJECT), tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 0));
    // step { } reject ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(STEP), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(REJECT), tok(SEMI_COLON)}, 4));
    // step { } reject { } reject { }
    EXPECT_TRUE(rejected_in_main_at({tok(STEP), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(REJECT),
                                     tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(REJECT),
                                     tok(CURLY_OPEN), tok(CURLY_CLOSE)},
                                    6));
}

TEST(ParserInvariant, Local) {
    const ast_root_ptr root =
        parse_statement_tokens({tok(INV), tok(CURLY_OPEN), ident("x"), tok(GREATER_OR_EQUAL),
                                int_lit("0"), tok(CURLY_CLOSE), tok(SEMI_COLON)});
    EXPECT_EQ(dump(only_statement(*root)), "(inv (>= x int:0))");
    const decl_stmt &statement = as<decl_stmt>(only_statement(*root));
    EXPECT_NE(dynamic_cast<const invariant_decl *>(statement.declaration.get()), nullptr);
}

TEST(ParserInvariant, LocalNeedsSemiColon) {
    EXPECT_TRUE(rejected_in_main_at({tok(INV), tok(CURLY_OPEN), ident("x"), tok(CURLY_CLOSE)}, 4));
}

TEST(ParserBlock, Empty) {
    EXPECT_EQ(statement_dump({tok(CURLY_OPEN), tok(CURLY_CLOSE)}), "(block)");
}

TEST(ParserBlock, Nested) {
    // { { } { x ; { return ; } } }
    EXPECT_EQ(
        statement_dump({tok(CURLY_OPEN), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(CURLY_OPEN),
                        ident("x"), tok(SEMI_COLON), tok(CURLY_OPEN), tok(RETURN), tok(SEMI_COLON),
                        tok(CURLY_CLOSE), tok(CURLY_CLOSE), tok(CURLY_CLOSE)}),
        "(block (block) (block (expr x) (block (return))))");
}

TEST(ParserBlock, StatementsKeepSourceOrder) {
    // fn main ( ) { let x : int ; x = 1 ; print ( x ) ; return ; }
    EXPECT_EQ(
        program_dump(in_main(
            {tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON), ident("x"), tok(ASSIGN),
             int_lit("1"), tok(SEMI_COLON), ident("print"), tok(PARENTHESES_OPEN), ident("x"),
             tok(PARENTHESES_CLOSE), tok(SEMI_COLON), tok(RETURN), tok(SEMI_COLON)})),
        "(program (fn main () void (block (let x int) (expr (= x int:1)) "
        "(expr (call print x)) (return))))");
}

TEST(ParserBlock, UnclosedAndOverClosed) {
    EXPECT_TRUE(rejected_at_end(in_main({tok(CURLY_OPEN)})));
    // fn main ( ) { } }
    EXPECT_TRUE(rejected_at(in_main({tok(CURLY_CLOSE)}), main_prefix_length + 1));
}

TEST(ParserBlock, DeclarationsOtherThanVariablesAndInvariantsAreNotStatements) {
    EXPECT_TRUE(rejected_in_main_at({tok(FN)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(STRUCT)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(IMPL)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(UPDATE)}, 0));
}

// the example from the ast_dump documentation: fn main() { if (x<-1) { tick(p)(1); } }
TEST(ParserStatement, DocumentedExample) {
    EXPECT_EQ(program_dump(in_main({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(DEREFERENCE),
                                    int_lit("1"), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN),
                                    ident("tick"), tok(PARENTHESES_OPEN), ident("p"),
                                    tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN), int_lit("1"),
                                    tok(PARENTHESES_CLOSE), tok(SEMI_COLON), tok(CURLY_CLOSE)})),
              "(program (fn main () void (block (if ((< x (- int:1)) "
              "(block (expr (update-call tick (p) (int:1)))))))))");
}

// ---------------------------------------------------------------------------------------------
// expression.txt: the binary operator levels from logical or down to multiplicative
// ---------------------------------------------------------------------------------------------

struct binary_operator {
    token_type type;
    std::string_view text; // how ast_dump writes the operator
    std::string_view name; // for test names
    int level;             // a higher level binds tighter
};

void PrintTo(const binary_operator &value, std::ostream *out) {
    *out << value.text;
}

// every left associative binary operator, in the grammar's order
constexpr std::array binary_operators = {
    binary_operator{LOGICAL_OR, "||", "LogicalOr", 0},
    binary_operator{LOGICAL_AND, "&&", "LogicalAnd", 1},
    binary_operator{EQUAL, "==", "Equal", 2},
    binary_operator{NOT_EQUAL, "!=", "NotEqual", 2},
    binary_operator{LESSER, "<", "Lesser", 3},
    binary_operator{GREATER, ">", "Greater", 3},
    binary_operator{LESS_OR_EQUAL, "<=", "LessOrEqual", 3},
    binary_operator{GREATER_OR_EQUAL, ">=", "GreaterOrEqual", 3},
    binary_operator{BIT_OR, "|", "BitOr", 4},
    binary_operator{XOR, "^", "Xor", 5},
    binary_operator{BIT_AND, "&", "BitAnd", 6},
    binary_operator{LEFT_SHIFT, "<<", "LeftShift", 7},
    binary_operator{RIGHT_SHIFT, ">>", "RightShift", 7},
    binary_operator{PLUS, "+", "Plus", 8},
    binary_operator{MINUS, "-", "Minus", 8},
    binary_operator{MULTIPLY, "*", "Multiply", 9},
    binary_operator{DIVIDE, "/", "Divide", 9},
    binary_operator{DIVIDE_FLOOR, "\\", "DivideFloor", 9},
};

// a OP b
std::vector<token> binary_tokens(const binary_operator &operation) {
    return {ident("a"), tok(operation.type), ident("b")};
}

// a FIRST b SECOND c
std::vector<token> chain_tokens(const binary_operator &first, const binary_operator &second) {
    return {ident("a"), tok(first.type), ident("b"), tok(second.type), ident("c")};
}

class ParserBinaryOperator : public testing::TestWithParam<binary_operator> {};

TEST_P(ParserBinaryOperator, BuildsABinaryExpression) {
    EXPECT_EQ(expression_dump(binary_tokens(GetParam())), std::format("({} a b)", GetParam().text));
}

TEST_P(ParserBinaryOperator, NeedsARightOperand) {
    // a OP ;
    EXPECT_TRUE(rejected_in_main_at({ident("a"), tok(GetParam().type), tok(SEMI_COLON)}, 2));
}

// a OP b = c is (a OP b) = c: assignment is looser than every binary operator
TEST_P(ParserBinaryOperator, BindsTighterThanAssignment) {
    const std::string_view text = GetParam().text;
    EXPECT_EQ(
        expression_dump({ident("x"), tok(ASSIGN), ident("a"), tok(GetParam().type), ident("b")}),
        std::format("(= x ({} a b))", text));
    EXPECT_EQ(
        expression_dump({ident("a"), tok(GetParam().type), ident("b"), tok(ASSIGN), ident("x")}),
        std::format("(= ({} a b) x)", text));
}

// a OP b ~ c is a OP (b ~ c): power is tighter than every other binary operator
TEST_P(ParserBinaryOperator, BindsLooserThanPower) {
    const std::string_view text = GetParam().text;
    EXPECT_EQ(
        expression_dump({ident("a"), tok(GetParam().type), ident("b"), tok(EXPONENT), ident("c")}),
        std::format("({} a (~ b c))", text));
    EXPECT_EQ(
        expression_dump({ident("a"), tok(EXPONENT), ident("b"), tok(GetParam().type), ident("c")}),
        std::format("({} (~ a b) c)", text));
}

// a OP -b and a OP !b: the operands are unary expressions
TEST_P(ParserBinaryOperator, TakesUnaryOperands) {
    const std::string_view text = GetParam().text;
    EXPECT_EQ(expression_dump({tok(NOT), ident("a"), tok(GetParam().type), tok(MINUS), ident("b")}),
              std::format("({} (! a) (- b))", text));
}

std::string binary_operator_name(const testing::TestParamInfo<binary_operator> &info) {
    return std::string(info.param.name);
}

INSTANTIATE_TEST_SUITE_P(AllOperators, ParserBinaryOperator, testing::ValuesIn(binary_operators),
                         binary_operator_name);

struct operator_pair {
    binary_operator first;
    binary_operator second;
};

void PrintTo(const operator_pair &value, std::ostream *out) {
    *out << value.first.text << " with " << value.second.text;
}

std::string operator_pair_name(const testing::TestParamInfo<operator_pair> &info) {
    return std::format("{}With{}", info.param.first.name, info.param.second.name);
}

// every ordered pair of operators whose levels differ by level_gap (first is the looser one)
std::vector<operator_pair> pairs_with_level_gap(int level_gap) {
    std::vector<operator_pair> pairs;
    for (const binary_operator &first : binary_operators) {
        for (const binary_operator &second : binary_operators) {
            if (second.level - first.level == level_gap) {
                pairs.push_back({first, second});
            }
        }
    }
    return pairs;
}

// operators on one level, including an operator with itself
class ParserSameLevelOperators : public testing::TestWithParam<operator_pair> {};

// a - b + c = (a - b) + c
TEST_P(ParserSameLevelOperators, AreLeftAssociative) {
    const operator_pair &pair = GetParam();
    EXPECT_EQ(expression_dump(chain_tokens(pair.first, pair.second)),
              std::format("({} ({} a b) c)", pair.second.text, pair.first.text));
}

INSTANTIATE_TEST_SUITE_P(EachLevel, ParserSameLevelOperators,
                         testing::ValuesIn(pairs_with_level_gap(0)), operator_pair_name);

// first is one level looser than second
class ParserNeighbouringLevels : public testing::TestWithParam<operator_pair> {};

// a + b * c = a + (b * c)
TEST_P(ParserNeighbouringLevels, TighterOperatorOnTheRightBindsFirst) {
    const operator_pair &pair = GetParam();
    EXPECT_EQ(expression_dump(chain_tokens(pair.first, pair.second)),
              std::format("({} a ({} b c))", pair.first.text, pair.second.text));
}

// a * b + c = (a * b) + c
TEST_P(ParserNeighbouringLevels, TighterOperatorOnTheLeftBindsFirst) {
    const operator_pair &pair = GetParam();
    EXPECT_EQ(expression_dump(chain_tokens(pair.second, pair.first)),
              std::format("({} ({} a b) c)", pair.first.text, pair.second.text));
}

INSTANTIATE_TEST_SUITE_P(EachBoundary, ParserNeighbouringLevels,
                         testing::ValuesIn(pairs_with_level_gap(1)), operator_pair_name);

TEST(ParserPrecedence, OneOperatorFromEveryLevel) {
    // a || b && c == d < e | f ^ g & h << i + j * k ~ l
    EXPECT_EQ(expression_dump({ident("a"), tok(LOGICAL_OR), ident("b"), tok(LOGICAL_AND),
                               ident("c"), tok(EQUAL),      ident("d"), tok(LESSER),
                               ident("e"), tok(BIT_OR),     ident("f"), tok(XOR),
                               ident("g"), tok(BIT_AND),    ident("h"), tok(LEFT_SHIFT),
                               ident("i"), tok(PLUS),       ident("j"), tok(MULTIPLY),
                               ident("k"), tok(EXPONENT),   ident("l")}),
              "(|| a (&& b (== c (< d (| e (^ f (& g (<< h (+ i (* j (~ k l)))))))))))");
}

TEST(ParserPrecedence, OneOperatorFromEveryLevelTightestFirst) {
    // a ~ b * c + d << e & f ^ g | h < i == j && k || l
    EXPECT_EQ(expression_dump({ident("a"), tok(EXPONENT),   ident("b"), tok(MULTIPLY),
                               ident("c"), tok(PLUS),       ident("d"), tok(LEFT_SHIFT),
                               ident("e"), tok(BIT_AND),    ident("f"), tok(XOR),
                               ident("g"), tok(BIT_OR),     ident("h"), tok(LESSER),
                               ident("i"), tok(EQUAL),      ident("j"), tok(LOGICAL_AND),
                               ident("k"), tok(LOGICAL_OR), ident("l")}),
              "(|| (&& (== (< (| (^ (& (<< (+ (* (~ a b) c) d) e) f) g) h) i) j) k) l)");
}

// grammar comment: bitwise operators bind tighter than comparisons, x & 1 == 0 = (x & 1) == 0
TEST(ParserPrecedence, BitwiseAndBindsTighterThanEquality) {
    EXPECT_EQ(expression_dump({ident("x"), tok(BIT_AND), int_lit("1"), tok(EQUAL), int_lit("0")}),
              "(== (& x int:1) int:0)");
    EXPECT_EQ(expression_dump({int_lit("0"), tok(EQUAL), ident("x"), tok(BIT_AND), int_lit("1")}),
              "(== int:0 (& x int:1))");
    EXPECT_EQ(expression_dump({ident("x"), tok(BIT_OR), int_lit("1"), tok(LESSER), ident("y"),
                               tok(XOR), int_lit("2")}),
              "(< (| x int:1) (^ y int:2))");
}

TEST(ParserPrecedence, ParenthesesOverride) {
    // ( a + b ) * c
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), ident("a"), tok(PLUS), ident("b"),
                               tok(PARENTHESES_CLOSE), tok(MULTIPLY), ident("c")}),
              "(* (+ a b) c)");
    // a - ( b - c )
    EXPECT_EQ(expression_dump({ident("a"), tok(MINUS), tok(PARENTHESES_OPEN), ident("b"),
                               tok(MINUS), ident("c"), tok(PARENTHESES_CLOSE)}),
              "(- a (- b c))");
}

// ---------------------------------------------------------------------------------------------
// expression.txt: assignmentExpression
// ---------------------------------------------------------------------------------------------

TEST(ParserAssignment, IsRightAssociative) {
    EXPECT_EQ(expression_dump({ident("a"), tok(ASSIGN), ident("b"), tok(ASSIGN), ident("c")}),
              "(= a (= b c))");
    EXPECT_EQ(expression_dump({ident("a"), tok(ASSIGN), ident("b"), tok(ASSIGN), ident("c"),
                               tok(ASSIGN), ident("d")}),
              "(= a (= b (= c d)))");
}

TEST(ParserAssignment, TargetsAreLogicalOrExpressions) {
    // a [ 0 ] = 1
    EXPECT_EQ(expression_dump({ident("a"), tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE),
                               tok(ASSIGN), int_lit("1")}),
              "(= (index a int:0) int:1)");
    // p . x = 1
    EXPECT_EQ(expression_dump({ident("p"), tok(ACCESS), ident("x"), tok(ASSIGN), int_lit("1")}),
              "(= (. p x) int:1)");
    // <- p = 1
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), ident("p"), tok(ASSIGN), int_lit("1")}),
              "(= (<- p) int:1)");
}

TEST(ParserAssignment, InsideParenthesesIsAnOperand) {
    // ( a = b ) + 1
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), ident("a"), tok(ASSIGN), ident("b"),
                               tok(PARENTHESES_CLOSE), tok(PLUS), int_lit("1")}),
              "(+ (= a b) int:1)");
}

TEST(ParserAssignment, NeedsAValue) {
    EXPECT_TRUE(rejected_in_main_at({ident("a"), tok(ASSIGN), tok(SEMI_COLON)}, 2));
    EXPECT_TRUE(rejected_in_main_at({tok(ASSIGN), ident("a"), tok(SEMI_COLON)}, 0));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: unaryExpression, powerExpression, prefixExpression
// ---------------------------------------------------------------------------------------------

TEST(ParserUnary, EachOperator) {
    EXPECT_EQ(expression_dump({tok(MINUS), ident("x")}), "(- x)");
    EXPECT_EQ(expression_dump({tok(NOT), ident("x")}), "(! x)");
    EXPECT_EQ(expression_dump({tok(ADDRESS), ident("x")}), "(@ x)");
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), ident("p")}), "(<- p)");
}

TEST(ParserUnary, SignOperatorChains) {
    EXPECT_EQ(expression_dump({tok(MINUS), tok(MINUS), ident("x")}), "(- (- x))");
    EXPECT_EQ(expression_dump({tok(NOT), tok(NOT), ident("x")}), "(! (! x))");
    EXPECT_EQ(expression_dump({tok(MINUS), tok(NOT), tok(MINUS), ident("x")}), "(- (! (- x)))");
}

TEST(ParserUnary, PointerOperatorChains) {
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), tok(DEREFERENCE), ident("p")}), "(<- (<- p))");
    EXPECT_EQ(expression_dump({tok(ADDRESS), tok(DEREFERENCE), ident("p")}), "(@ (<- p))");
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), tok(ADDRESS), ident("x")}), "(<- (@ x))");
}

TEST(ParserUnary, SignOperatorsWrapPointerOperators) {
    EXPECT_EQ(expression_dump({tok(MINUS), tok(DEREFERENCE), ident("p")}), "(- (<- p))");
    EXPECT_EQ(expression_dump({tok(NOT), tok(ADDRESS), ident("x")}), "(! (@ x))");
}

// prefixExpression = ( "@" | "<-" ) prefixExpression: a sign operator cannot follow one
TEST(ParserUnary, PointerOperatorsCannotWrapSignOperators) {
    EXPECT_TRUE(
        rejected_in_main_at({tok(DEREFERENCE), tok(MINUS), ident("p"), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_in_main_at({tok(ADDRESS), tok(NOT), ident("x"), tok(SEMI_COLON)}, 1));
    // with parentheses it is fine: <- ( - p )
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), tok(PARENTHESES_OPEN), tok(MINUS), ident("p"),
                               tok(PARENTHESES_CLOSE)}),
              "(<- (- p))");
}

TEST(ParserUnary, BindsLooserThanPostfix) {
    // - a . b
    EXPECT_EQ(expression_dump({tok(MINUS), ident("a"), tok(ACCESS), ident("b")}), "(- (. a b))");
    // - f ( x )
    EXPECT_EQ(expression_dump({tok(MINUS), ident("f"), tok(PARENTHESES_OPEN), ident("x"),
                               tok(PARENTHESES_CLOSE)}),
              "(- (call f x))");
    // <- p . x
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), ident("p"), tok(ACCESS), ident("x")}),
              "(<- (. p x))");
    // @ a [ 0 ]
    EXPECT_EQ(expression_dump(
                  {tok(ADDRESS), ident("a"), tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE)}),
              "(@ (index a int:0))");
    // ( <- p ) . x
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), tok(DEREFERENCE), ident("p"),
                               tok(PARENTHESES_CLOSE), tok(ACCESS), ident("x")}),
              "(. (<- p) x)");
}

TEST(ParserUnary, BindsTighterThanMultiplicative) {
    EXPECT_EQ(expression_dump({tok(MINUS), ident("a"), tok(MULTIPLY), ident("b")}), "(* (- a) b)");
    EXPECT_EQ(expression_dump({ident("a"), tok(MULTIPLY), tok(MINUS), ident("b")}), "(* a (- b))");
    EXPECT_EQ(expression_dump({ident("a"), tok(MINUS), tok(MINUS), ident("b")}), "(- a (- b))");
    EXPECT_EQ(expression_dump({tok(NOT), ident("a"), tok(LOGICAL_AND), tok(NOT), ident("b")}),
              "(&& (! a) (! b))");
}

TEST(ParserUnary, NeedsAnOperand) {
    EXPECT_TRUE(rejected_in_main_at({tok(MINUS), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_in_main_at({tok(NOT), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_in_main_at({tok(ADDRESS), tok(SEMI_COLON)}, 1));
    EXPECT_TRUE(rejected_in_main_at({tok(DEREFERENCE), tok(SEMI_COLON)}, 1));
}

TEST(ParserUnary, OperatorsWithNoPrefixForm) {
    EXPECT_TRUE(rejected_in_main_at({tok(PLUS), ident("x"), tok(SEMI_COLON)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(MULTIPLY), ident("x"), tok(SEMI_COLON)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(EXPONENT), ident("x"), tok(SEMI_COLON)}, 0));
    EXPECT_TRUE(rejected_in_main_at({tok(BIT_AND), ident("x"), tok(SEMI_COLON)}, 0));
}

TEST(ParserPower, Binary) {
    EXPECT_EQ(expression_dump({ident("a"), tok(EXPONENT), ident("b")}), "(~ a b)");
}

// grammar comment: sign operators bind looser than ~, so -2 ~ 2 = -(2 ~ 2)
TEST(ParserPower, SignOnTheBaseAppliesToTheWholePower) {
    EXPECT_EQ(expression_dump({tok(MINUS), int_lit("2"), tok(EXPONENT), int_lit("2")}),
              "(- (~ int:2 int:2))");
    EXPECT_EQ(expression_dump({tok(NOT), ident("a"), tok(EXPONENT), ident("b")}), "(! (~ a b))");
}

// grammar comment: right associative, 2 ~ 3 ~ 2 = 2 ~ (3 ~ 2)
TEST(ParserPower, IsRightAssociative) {
    EXPECT_EQ(
        expression_dump({int_lit("2"), tok(EXPONENT), int_lit("3"), tok(EXPONENT), int_lit("2")}),
        "(~ int:2 (~ int:3 int:2))");
    EXPECT_EQ(expression_dump({ident("a"), tok(EXPONENT), ident("b"), tok(EXPONENT), ident("c"),
                               tok(EXPONENT), ident("d")}),
              "(~ a (~ b (~ c d)))");
    // ( 2 ~ 3 ) ~ 2
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), int_lit("2"), tok(EXPONENT), int_lit("3"),
                               tok(PARENTHESES_CLOSE), tok(EXPONENT), int_lit("2")}),
              "(~ (~ int:2 int:3) int:2)");
}

// grammar comment: the exponent may be signed, 2 ~ -1
TEST(ParserPower, ExponentMayBeSigned) {
    EXPECT_EQ(expression_dump({int_lit("2"), tok(EXPONENT), tok(MINUS), int_lit("1")}),
              "(~ int:2 (- int:1))");
    EXPECT_EQ(expression_dump({ident("a"), tok(EXPONENT), tok(NOT), ident("b")}), "(~ a (! b))");
    // 2 ~ - 3 ~ 2 = 2 ~ -(3 ~ 2)
    EXPECT_EQ(expression_dump({int_lit("2"), tok(EXPONENT), tok(MINUS), int_lit("3"), tok(EXPONENT),
                               int_lit("2")}),
              "(~ int:2 (- (~ int:3 int:2)))");
}

// grammar comment: pointer operators bind tighter than ~, so <-p ~ 2 = (<-p) ~ 2
TEST(ParserPower, PointerOperatorsBindTighter) {
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), ident("p"), tok(EXPONENT), int_lit("2")}),
              "(~ (<- p) int:2)");
    EXPECT_EQ(expression_dump({tok(ADDRESS), ident("x"), tok(EXPONENT), int_lit("2")}),
              "(~ (@ x) int:2)");
    EXPECT_EQ(expression_dump({int_lit("2"), tok(EXPONENT), tok(DEREFERENCE), ident("p")}),
              "(~ int:2 (<- p))");
    // - <- p ~ 2 = -((<-p) ~ 2)
    EXPECT_EQ(
        expression_dump({tok(MINUS), tok(DEREFERENCE), ident("p"), tok(EXPONENT), int_lit("2")}),
        "(- (~ (<- p) int:2))");
}

TEST(ParserPower, PostfixBindsTighter) {
    // a . b ~ c [ 0 ]
    EXPECT_EQ(expression_dump({ident("a"), tok(ACCESS), ident("b"), tok(EXPONENT), ident("c"),
                               tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE)}),
              "(~ (. a b) (index c int:0))");
}

TEST(ParserPower, ExponentStopsAtMultiplicative) {
    // 2 ~ 3 * 4 = (2 ~ 3) * 4
    EXPECT_EQ(
        expression_dump({int_lit("2"), tok(EXPONENT), int_lit("3"), tok(MULTIPLY), int_lit("4")}),
        "(* (~ int:2 int:3) int:4)");
    // 2 ~ - 3 * 4 = (2 ~ -3) * 4
    EXPECT_EQ(expression_dump({int_lit("2"), tok(EXPONENT), tok(MINUS), int_lit("3"), tok(MULTIPLY),
                               int_lit("4")}),
              "(* (~ int:2 (- int:3)) int:4)");
}

TEST(ParserPower, NeedsAnExponent) {
    EXPECT_TRUE(rejected_in_main_at({ident("a"), tok(EXPONENT), tok(SEMI_COLON)}, 2));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: "<-" directly after an operand is "<" then "-", in prefix position a dereference
// ---------------------------------------------------------------------------------------------

TEST(ParserDereferenceSplit, AfterAnIdentifierIsLessThanNegative) {
    EXPECT_EQ(expression_dump({ident("x"), tok(DEREFERENCE), int_lit("1")}), "(< x (- int:1))");
}

TEST(ParserDereferenceSplit, MatchesTheSeparateTokens) {
    EXPECT_EQ(expression_dump({ident("x"), tok(DEREFERENCE), int_lit("1")}),
              expression_dump({ident("x"), tok(LESSER), tok(MINUS), int_lit("1")}));
}

TEST(ParserDereferenceSplit, AfterEveryKindOfOperand) {
    // 1 <- 2
    EXPECT_EQ(expression_dump({int_lit("1"), tok(DEREFERENCE), int_lit("2")}),
              "(< int:1 (- int:2))");
    // f ( x ) <- 1
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("x"),
                               tok(PARENTHESES_CLOSE), tok(DEREFERENCE), int_lit("1")}),
              "(< (call f x) (- int:1))");
    // a [ 0 ] <- 1
    EXPECT_EQ(expression_dump({ident("a"), tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE),
                               tok(DEREFERENCE), int_lit("1")}),
              "(< (index a int:0) (- int:1))");
    // p . x <- 1
    EXPECT_EQ(
        expression_dump({ident("p"), tok(ACCESS), ident("x"), tok(DEREFERENCE), int_lit("1")}),
        "(< (. p x) (- int:1))");
    // ( x ) <- 1
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                               tok(DEREFERENCE), int_lit("1")}),
              "(< x (- int:1))");
    // this <- 1
    EXPECT_EQ(expression_dump({tok(THIS), tok(DEREFERENCE), int_lit("1")}), "(< this (- int:1))");
}

TEST(ParserDereferenceSplit, TheSplitHalvesKeepTheirPrecedence) {
    // a + b <- c * d = (a + b) < ((-c) * d)
    EXPECT_EQ(expression_dump({ident("a"), tok(PLUS), ident("b"), tok(DEREFERENCE), ident("c"),
                               tok(MULTIPLY), ident("d")}),
              "(< (+ a b) (* (- c) d))");
    // x <- 2 ~ 2 = x < -(2 ~ 2)
    EXPECT_EQ(
        expression_dump({ident("x"), tok(DEREFERENCE), int_lit("2"), tok(EXPONENT), int_lit("2")}),
        "(< x (- (~ int:2 int:2)))");
    // x <- 1 == y = (x < -1) == y
    EXPECT_EQ(expression_dump({ident("x"), tok(DEREFERENCE), int_lit("1"), tok(EQUAL), ident("y")}),
              "(== (< x (- int:1)) y)");
    // a <- b <- c = (a < -b) < -c
    EXPECT_EQ(
        expression_dump({ident("a"), tok(DEREFERENCE), ident("b"), tok(DEREFERENCE), ident("c")}),
        "(< (< a (- b)) (- c))");
}

TEST(ParserDereferenceSplit, PrefixPositionStaysADereference) {
    // x == <- p
    EXPECT_EQ(expression_dump({ident("x"), tok(EQUAL), tok(DEREFERENCE), ident("p")}),
              "(== x (<- p))");
    // x < <- p
    EXPECT_EQ(expression_dump({ident("x"), tok(LESSER), tok(DEREFERENCE), ident("p")}),
              "(< x (<- p))");
    // x - <- p
    EXPECT_EQ(expression_dump({ident("x"), tok(MINUS), tok(DEREFERENCE), ident("p")}),
              "(- x (<- p))");
    // x = <- p
    EXPECT_EQ(expression_dump({ident("x"), tok(ASSIGN), tok(DEREFERENCE), ident("p")}),
              "(= x (<- p))");
    // f ( <- p , <- q )
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), tok(DEREFERENCE), ident("p"),
                               tok(COMMA), tok(DEREFERENCE), ident("q"), tok(PARENTHESES_CLOSE)}),
              "(call f (<- p) (<- q))");
    // a [ <- p ]
    EXPECT_EQ(expression_dump(
                  {ident("a"), tok(SQUARE_OPEN), tok(DEREFERENCE), ident("p"), tok(SQUARE_CLOSE)}),
              "(index a (<- p))");
    // ( <- p )
    EXPECT_EQ(expression_dump(
                  {tok(PARENTHESES_OPEN), tok(DEREFERENCE), ident("p"), tok(PARENTHESES_CLOSE)}),
              "(<- p)");
}

TEST(ParserDereferenceSplit, SplitThenPrefixDereference) {
    // x <- <- p = x < -(<-p)
    EXPECT_EQ(expression_dump({ident("x"), tok(DEREFERENCE), tok(DEREFERENCE), ident("p")}),
              "(< x (- (<- p)))");
    // <- p <- 1 = (<-p) < -1
    EXPECT_EQ(expression_dump({tok(DEREFERENCE), ident("p"), tok(DEREFERENCE), int_lit("1")}),
              "(< (<- p) (- int:1))");
}

TEST(ParserDereferenceSplit, TriviaAroundTheTokenDoesNotMatter) {
    EXPECT_EQ(expression_dump({ident("x"), whitespace(" "), tok(DEREFERENCE), comment("# c"),
                               whitespace(" "), int_lit("1")}),
              "(< x (- int:1))");
}

TEST(ParserDereferenceSplit, TheMinusHalfStillNeedsAnOperand) {
    // x <- ;
    EXPECT_TRUE(rejected_in_main_at({ident("x"), tok(DEREFERENCE), tok(SEMI_COLON)}, 2));
    // fn main ( ) { x <-
    EXPECT_TRUE(
        rejected_at_end({tok(FN), ident("main"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                         tok(CURLY_OPEN), ident("x"), tok(DEREFERENCE)}));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: postfixExpression
// ---------------------------------------------------------------------------------------------

TEST(ParserPostfix, Calls) {
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}),
              "(call f)");
    EXPECT_EQ(
        expression_dump({ident("f"), tok(PARENTHESES_OPEN), int_lit("1"), tok(PARENTHESES_CLOSE)}),
        "(call f int:1)");
    // f ( a , b + 1 , c = d )
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COMMA),
                               ident("b"), tok(PLUS), int_lit("1"), tok(COMMA), ident("c"),
                               tok(ASSIGN), ident("d"), tok(PARENTHESES_CLOSE)}),
              "(call f a (+ b int:1) (= c d))");
}

TEST(ParserPostfix, Index) {
    EXPECT_EQ(expression_dump({ident("a"), tok(SQUARE_OPEN), ident("i"), tok(SQUARE_CLOSE)}),
              "(index a i)");
    // a [ i ] [ j + 1 ]
    EXPECT_EQ(
        expression_dump({ident("a"), tok(SQUARE_OPEN), ident("i"), tok(SQUARE_CLOSE),
                         tok(SQUARE_OPEN), ident("j"), tok(PLUS), int_lit("1"), tok(SQUARE_CLOSE)}),
        "(index (index a i) (+ j int:1))");
}

TEST(ParserPostfix, Member) {
    EXPECT_EQ(expression_dump({ident("p"), tok(ACCESS), ident("x")}), "(. p x)");
    EXPECT_EQ(expression_dump({ident("p"), tok(ACCESS), ident("x"), tok(ACCESS), ident("y")}),
              "(. (. p x) y)");
}

TEST(ParserPostfix, MixedChainsApplyLeftToRight) {
    // a . b [ 0 ] . c ( x )
    EXPECT_EQ(expression_dump({ident("a"), tok(ACCESS), ident("b"), tok(SQUARE_OPEN), int_lit("0"),
                               tok(SQUARE_CLOSE), tok(ACCESS), ident("c"), tok(PARENTHESES_OPEN),
                               ident("x"), tok(PARENTHESES_CLOSE)}),
              "(call (. (index (. a b) int:0) c) x)");
    // f ( x ) [ 0 ] . y
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("x"),
                               tok(PARENTHESES_CLOSE), tok(SQUARE_OPEN), int_lit("0"),
                               tok(SQUARE_CLOSE), tok(ACCESS), ident("y")}),
              "(. (index (call f x) int:0) y)");
}

// update methods take one clause, so p.m(x) is an ordinary call
TEST(ParserPostfix, MethodCallIsAnOrdinaryCall) {
    EXPECT_EQ(expression_dump({ident("p"), tok(ACCESS), ident("m"), tok(PARENTHESES_OPEN),
                               ident("x"), tok(PARENTHESES_CLOSE)}),
              "(call (. p m) x)");
}

TEST(ParserPostfix, TwoClausesAreOneUpdateCall) {
    // f ( s , t ) ( x , y , z )
    const ast_root_ptr root = parse_expression_tokens(
        {ident("f"), tok(PARENTHESES_OPEN), ident("s"), tok(COMMA), ident("t"),
         tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN), ident("x"), tok(COMMA), ident("y"),
         tok(COMMA), ident("z"), tok(PARENTHESES_CLOSE)});
    EXPECT_EQ(dump(only_expression(*root)), "(update-call f (s t) (x y z))");

    // the first clause is the state arguments, the second the ordinary ones
    const postfix_expr &call = as<postfix_expr>(only_expression(*root));
    EXPECT_EQ(call.op, postfix_op::UPDATE_CALL);
    const update_arguments &arguments = std::get<update_arguments>(call.arg);
    ASSERT_EQ(arguments.state.size(), 2U);
    EXPECT_EQ(dump(*arguments.state[0]), "s");
    ASSERT_EQ(arguments.regular.size(), 3U);
    EXPECT_EQ(dump(*arguments.regular[0]), "x");
}

TEST(ParserPostfix, UpdateCallClausesMayBeEmpty) {
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                               tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}),
              "(update-call f () ())");
    EXPECT_EQ(
        expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("s"), tok(PARENTHESES_CLOSE),
                         tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}),
        "(update-call f (s) ())");
}

TEST(ParserPostfix, OneClauseIsAnOrdinaryCall) {
    const ast_root_ptr root = parse_expression_tokens(
        {ident("f"), tok(PARENTHESES_OPEN), ident("s"), tok(PARENTHESES_CLOSE)});
    EXPECT_EQ(as<postfix_expr>(only_expression(*root)).op, postfix_op::FUNCT_CALL);
}

TEST(ParserPostfix, ClausesPairUpFromTheLeft) {
    // f ( a ) ( b ) ( c ) = (f(a)(b))(c)
    EXPECT_EQ(
        expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(PARENTHESES_CLOSE),
                         tok(PARENTHESES_OPEN), ident("b"), tok(PARENTHESES_CLOSE),
                         tok(PARENTHESES_OPEN), ident("c"), tok(PARENTHESES_CLOSE)}),
        "(call (update-call f (a) (b)) c)");
    // f ( a ) ( b ) ( c ) ( d ) = (f(a)(b))(c)(d)
    EXPECT_EQ(
        expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(PARENTHESES_CLOSE),
                         tok(PARENTHESES_OPEN), ident("b"), tok(PARENTHESES_CLOSE),
                         tok(PARENTHESES_OPEN), ident("c"), tok(PARENTHESES_CLOSE),
                         tok(PARENTHESES_OPEN), ident("d"), tok(PARENTHESES_CLOSE)}),
        "(update-call (update-call f (a) (b)) (c) (d))");
}

TEST(ParserPostfix, UpdateCallOnAnyCallee) {
    // p . m ( s ) ( x )
    EXPECT_EQ(expression_dump({ident("p"), tok(ACCESS), ident("m"), tok(PARENTHESES_OPEN),
                               ident("s"), tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN),
                               ident("x"), tok(PARENTHESES_CLOSE)}),
              "(update-call (. p m) (s) (x))");
    // a [ 0 ] ( s ) ( x )
    EXPECT_EQ(expression_dump({ident("a"), tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE),
                               tok(PARENTHESES_OPEN), ident("s"), tok(PARENTHESES_CLOSE),
                               tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE)}),
              "(update-call (index a int:0) (s) (x))");
}

TEST(ParserPostfix, ClausesThatAreNotInARowAreSeparateCalls) {
    // f ( s ) [ 0 ] ( x )
    EXPECT_EQ(
        expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("s"), tok(PARENTHESES_CLOSE),
                         tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE), tok(PARENTHESES_OPEN),
                         ident("x"), tok(PARENTHESES_CLOSE)}),
        "(call (index (call f s) int:0) x)");
    // f ( s ) . g ( x )
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("s"),
                               tok(PARENTHESES_CLOSE), tok(ACCESS), ident("g"),
                               tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE)}),
              "(call (. (call f s) g) x)");
    // ( f ( s ) ) ( x )   the parentheses end the first postfix expression
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), ident("f"), tok(PARENTHESES_OPEN), ident("s"),
                               tok(PARENTHESES_CLOSE), tok(PARENTHESES_CLOSE),
                               tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE)}),
              "(call (call f s) x)");
}

TEST(ParserPostfix, ArgumentListErrors) {
    // f ( 1 , ) ;
    EXPECT_TRUE(rejected_in_main_at({ident("f"), tok(PARENTHESES_OPEN), int_lit("1"), tok(COMMA),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    4));
    // f ( , 1 ) ;
    EXPECT_TRUE(rejected_in_main_at({ident("f"), tok(PARENTHESES_OPEN), tok(COMMA), int_lit("1"),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    2));
    // f ( 1 2 ) ;
    EXPECT_TRUE(rejected_in_main_at({ident("f"), tok(PARENTHESES_OPEN), int_lit("1"), int_lit("2"),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    3));
    // f ( 1 ;
    EXPECT_TRUE(
        rejected_in_main_at({ident("f"), tok(PARENTHESES_OPEN), int_lit("1"), tok(SEMI_COLON)}, 3));
}

TEST(ParserPostfix, IndexErrors) {
    // a [ ] ;
    EXPECT_TRUE(
        rejected_in_main_at({ident("a"), tok(SQUARE_OPEN), tok(SQUARE_CLOSE), tok(SEMI_COLON)}, 2));
    // a [ 1 ;
    EXPECT_TRUE(
        rejected_in_main_at({ident("a"), tok(SQUARE_OPEN), int_lit("1"), tok(SEMI_COLON)}, 3));
    // a [ 1 , 2 ] ;
    EXPECT_TRUE(rejected_in_main_at({ident("a"), tok(SQUARE_OPEN), int_lit("1"), tok(COMMA),
                                     int_lit("2"), tok(SQUARE_CLOSE), tok(SEMI_COLON)},
                                    3));
}

TEST(ParserPostfix, MemberMustBeAnIdentifier) {
    EXPECT_TRUE(rejected_in_main_at({ident("p"), tok(ACCESS), int_lit("1"), tok(SEMI_COLON)}, 2));
    EXPECT_TRUE(rejected_in_main_at({ident("p"), tok(ACCESS), tok(SEMI_COLON)}, 2));
    EXPECT_TRUE(rejected_in_main_at({ident("p"), tok(ACCESS), tok(THIS), tok(SEMI_COLON)}, 2));
}

TEST(ParserPostfix, ArrowIsNotAMemberOperator) {
    EXPECT_TRUE(rejected_in_main_at({ident("p"), tok(POINTER), ident("x"), tok(SEMI_COLON)}, 1));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: literal
// ---------------------------------------------------------------------------------------------

TEST(ParserLiteral, Int) {
    EXPECT_EQ(literal_value<std::int32_t>(int_lit("0")), 0);
    EXPECT_EQ(literal_value<std::int32_t>(int_lit("42")), 42);
    EXPECT_EQ(literal_value<std::int32_t>(int_lit("007")), 7);
    EXPECT_EQ(expression_dump({int_lit("42")}), "int:42");
}

TEST(ParserLiteral, IntRangeLimit) {
    EXPECT_EQ(literal_value<std::int32_t>(int_lit("2147483647")), 2147483647);
    EXPECT_TRUE(literal_out_of_range(int_lit("2147483648")));
    EXPECT_TRUE(literal_out_of_range(int_lit("99999999999999999999")));
}

// there are no negative literals: the sign is a unary minus on a positive literal
TEST(ParserLiteral, NegativeIntIsNegateOfALiteral) {
    EXPECT_EQ(expression_dump({tok(MINUS), int_lit("1")}), "(- int:1)");
    EXPECT_EQ(expression_dump({tok(MINUS), int_lit("2147483647")}), "(- int:2147483647)");
}

// the literal is range checked before the minus is applied, so the smallest int cannot be
// written as -2147483648. the grammar does not say whether this is intended
TEST(ParserLiteral, SmallestIntCannotBeWrittenDirectly) {
    const std::vector<token> tokens =
        on_one_line(in_main({tok(MINUS), int_lit("2147483648"), tok(SEMI_COLON)}));
    EXPECT_TRUE(is_error_at(parse_error(tokens), "SYN002: literal", tokens[main_prefix_length + 1]));
}

TEST(ParserLiteral, Base2) {
    EXPECT_EQ(literal_value<std::int32_t>(base2_lit("2b0")), 0);
    EXPECT_EQ(literal_value<std::int32_t>(base2_lit("2b101")), 5);
    EXPECT_EQ(literal_value<std::int32_t>(base2_lit("2b0011")), 3);
    EXPECT_EQ(expression_dump({base2_lit("2b101")}), "int:5");
}

TEST(ParserLiteral, Base2RangeLimit) {
    // 31 ones, then a one followed by 31 zeros
    EXPECT_EQ(literal_value<std::int32_t>(base2_lit("2b" + std::string(31, '1'))), 2147483647);
    EXPECT_TRUE(literal_out_of_range(base2_lit("2b1" + std::string(31, '0'))));
}

TEST(ParserLiteral, Base16) {
    EXPECT_EQ(literal_value<std::int32_t>(base16_lit("16b0")), 0);
    EXPECT_EQ(literal_value<std::int32_t>(base16_lit("16bff")), 255);
    EXPECT_EQ(literal_value<std::int32_t>(base16_lit("16bff14")), 0xff14);
    EXPECT_EQ(literal_value<std::int32_t>(base16_lit("16b1b")), 27);
    EXPECT_EQ(expression_dump({base16_lit("16bff")}), "int:255");
}

TEST(ParserLiteral, Base16RangeLimit) {
    EXPECT_EQ(literal_value<std::int32_t>(base16_lit("16b7fffffff")), 2147483647);
    EXPECT_TRUE(literal_out_of_range(base16_lit("16b80000000")));
    EXPECT_TRUE(literal_out_of_range(base16_lit("16bffffffff")));
}

TEST(ParserLiteral, Float) {
    EXPECT_EQ(literal_value<double>(float_lit("1.5")), 1.5);
    EXPECT_EQ(literal_value<double>(float_lit("0.0")), 0.0);
    EXPECT_EQ(literal_value<double>(float_lit("3.14159")), 3.14159);
    EXPECT_EQ(expression_dump({float_lit("1.5")}), "float:1.5");
}

// the parser documents 1.5, .5 and 1. as the float spellings
TEST(ParserLiteral, FloatWithoutLeadingOrTrailingDigits) {
    EXPECT_EQ(literal_value<double>(float_lit(".5")), 0.5);
    EXPECT_EQ(literal_value<double>(float_lit("1.")), 1.0);
}

TEST(ParserLiteral, FloatIsNeverAnInt) {
    EXPECT_EQ(literal_value<std::int32_t>(float_lit("1.0")), std::nullopt);
    EXPECT_EQ(literal_value<double>(int_lit("1")), std::nullopt);
}

TEST(ParserLiteral, FloatRangeLimit) {
    // far past the largest double, about 1.8e308
    EXPECT_TRUE(literal_out_of_range(float_lit(std::string(400, '9') + ".0")));
}

TEST(ParserLiteral, Bool) {
    EXPECT_EQ(literal_value<bool>(bool_lit("true")), true);
    EXPECT_EQ(literal_value<bool>(bool_lit("false")), false);
    EXPECT_EQ(expression_dump({bool_lit("true")}), "bool:true");
    EXPECT_EQ(expression_dump({bool_lit("false")}), "bool:false");
}

TEST(ParserLiteral, Char) {
    EXPECT_EQ(literal_value<char>(char_lit("'a'")), 'a');
    EXPECT_EQ(literal_value<char>(char_lit("' '")), ' ');
    EXPECT_EQ(literal_value<char>(char_lit("'\"'")), '"');
    EXPECT_EQ(expression_dump({char_lit("'a'")}), "char:'a'");
}

TEST(ParserLiteral, CharEscapes) {
    EXPECT_EQ(literal_value<char>(char_lit(R"('\n')")), '\n');
    EXPECT_EQ(literal_value<char>(char_lit(R"('\t')")), '\t');
    EXPECT_EQ(literal_value<char>(char_lit(R"('\r')")), '\r');
    EXPECT_EQ(literal_value<char>(char_lit(R"('\0')")), '\0');
    EXPECT_EQ(literal_value<char>(char_lit(R"('\\')")), '\\');
    EXPECT_EQ(literal_value<char>(char_lit(R"('\'')")), '\'');
    EXPECT_EQ(literal_value<char>(char_lit(R"('\"')")), '"');
}

TEST(ParserLiteral, String) {
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("hi")")), "hi");
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("")")), "");
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("it's # not a comment")")),
              "it's # not a comment");
    EXPECT_EQ(expression_dump({string_lit(R"("hi")")}), R"(str:"hi")");
}

TEST(ParserLiteral, StringEscapes) {
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("a\nb")")), "a\nb");
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("\t\r")")), "\t\r");
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("a\\b")")), "a\\b");
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("say \"hi\"")")), "say \"hi\"");
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("\'")")), "'");
    // a backslash before the closing quote's backslash: "\\" is one backslash
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("\\")")), "\\");
    // \\n is a backslash then the letter n, not a newline
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("\\n")")), "\\n");
}

TEST(ParserLiteral, StringNulEscapeKeepsTheRestOfTheString) {
    const std::string expected = {'a', '\0', 'b'};
    EXPECT_EQ(literal_value<std::string>(string_lit(R"("a\0b")")), expected);
}

TEST(ParserLiteral, StringMayHoldARawNewline) {
    EXPECT_EQ(literal_value<std::string>(string_lit("\"ab\ncd\"")), "ab\ncd");
}

TEST(ParserLiteral, LiteralsAreOperands) {
    // "a" + 'b' * 2b1
    EXPECT_EQ(expression_dump({string_lit(R"("a")"), tok(PLUS), char_lit("'b'"), tok(MULTIPLY),
                               base2_lit("2b1")}),
              R"((+ str:"a" (* char:'b' int:1)))");
    // "abc" [ 0 ]
    EXPECT_EQ(expression_dump(
                  {string_lit(R"("abc")"), tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE)}),
              R"((index str:"abc" int:0))");
}

TEST(ParserLiteral, TwoLiteralsInARowAreRejected) {
    EXPECT_TRUE(rejected_in_main_at({int_lit("1"), int_lit("2"), tok(SEMI_COLON)}, 1));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: this, parentheses, arrayLiteral
// ---------------------------------------------------------------------------------------------

TEST(ParserPrimary, Identifier) {
    EXPECT_EQ(expression_dump({ident("some_name1")}), "some_name1");
}

TEST(ParserPrimary, This) {
    EXPECT_EQ(expression_dump({tok(THIS)}), "this");
    EXPECT_EQ(expression_dump({tok(THIS), tok(ACCESS), ident("x")}), "(. this x)");
    // this . m ( 1 )
    EXPECT_EQ(expression_dump({tok(THIS), tok(ACCESS), ident("m"), tok(PARENTHESES_OPEN),
                               int_lit("1"), tok(PARENTHESES_CLOSE)}),
              "(call (. this m) int:1)");
    // this . x = this . x + 1
    EXPECT_EQ(expression_dump({tok(THIS), tok(ACCESS), ident("x"), tok(ASSIGN), tok(THIS),
                               tok(ACCESS), ident("x"), tok(PLUS), int_lit("1")}),
              "(= (. this x) (+ (. this x) int:1))");
}

TEST(ParserPrimary, ParenthesesLeaveNoNode) {
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), ident("a"), tok(PARENTHESES_CLOSE)}), "a");
    EXPECT_EQ(expression_dump({tok(PARENTHESES_OPEN), tok(PARENTHESES_OPEN), ident("a"),
                               tok(PARENTHESES_CLOSE), tok(PARENTHESES_CLOSE)}),
              "a");
}

TEST(ParserPrimary, ParenthesesErrors) {
    // ( ) ;
    EXPECT_TRUE(
        rejected_in_main_at({tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}, 1));
    // ( a ;
    EXPECT_TRUE(rejected_in_main_at({tok(PARENTHESES_OPEN), ident("a"), tok(SEMI_COLON)}, 2));
    // a ) ;
    EXPECT_TRUE(rejected_in_main_at({ident("a"), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}, 1));
    // ( a , b ) ;
    EXPECT_TRUE(rejected_in_main_at({tok(PARENTHESES_OPEN), ident("a"), tok(COMMA), ident("b"),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    2));
}

TEST(ParserArrayLiteral, Empty) {
    EXPECT_EQ(expression_dump({tok(SQUARE_OPEN), tok(SQUARE_CLOSE)}), "(array)");
}

TEST(ParserArrayLiteral, Elements) {
    EXPECT_EQ(expression_dump({tok(SQUARE_OPEN), int_lit("1"), tok(SQUARE_CLOSE)}),
              "(array int:1)");
    // [ 1 , a + b , f ( ) ]
    EXPECT_EQ(expression_dump({tok(SQUARE_OPEN), int_lit("1"), tok(COMMA), ident("a"), tok(PLUS),
                               ident("b"), tok(COMMA), ident("f"), tok(PARENTHESES_OPEN),
                               tok(PARENTHESES_CLOSE), tok(SQUARE_CLOSE)}),
              "(array int:1 (+ a b) (call f))");
}

TEST(ParserArrayLiteral, Nested) {
    // [ [ 1 , 2 ] , [ ] , [ [ 3 ] ] ]
    EXPECT_EQ(
        expression_dump({tok(SQUARE_OPEN), tok(SQUARE_OPEN), int_lit("1"), tok(COMMA), int_lit("2"),
                         tok(SQUARE_CLOSE), tok(COMMA), tok(SQUARE_OPEN), tok(SQUARE_CLOSE),
                         tok(COMMA), tok(SQUARE_OPEN), tok(SQUARE_OPEN), int_lit("3"),
                         tok(SQUARE_CLOSE), tok(SQUARE_CLOSE), tok(SQUARE_CLOSE)}),
        "(array (array int:1 int:2) (array) (array (array int:3)))");
}

TEST(ParserArrayLiteral, CanBeIndexed) {
    // [ 1 , 2 ] [ 0 ]
    EXPECT_EQ(
        expression_dump({tok(SQUARE_OPEN), int_lit("1"), tok(COMMA), int_lit("2"),
                         tok(SQUARE_CLOSE), tok(SQUARE_OPEN), int_lit("0"), tok(SQUARE_CLOSE)}),
        "(index (array int:1 int:2) int:0)");
}

TEST(ParserArrayLiteral, Errors) {
    // [ 1 , ] ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(SQUARE_OPEN), int_lit("1"), tok(COMMA), tok(SQUARE_CLOSE), tok(SEMI_COLON)}, 3));
    // [ , ] ;
    EXPECT_TRUE(
        rejected_in_main_at({tok(SQUARE_OPEN), tok(COMMA), tok(SQUARE_CLOSE), tok(SEMI_COLON)}, 1));
    // [ 1 ;
    EXPECT_TRUE(rejected_in_main_at({tok(SQUARE_OPEN), int_lit("1"), tok(SEMI_COLON)}, 2));
    // [ 1 2 ] ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(SQUARE_OPEN), int_lit("1"), int_lit("2"), tok(SQUARE_CLOSE), tok(SEMI_COLON)}, 2));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: structInitializer
// ---------------------------------------------------------------------------------------------

TEST(ParserStructInitialiser, Empty) {
    EXPECT_EQ(expression_dump({ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}), "(struct-init P)");
}

TEST(ParserStructInitialiser, SeveralFields) {
    // P { x : 1 , y : a + b , z : c = d }
    EXPECT_EQ(expression_dump({ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), int_lit("1"),
                               tok(COMMA), ident("y"), tok(COLON), ident("a"), tok(PLUS),
                               ident("b"), tok(COMMA), ident("z"), tok(COLON), ident("c"),
                               tok(ASSIGN), ident("d"), tok(CURLY_CLOSE)}),
              "(struct-init P (x int:1) (y (+ a b)) (z (= c d)))");
}

TEST(ParserStructInitialiser, Nested) {
    // L { a : P { x : 1 } , b : [ Q { } ] }
    EXPECT_EQ(
        expression_dump({ident("L"), tok(CURLY_OPEN), ident("a"), tok(COLON), ident("P"),
                         tok(CURLY_OPEN), ident("x"), tok(COLON), int_lit("1"), tok(CURLY_CLOSE),
                         tok(COMMA), ident("b"), tok(COLON), tok(SQUARE_OPEN), ident("Q"),
                         tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SQUARE_CLOSE), tok(CURLY_CLOSE)}),
        "(struct-init L (a (struct-init P (x int:1))) (b (array (struct-init Q))))");
}

TEST(ParserStructInitialiser, TakesPostfixSuffixes) {
    // P { x : 1 } . x
    EXPECT_EQ(expression_dump({ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), int_lit("1"),
                               tok(CURLY_CLOSE), tok(ACCESS), ident("x")}),
              "(. (struct-init P (x int:1)) x)");
}

TEST(ParserStructInitialiser, AsInitialiserArgumentAndReturnValue) {
    // let p : P = P { x : 1 } ;
    EXPECT_EQ(statement_dump({tok(LET), ident("p"), tok(COLON), ident("P"), tok(ASSIGN), ident("P"),
                              tok(CURLY_OPEN), ident("x"), tok(COLON), int_lit("1"),
                              tok(CURLY_CLOSE), tok(SEMI_COLON)}),
              "(let p P (struct-init P (x int:1)))");
    // return P { } ;
    EXPECT_EQ(statement_dump(
                  {tok(RETURN), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}),
              "(return (struct-init P))");
    // f ( P { } ) ;
    EXPECT_EQ(expression_dump({ident("f"), tok(PARENTHESES_OPEN), ident("P"), tok(CURLY_OPEN),
                               tok(CURLY_CLOSE), tok(PARENTHESES_CLOSE)}),
              "(call f (struct-init P))");
}

// IDENT [ structInitializer ]: an identifier directly followed by "{" is always an initialiser.
// a block always follows ")" or a keyword, so "if (x) {" and "while (x) {" are not affected
TEST(ParserStructInitialiser, ConditionFollowedByBlockIsNotAnInitialiser) {
    // if ( x ) { }
    EXPECT_EQ(statement_dump({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(if (x (block)))");
    // while ( x ) { }
    EXPECT_EQ(statement_dump({tok(WHILE), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(while x (block))");
    // for ( let i : int = n ; i ; i ) { }
    EXPECT_EQ(
        statement_dump({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"), tok(COLON), tok(INT),
                        tok(ASSIGN), ident("n"), tok(SEMI_COLON), ident("i"), tok(SEMI_COLON),
                        ident("i"), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
        "(for (let i int n) i i (block))");
}

TEST(ParserStructInitialiser, InsideAConditionIsAnInitialiser) {
    // if ( P { } == q ) { }
    EXPECT_EQ(statement_dump({tok(IF), tok(PARENTHESES_OPEN), ident("P"), tok(CURLY_OPEN),
                              tok(CURLY_CLOSE), tok(EQUAL), ident("q"), tok(PARENTHESES_CLOSE),
                              tok(CURLY_OPEN), tok(CURLY_CLOSE)}),
              "(if ((== (struct-init P) q) (block)))");
}

TEST(ParserStructInitialiser, InvariantBodyIsNotAnInitialiser) {
    // inv { x } ;   and   inv { P { } } ;
    EXPECT_EQ(
        statement_dump({tok(INV), tok(CURLY_OPEN), ident("x"), tok(CURLY_CLOSE), tok(SEMI_COLON)}),
        "(inv x)");
    EXPECT_EQ(statement_dump({tok(INV), tok(CURLY_OPEN), ident("P"), tok(CURLY_OPEN),
                              tok(CURLY_CLOSE), tok(CURLY_CLOSE), tok(SEMI_COLON)}),
              "(inv (struct-init P))");
}

TEST(ParserStructInitialiser, StatementThenBlock) {
    // x ; { }   the ";" keeps the block apart from x
    EXPECT_EQ(
        program_dump(in_main({ident("x"), tok(SEMI_COLON), tok(CURLY_OPEN), tok(CURLY_CLOSE)})),
        "(program (fn main () void (block (expr x) (block))))");
    // x { } ;   without it, this is an initialiser used as a statement
    EXPECT_EQ(statement_dump({ident("x"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}),
              "(expr (struct-init x))");
}

TEST(ParserStructInitialiser, OnlyDirectlyAfterAnIdentifier) {
    // p . q { } ;
    EXPECT_TRUE(rejected_in_main_at(
        {ident("p"), tok(ACCESS), ident("q"), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)},
        3));
    // this { } ;
    EXPECT_TRUE(
        rejected_in_main_at({tok(THIS), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 1));
    // f ( ) { } ;
    EXPECT_TRUE(rejected_in_main_at({ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
                                     tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                                    3));
    // ( P ) { } ;
    EXPECT_TRUE(rejected_in_main_at({tok(PARENTHESES_OPEN), ident("P"), tok(PARENTHESES_CLOSE),
                                     tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                                    3));
}

TEST(ParserStructInitialiser, FieldErrors) {
    // P { x : 1 , } ;
    EXPECT_TRUE(rejected_in_main_at({ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON),
                                     int_lit("1"), tok(COMMA), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                                    6));
    // P { x 1 } ;
    EXPECT_TRUE(rejected_in_main_at(
        {ident("P"), tok(CURLY_OPEN), ident("x"), int_lit("1"), tok(CURLY_CLOSE), tok(SEMI_COLON)},
        3));
    // P { x : } ;
    EXPECT_TRUE(rejected_in_main_at(
        {ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), tok(CURLY_CLOSE), tok(SEMI_COLON)},
        4));
    // P { 1 } ;
    EXPECT_TRUE(rejected_in_main_at(
        {ident("P"), tok(CURLY_OPEN), int_lit("1"), tok(CURLY_CLOSE), tok(SEMI_COLON)}, 2));
    // P { x = 1 } ;
    EXPECT_TRUE(rejected_in_main_at({ident("P"), tok(CURLY_OPEN), ident("x"), tok(ASSIGN),
                                     int_lit("1"), tok(CURLY_CLOSE), tok(SEMI_COLON)},
                                    3));
    // P { x : 1 ;
    EXPECT_TRUE(rejected_in_main_at(
        {ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), int_lit("1"), tok(SEMI_COLON)}, 5));
    // P { x : 1 y : 2 } ;
    EXPECT_TRUE(rejected_in_main_at({ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON),
                                     int_lit("1"), ident("y"), tok(COLON), int_lit("2"),
                                     tok(CURLY_CLOSE), tok(SEMI_COLON)},
                                    5));
}

// ---------------------------------------------------------------------------------------------
// expression.txt: conversionExpression, allocationExpression
// ---------------------------------------------------------------------------------------------

TEST(ParserConversion, IsAPrimaryExpression) {
    // int ( x + 1 ) * 2
    EXPECT_EQ(expression_dump({tok(INT), tok(PARENTHESES_OPEN), ident("x"), tok(PLUS), int_lit("1"),
                               tok(PARENTHESES_CLOSE), tok(MULTIPLY), int_lit("2")}),
              "(* (convert int (+ x int:1)) int:2)");
    // - float ( x )
    EXPECT_EQ(expression_dump({tok(MINUS), tok(FLOAT), tok(PARENTHESES_OPEN), ident("x"),
                               tok(PARENTHESES_CLOSE)}),
              "(- (convert float x))");
}

TEST(ParserConversion, Nested) {
    // float ( int ( c ) )
    EXPECT_EQ(expression_dump({tok(FLOAT), tok(PARENTHESES_OPEN), tok(INT), tok(PARENTHESES_OPEN),
                               ident("c"), tok(PARENTHESES_CLOSE), tok(PARENTHESES_CLOSE)}),
              "(convert float (convert int c))");
}

TEST(ParserConversion, NamedTypeIsACallInstead) {
    EXPECT_EQ(expression_dump(
                  {ident("Point"), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE)}),
              "(call Point x)");
}

TEST(ParserConversion, Errors) {
    // int ( ) ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(INT), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}, 2));
    // int ( x ;
    EXPECT_TRUE(
        rejected_in_main_at({tok(INT), tok(PARENTHESES_OPEN), ident("x"), tok(SEMI_COLON)}, 3));
    // int ( x , y ) ;
    EXPECT_TRUE(rejected_in_main_at({tok(INT), tok(PARENTHESES_OPEN), ident("x"), tok(COMMA),
                                     ident("y"), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    3));
    // -> int ( x ) ;   only a primitive type name converts
    EXPECT_TRUE(rejected_in_main_at({tok(POINTER), tok(INT), tok(PARENTHESES_OPEN), ident("x"),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    0));
}

TEST(ParserAllocation, PrimitiveElement) {
    EXPECT_EQ(expression_dump({tok(NEW), tok(PARENTHESES_OPEN), tok(INT), tok(COMMA), int_lit("10"),
                               tok(PARENTHESES_CLOSE)}),
              "(new int int:10)");
}

TEST(ParserAllocation, AnyTypeAndCountExpression) {
    // new ( -> Point , n + 1 )
    EXPECT_EQ(
        expression_dump({tok(NEW), tok(PARENTHESES_OPEN), tok(POINTER), ident("Point"), tok(COMMA),
                         ident("n"), tok(PLUS), int_lit("1"), tok(PARENTHESES_CLOSE)}),
        "(new (-> Point) (+ n int:1))");
    // new ( [ 3 ] int , 2 )
    EXPECT_EQ(expression_dump({tok(NEW), tok(PARENTHESES_OPEN), tok(SQUARE_OPEN), int_lit("3"),
                               tok(SQUARE_CLOSE), tok(INT), tok(COMMA), int_lit("2"),
                               tok(PARENTHESES_CLOSE)}),
              "(new ([] int:3 int) int:2)");
    // new ( Point , 1 )
    EXPECT_EQ(expression_dump({tok(NEW), tok(PARENTHESES_OPEN), ident("Point"), tok(COMMA),
                               int_lit("1"), tok(PARENTHESES_CLOSE)}),
              "(new Point int:1)");
}

TEST(ParserAllocation, IsAPrimaryExpression) {
    // new ( int , 4 ) [ 0 ]
    EXPECT_EQ(expression_dump({tok(NEW), tok(PARENTHESES_OPEN), tok(INT), tok(COMMA), int_lit("4"),
                               tok(PARENTHESES_CLOSE), tok(SQUARE_OPEN), int_lit("0"),
                               tok(SQUARE_CLOSE)}),
              "(index (new int int:4) int:0)");
    // p = new ( int , 4 )
    EXPECT_EQ(expression_dump({ident("p"), tok(ASSIGN), tok(NEW), tok(PARENTHESES_OPEN), tok(INT),
                               tok(COMMA), int_lit("4"), tok(PARENTHESES_CLOSE)}),
              "(= p (new int int:4))");
}

TEST(ParserAllocation, Errors) {
    // new int ;
    EXPECT_TRUE(rejected_in_main_at({tok(NEW), tok(INT), tok(SEMI_COLON)}, 1));
    // new ( int ) ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(NEW), tok(PARENTHESES_OPEN), tok(INT), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)}, 3));
    // new ( , 1 ) ;
    EXPECT_TRUE(rejected_in_main_at({tok(NEW), tok(PARENTHESES_OPEN), tok(COMMA), int_lit("1"),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    2));
    // new ( int , ) ;
    EXPECT_TRUE(rejected_in_main_at({tok(NEW), tok(PARENTHESES_OPEN), tok(INT), tok(COMMA),
                                     tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    4));
    // new ( int , 1 ;
    EXPECT_TRUE(rejected_in_main_at(
        {tok(NEW), tok(PARENTHESES_OPEN), tok(INT), tok(COMMA), int_lit("1"), tok(SEMI_COLON)}, 5));
    // new ( 1 , int ) ;   the type comes first
    EXPECT_TRUE(rejected_in_main_at({tok(NEW), tok(PARENTHESES_OPEN), int_lit("1"), tok(COMMA),
                                     tok(INT), tok(PARENTHESES_CLOSE), tok(SEMI_COLON)},
                                    2));
}

// ---------------------------------------------------------------------------------------------
// errors: stray tokens, end of input, reported positions
// ---------------------------------------------------------------------------------------------

TEST(ParserError, StrayTokenAtTopLevel) {
    EXPECT_TRUE(rejected_at({tok(SEMI_COLON)}, 0));
    EXPECT_TRUE(rejected_at({tok(CURLY_CLOSE)}, 0));
    EXPECT_TRUE(rejected_at({ident("x")}, 0));
    EXPECT_TRUE(rejected_at({int_lit("1")}, 0));
    EXPECT_TRUE(rejected_at({tok(RETURN), tok(SEMI_COLON)}, 0));
    EXPECT_TRUE(rejected_at({tok(CURLY_OPEN), tok(CURLY_CLOSE)}, 0));
}

TEST(ParserError, StrayTokenAfterAValidDeclaration) {
    // define let x : int ; ;
    EXPECT_TRUE(rejected_at(
        {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(SEMI_COLON), tok(SEMI_COLON)},
        6));
    // fn main ( ) { } ;
    EXPECT_TRUE(rejected_at(in_main({tok(CURLY_CLOSE), tok(SEMI_COLON)}), main_prefix_length + 1));
}

TEST(ParserError, MessagesAreRuntimeErrorsStartingWithParserError) {
    const std::vector<token> tokens = on_one_line({tok(SEMI_COLON)});
    try {
        const ast_root_ptr root = parse(tokens);
        FAIL() << "parsed as " << dump(*root);
    } catch (const std::runtime_error &error) {
        EXPECT_TRUE(std::string_view(error.what()).starts_with("SYN001:")) << error.what();
    }
}

// every token type on its own is either trivia or a syntax error, never a crash
TEST(ParserError, AnySingleTokenIsHandled) {
    const std::vector<token> singles = {
        tok(INT),
        tok(BOOL),
        tok(CHAR),
        tok(BYTE),
        tok(FLOAT),
        tok(IF),
        tok(ELIF),
        tok(ELSE),
        tok(WHILE),
        tok(FOR),
        tok(RETURN),
        tok(STEP),
        tok(REJECT),
        tok(INV),
        tok(UPDATE),
        tok(LET),
        tok(IMUT),
        tok(STATE),
        tok(DEFINE),
        tok(FN),
        tok(STRUCT),
        tok(IMPL),
        tok(THIS),
        tok(NEW),
        ident("x"),
        ident("main"),
        tok(PLUS),
        tok(MINUS),
        tok(MULTIPLY),
        tok(DIVIDE),
        tok(DIVIDE_FLOOR),
        tok(EXPONENT),
        tok(XOR),
        tok(BIT_AND),
        tok(BIT_OR),
        tok(LOGICAL_AND),
        tok(LOGICAL_OR),
        tok(NOT),
        tok(ADDRESS),
        tok(DEREFERENCE),
        tok(POINTER),
        tok(ACCESS),
        tok(EQUAL),
        tok(ASSIGN),
        tok(NOT_EQUAL),
        tok(LESSER),
        tok(GREATER),
        tok(LESS_OR_EQUAL),
        tok(GREATER_OR_EQUAL),
        tok(LEFT_SHIFT),
        tok(RIGHT_SHIFT),
        int_lit("1"),
        float_lit("1.5"),
        char_lit("'a'"),
        bool_lit("true"),
        base2_lit("2b1"),
        base16_lit("16bf"),
        string_lit("\"s\""),
        tok(CURLY_OPEN),
        tok(CURLY_CLOSE),
        tok(PARENTHESES_OPEN),
        tok(PARENTHESES_CLOSE),
        tok(SQUARE_OPEN),
        tok(SQUARE_CLOSE),
        tok(SEMI_COLON),
        tok(COLON),
        tok(COMMA),
    };
    for (const token &single : singles) {
        const std::string message = parse_error(on_one_line({single}));
        EXPECT_TRUE(message.starts_with("SYN001: unexpected"))
            << single.lexeme << ": " << message;
    }
}

struct truncated_case {
    std::string_view name;
    std::vector<token> tokens;
};

void PrintTo(const truncated_case &value, std::ostream *out) {
    *out << value.name;
}

std::string truncated_case_name(const testing::TestParamInfo<truncated_case> &info) {
    return std::string(info.param.name);
}

// the unplaced tokens of: fn main ( ) { <body>   with nothing after it
std::vector<token> open_main(const std::vector<token> &body) {
    std::vector<token> tokens = in_main(body);
    tokens.pop_back();
    return tokens;
}

std::vector<truncated_case> truncated_cases() {
    return {
        {"AfterDefine", {tok(DEFINE)}},
        {"AfterLet", {tok(DEFINE), tok(LET)}},
        {"AfterBindingName", {tok(DEFINE), tok(LET), ident("x")}},
        {"AfterBindingColon", {tok(DEFINE), tok(LET), ident("x"), tok(COLON)}},
        {"AfterArrayDimension",
         {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(SQUARE_OPEN), int_lit("3"),
          tok(SQUARE_CLOSE)}},
        {"AfterPointerArrow", {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(POINTER)}},
        {"AfterGlobalType", {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT)}},
        {"AfterGlobalAssign",
         {tok(DEFINE), tok(LET), ident("x"), tok(COLON), tok(INT), tok(ASSIGN)}},
        {"AfterStateBinding", {tok(DEFINE), tok(STATE), ident("x"), tok(COLON), tok(INT)}},
        {"AfterFn", {tok(FN)}},
        {"AfterFunctionName", {tok(FN), ident("f")}},
        {"InsideParameterClause", {tok(FN), ident("f"), tok(PARENTHESES_OPEN)}},
        {"AfterParameterComma",
         {tok(FN), ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COLON), tok(INT),
          tok(COMMA)}},
        {"AfterParameterClause",
         {tok(FN), ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}},
        {"AfterReturnType",
         {tok(FN), ident("f"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(COLON),
          tok(INT)}},
        {"AfterMainName", {tok(FN), ident("main")}},
        {"AfterMainSignature",
         {tok(FN), ident("main"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}},
        {"AfterUpdate", {tok(UPDATE)}},
        {"AfterUpdateStateClause",
         {tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}},
        {"AfterUpdateBothClauses",
         {tok(UPDATE), tok(FN), ident("t"), tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE),
          tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE)}},
        {"InsideStruct", {tok(STRUCT), ident("P"), tok(CURLY_OPEN)}},
        {"InsideStructField",
         {tok(STRUCT), ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), tok(INT)}},
        {"BeforeStructSemiColon", {tok(STRUCT), ident("P"), tok(CURLY_OPEN), tok(CURLY_CLOSE)}},
        {"InsideImpl", {tok(IMPL), ident("P"), tok(CURLY_OPEN)}},
        {"InsideImplAfterMethod",
         {tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(FN), ident("f"), tok(PARENTHESES_OPEN),
          tok(PARENTHESES_CLOSE), tok(COLON), tok(INT), tok(CURLY_OPEN), tok(CURLY_CLOSE)}},
        {"InsideMethodBody",
         {tok(IMPL), ident("P"), tok(CURLY_OPEN), tok(UPDATE), tok(FN), ident("f"),
          tok(PARENTHESES_OPEN), tok(PARENTHESES_CLOSE), tok(CURLY_OPEN)}},
        {"InsideInvariant", {tok(INV), tok(CURLY_OPEN), ident("x")}},
        {"BeforeInvariantSemiColon", {tok(INV), tok(CURLY_OPEN), ident("x"), tok(CURLY_CLOSE)}},
        {"InsideEmptyBody", open_main({})},
        {"AfterTrailingTrivia", open_main({whitespace("\n"), comment("# end")})},
        {"AfterExpression", open_main({ident("x")})},
        {"AfterBinaryOperator", open_main({ident("x"), tok(PLUS)})},
        {"AfterUnaryOperator", open_main({tok(MINUS)})},
        {"AfterAssign", open_main({ident("x"), tok(ASSIGN)})},
        {"AfterLocalLet", open_main({tok(LET), ident("x"), tok(COLON), tok(INT)})},
        {"AfterReturn", open_main({tok(RETURN)})},
        {"InsideIfCondition", open_main({tok(IF), tok(PARENTHESES_OPEN), ident("x")})},
        {"AfterIfCondition",
         open_main({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE)})},
        {"AfterElse", open_main({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                                 tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(ELSE)})},
        {"AfterElif", open_main({tok(IF), tok(PARENTHESES_OPEN), ident("x"), tok(PARENTHESES_CLOSE),
                                 tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(ELIF)})},
        {"InsideWhileBody", open_main({tok(WHILE), tok(PARENTHESES_OPEN), ident("x"),
                                       tok(PARENTHESES_CLOSE), tok(CURLY_OPEN)})},
        {"InsideForInitialiser", open_main({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"),
                                            tok(COLON), tok(INT), tok(ASSIGN), int_lit("0")})},
        {"InsideForCondition",
         open_main({tok(FOR), tok(PARENTHESES_OPEN), tok(LET), ident("i"), tok(COLON), tok(INT),
                    tok(ASSIGN), int_lit("0"), tok(SEMI_COLON), ident("i"), tok(SEMI_COLON)})},
        {"AfterStep", open_main({tok(STEP)})},
        {"AfterReject", open_main({tok(STEP), tok(CURLY_OPEN), tok(CURLY_CLOSE), tok(REJECT)})},
        {"InsideNestedBlocks", open_main({tok(CURLY_OPEN), tok(CURLY_OPEN), tok(CURLY_OPEN)})},
        {"InsideArgumentClause", open_main({ident("f"), tok(PARENTHESES_OPEN)})},
        {"AfterArgumentComma",
         open_main({ident("f"), tok(PARENTHESES_OPEN), ident("a"), tok(COMMA)})},
        {"InsideSecondArgumentClause", open_main({ident("f"), tok(PARENTHESES_OPEN),
                                                  tok(PARENTHESES_CLOSE), tok(PARENTHESES_OPEN)})},
        {"InsideIndex", open_main({ident("a"), tok(SQUARE_OPEN), int_lit("0")})},
        {"AfterMemberDot", open_main({ident("p"), tok(ACCESS)})},
        {"InsideParentheses", open_main({tok(PARENTHESES_OPEN), ident("a")})},
        {"InsideArrayLiteral", open_main({tok(SQUARE_OPEN), int_lit("1"), tok(COMMA)})},
        {"InsideStructInitialiser",
         open_main({ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON)})},
        {"AfterStructInitialiserField",
         open_main({ident("P"), tok(CURLY_OPEN), ident("x"), tok(COLON), int_lit("1")})},
        {"InsideConversion", open_main({tok(INT), tok(PARENTHESES_OPEN)})},
        {"AfterPrimitiveType", open_main({tok(INT)})},
        {"InsideAllocation", open_main({tok(NEW), tok(PARENTHESES_OPEN), tok(INT), tok(COMMA)})},
        {"AfterAllocationType", open_main({tok(NEW), tok(PARENTHESES_OPEN), tok(INT)})},
        {"AfterPowerOperator", open_main({ident("a"), tok(EXPONENT)})},
        {"AfterSplitDereference", open_main({ident("x"), tok(DEREFERENCE)})},
        {"AfterPrefixDereference", open_main({tok(DEREFERENCE)})},
    };
}

class ParserTruncatedInput : public testing::TestWithParam<truncated_case> {};

TEST_P(ParserTruncatedInput, IsUnexpectedEndOfInput) {
    EXPECT_TRUE(rejected_at_end(GetParam().tokens));
}

INSTANTIATE_TEST_SUITE_P(EveryRuleFamily, ParserTruncatedInput,
                         testing::ValuesIn(truncated_cases()), truncated_case_name);

// one declaration that uses most of the grammar. a proper prefix of one declaration can never
// be a whole program, so cutting it anywhere must report the end of input (and never crash)
TEST(ParserError, EveryProperPrefixOfADeclarationEndsUnexpectedly) {
    const std::vector<token> tokens = on_one_line(in_main({
        // let a : [ 3 ] -> int = [ 1 , 2b1 ] ;
        tok(LET),
        ident("a"),
        tok(COLON),
        tok(SQUARE_OPEN),
        int_lit("3"),
        tok(SQUARE_CLOSE),
        tok(POINTER),
        tok(INT),
        tok(ASSIGN),
        tok(SQUARE_OPEN),
        int_lit("1"),
        tok(COMMA),
        base2_lit("2b1"),
        tok(SQUARE_CLOSE),
        tok(SEMI_COLON),
        // if ( a [ 0 ] <- 1 ) { f ( s ) ( x ) ; }
        tok(IF),
        tok(PARENTHESES_OPEN),
        ident("a"),
        tok(SQUARE_OPEN),
        int_lit("0"),
        tok(SQUARE_CLOSE),
        tok(DEREFERENCE),
        int_lit("1"),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        ident("f"),
        tok(PARENTHESES_OPEN),
        ident("s"),
        tok(PARENTHESES_CLOSE),
        tok(PARENTHESES_OPEN),
        ident("x"),
        tok(PARENTHESES_CLOSE),
        tok(SEMI_COLON),
        tok(CURLY_CLOSE),
        // elif ( ! b ) { return ; }
        tok(ELIF),
        tok(PARENTHESES_OPEN),
        tok(NOT),
        ident("b"),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        tok(RETURN),
        tok(SEMI_COLON),
        tok(CURLY_CLOSE),
        // else { step { p . x = P { y : 2 ~ - 3 } ; } reject { } }
        tok(ELSE),
        tok(CURLY_OPEN),
        tok(STEP),
        tok(CURLY_OPEN),
        ident("p"),
        tok(ACCESS),
        ident("x"),
        tok(ASSIGN),
        ident("P"),
        tok(CURLY_OPEN),
        ident("y"),
        tok(COLON),
        int_lit("2"),
        tok(EXPONENT),
        tok(MINUS),
        int_lit("3"),
        tok(CURLY_CLOSE),
        tok(SEMI_COLON),
        tok(CURLY_CLOSE),
        tok(REJECT),
        tok(CURLY_OPEN),
        tok(CURLY_CLOSE),
        tok(CURLY_CLOSE),
        // for ( let i : int = 0 ; i < 3 ; i = i + 1 ) { while ( true ) { inv { <- q } ; } }
        tok(FOR),
        tok(PARENTHESES_OPEN),
        tok(LET),
        ident("i"),
        tok(COLON),
        tok(INT),
        tok(ASSIGN),
        int_lit("0"),
        tok(SEMI_COLON),
        ident("i"),
        tok(LESSER),
        int_lit("3"),
        tok(SEMI_COLON),
        ident("i"),
        tok(ASSIGN),
        ident("i"),
        tok(PLUS),
        int_lit("1"),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        tok(WHILE),
        tok(PARENTHESES_OPEN),
        bool_lit("true"),
        tok(PARENTHESES_CLOSE),
        tok(CURLY_OPEN),
        tok(INV),
        tok(CURLY_OPEN),
        tok(DEREFERENCE),
        ident("q"),
        tok(CURLY_CLOSE),
        tok(SEMI_COLON),
        tok(CURLY_CLOSE),
        tok(CURLY_CLOSE),
        // return new ( char , int ( "s" [ 0 ] ) ) ;
        tok(RETURN),
        tok(NEW),
        tok(PARENTHESES_OPEN),
        tok(CHAR),
        tok(COMMA),
        tok(INT),
        tok(PARENTHESES_OPEN),
        string_lit("\"s\""),
        tok(SQUARE_OPEN),
        int_lit("0"),
        tok(SQUARE_CLOSE),
        tok(PARENTHESES_CLOSE),
        tok(PARENTHESES_CLOSE),
        tok(SEMI_COLON),
    }));
    ASSERT_EQ(parse(tokens)->declarations.size(), 1U);

    for (std::size_t length = 1; length < tokens.size(); length++) {
        const std::vector<token> prefix(tokens.begin(),
                                        tokens.begin() + static_cast<std::ptrdiff_t>(length));
        EXPECT_TRUE(parse_error(prefix).starts_with(end_of_input_message))
            << "prefix of " << length << " tokens, ending at '" << prefix.back().lexeme << "'";
    }
}

TEST(ParserError, PositionIsThatOfTheOffendingToken) {
    // fn main() {
    //     let x: int = 1
    //     return;
    // }
    const std::vector<token> tokens = {
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(tok(LET), 1, 4),
        at(ident("x"), 1, 8),
        at(tok(COLON), 1, 9),
        at(tok(INT), 1, 11),
        at(tok(ASSIGN), 1, 15),
        at(int_lit("1"), 1, 17),
        at(tok(RETURN), 2, 4),
        at(tok(SEMI_COLON), 2, 10),
        at(tok(CURLY_CLOSE), 3, 0),
    };
    const std::string message = parse_error(tokens);
    EXPECT_TRUE(is_unexpected_token(message, tokens[11]));
    EXPECT_TRUE(message.starts_with("SYN001: unexpected token 'return'")) << message;
    EXPECT_TRUE(message.ends_with("at line 2, column 4")) << message;
}

TEST(ParserError, PositionSkipsTriviaBeforeTheOffendingToken) {
    // define let x : int   # no semi colon
    // }
    const std::vector<token> tokens = {
        at(tok(DEFINE), 4, 0),
        at(whitespace(" "), 4, 6),
        at(tok(LET), 4, 7),
        at(whitespace(" "), 4, 10),
        at(ident("x"), 4, 11),
        at(tok(COLON), 4, 12),
        at(tok(INT), 4, 14),
        at(whitespace("   "), 4, 17),
        at(comment("# no semi colon"), 4, 20),
        at(whitespace("\n"), 4, 35),
        at(tok(CURLY_CLOSE), 5, 0),
    };
    const std::string message = parse_error(tokens);
    EXPECT_TRUE(message.starts_with("SYN001: unexpected token '}'")) << message;
    EXPECT_TRUE(message.ends_with("at line 5, column 0")) << message;
}

TEST(ParserError, LiteralOutOfRangeReportsTheLiteralPosition) {
    // define let x : int = 2147483648 ;   on line 7
    const std::vector<token> tokens = {
        at(tok(DEFINE), 7, 2),
        at(tok(LET), 7, 9),
        at(ident("x"), 7, 13),
        at(tok(COLON), 7, 14),
        at(tok(INT), 7, 16),
        at(tok(ASSIGN), 7, 20),
        at(int_lit("2147483648"), 7, 22),
        at(tok(SEMI_COLON), 7, 32),
    };
    EXPECT_EQ(parse_error(tokens),
              "SYN002: literal '2147483648' is out of range at line 7, column 22");
}

TEST(ParserError, FirstErrorIsTheOneReported) {
    // define let : ; ;   both the missing name and the missing type are errors
    EXPECT_TRUE(
        rejected_at({tok(DEFINE), tok(LET), tok(COLON), tok(SEMI_COLON), tok(SEMI_COLON)}, 2));
}

// ---------------------------------------------------------------------------------------------
// source spans: a node's span runs from its first token to just past its last token
// ---------------------------------------------------------------------------------------------

TEST(ParserSpan, FunctionBlockStatementAndBinaryExpression) {
    // fn add(a: int): int {
    //     return a + bb;
    // }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("add"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 6),
        at(ident("a"), 0, 7),
        at(tok(COLON), 0, 8),
        at(tok(INT), 0, 10),
        at(tok(PARENTHESES_CLOSE), 0, 13),
        at(tok(COLON), 0, 14),
        at(tok(INT), 0, 16),
        at(tok(CURLY_OPEN), 0, 20),
        at(tok(RETURN), 1, 4),
        at(ident("a"), 1, 11),
        at(tok(PLUS), 1, 13),
        at(ident("bb"), 1, 15),
        at(tok(SEMI_COLON), 1, 17),
        at(tok(CURLY_CLOSE), 2, 0),
    });
    const function_decl &function = as<function_decl>(*root->declarations.at(0));
    EXPECT_EQ(span_text(function.source), "0:0-2:1");
    EXPECT_EQ(span_text(function.name->source), "0:3-0:6");
    EXPECT_EQ(span_text(function.ordinary_parameters.at(0).declared_type->source), "0:10-0:13");
    EXPECT_EQ(span_text(function.return_type->source), "0:16-0:19");
    EXPECT_EQ(span_text(function.body->source), "0:20-2:1");

    const return_stmt &statement = as<return_stmt>(*function.body->statements.at(0));
    EXPECT_EQ(span_text(statement.source), "1:4-1:18");

    const binary_expr &sum = as<binary_expr>(*statement.value);
    EXPECT_EQ(span_text(sum.source), "1:11-1:17");
    EXPECT_EQ(span_text(sum.left->source), "1:11-1:12");
    EXPECT_EQ(span_text(sum.right->source), "1:15-1:17");
}

TEST(ParserSpan, MultiLineStringLiteralEndsOnItsLastLine) {
    // fn main() {
    //     s = "ab
    // cd";
    // }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(ident("s"), 1, 4),
        at(tok(ASSIGN), 1, 6),
        at(string_lit("\"ab\ncd\""), 1, 8),
        at(tok(SEMI_COLON), 2, 3),
        at(tok(CURLY_CLOSE), 3, 0),
    });
    const expr_stmt &statement = as<expr_stmt>(only_statement(*root));
    const assignment_expr &assignment = as<assignment_expr>(*statement.expression);
    EXPECT_EQ(span_text(assignment.value->source), "1:8-2:3");
    EXPECT_EQ(span_text(assignment.source), "1:4-2:3");
    EXPECT_EQ(span_text(statement.source), "1:4-2:4");
}

TEST(ParserSpan, StringLiteralEndingInANewlineEndsAtColumnOneOfTheNextLine) {
    // the lexeme is: quote, a, newline, quote
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(string_lit("\"a\n\""), 1, 0),
        at(tok(SEMI_COLON), 2, 1),
        at(tok(CURLY_CLOSE), 2, 3),
    });
    EXPECT_EQ(span_text(only_expression(*root).source), "1:0-2:1");
}

// the span of a global runs from "define" to its ";"
TEST(ParserSpan, GlobalVariableCoversDefineThroughSemiColon) {
    // define let x: int = 1;   starting at line 2, column 4
    const ast_root_ptr root = parse({
        at(tok(DEFINE), 2, 4),
        at(tok(LET), 2, 11),
        at(ident("x"), 2, 15),
        at(tok(COLON), 2, 16),
        at(tok(INT), 2, 18),
        at(tok(ASSIGN), 2, 22),
        at(int_lit("1"), 2, 24),
        at(tok(SEMI_COLON), 2, 25),
    });
    const variable_decl &declaration = as<variable_decl>(*root->declarations.at(0));
    EXPECT_EQ(span_text(declaration.source), "2:4-2:26");
    EXPECT_EQ(span_text(declaration.name->source), "2:15-2:16");
    EXPECT_EQ(span_text(declaration.declared_type->source), "2:18-2:21");
    EXPECT_EQ(span_text(declaration.initializer->source), "2:24-2:25");
}

// for a local, the ";" belongs to the statement and not to the declaration
TEST(ParserSpan, LocalVariableStatementTakesTheSemiColon) {
    // fn main() { let x: [3]->int; }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(tok(LET), 0, 12),
        at(ident("x"), 0, 16),
        at(tok(COLON), 0, 17),
        at(tok(SQUARE_OPEN), 0, 19),
        at(int_lit("3"), 0, 20),
        at(tok(SQUARE_CLOSE), 0, 21),
        at(tok(POINTER), 0, 22),
        at(tok(INT), 0, 24),
        at(tok(SEMI_COLON), 0, 27),
        at(tok(CURLY_CLOSE), 0, 29),
    });
    const decl_stmt &statement = as<decl_stmt>(only_statement(*root));
    EXPECT_EQ(span_text(statement.source), "0:12-0:28");
    const variable_decl &declaration = as<variable_decl>(*statement.declaration);
    EXPECT_EQ(span_text(declaration.source), "0:12-0:27");

    // each type spans from its own first token to the end of the innermost type
    const array_type_syntax &array = as<array_type_syntax>(*declaration.declared_type);
    EXPECT_EQ(span_text(array.source), "0:19-0:27");
    EXPECT_EQ(span_text(array.size->source), "0:20-0:21");
    EXPECT_EQ(span_text(array.element->source), "0:22-0:27");
}

// main's void return type is not written, parser.cpp gives it the span of "main()"
TEST(ParserSpan, MainSignature) {
    const ast_root_ptr root = parse({
        at(tok(FN), 1, 0),
        at(ident("main"), 1, 3),
        at(tok(PARENTHESES_OPEN), 1, 7),
        at(tok(PARENTHESES_CLOSE), 1, 8),
        at(tok(CURLY_OPEN), 1, 10),
        at(tok(CURLY_CLOSE), 1, 11),
    });
    const function_decl &function = as<function_decl>(*root->declarations.at(0));
    EXPECT_EQ(span_text(function.source), "1:0-1:12");
    EXPECT_EQ(span_text(function.name->source), "1:3-1:7");
    EXPECT_EQ(span_text(function.return_type->source), "1:3-1:9");
    EXPECT_EQ(span_text(function.body->source), "1:10-1:12");
}

// x<-1: the "<-" token at column 5 becomes "<" at column 5 and "-" at column 6
TEST(ParserSpan, SplitDereferenceGivesTheMinusItsOwnColumn) {
    // fn main() {
    //     x<-1;
    // }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(ident("x"), 1, 4),
        at(tok(DEREFERENCE), 1, 5),
        at(int_lit("1"), 1, 7),
        at(tok(SEMI_COLON), 1, 8),
        at(tok(CURLY_CLOSE), 2, 0),
    });
    const binary_expr &comparison = as<binary_expr>(only_expression(*root));
    EXPECT_EQ(comparison.operation, binary_op::LT);
    EXPECT_EQ(span_text(comparison.source), "1:4-1:8");
    const unary_expr &negation = as<unary_expr>(*comparison.right);
    EXPECT_EQ(negation.operation, unary_op::NEGATE);
    EXPECT_EQ(span_text(negation.source), "1:6-1:8");
    EXPECT_EQ(span_text(negation.right->source), "1:7-1:8");
}

TEST(ParserSpan, PostfixRunsFromTheObjectToTheEndOfTheSuffix) {
    // fn main() { p.x[10](a)(b); }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(ident("p"), 0, 12),
        at(tok(ACCESS), 0, 13),
        at(ident("x"), 0, 14),
        at(tok(SQUARE_OPEN), 0, 15),
        at(int_lit("10"), 0, 16),
        at(tok(SQUARE_CLOSE), 0, 18),
        at(tok(PARENTHESES_OPEN), 0, 19),
        at(ident("a"), 0, 20),
        at(tok(PARENTHESES_CLOSE), 0, 21),
        at(tok(PARENTHESES_OPEN), 0, 22),
        at(ident("b"), 0, 23),
        at(tok(PARENTHESES_CLOSE), 0, 24),
        at(tok(SEMI_COLON), 0, 25),
        at(tok(CURLY_CLOSE), 0, 27),
    });
    const postfix_expr &update_call = as<postfix_expr>(only_expression(*root));
    EXPECT_EQ(span_text(update_call.source), "0:12-0:25");
    const postfix_expr &subscript = as<postfix_expr>(*update_call.obj);
    EXPECT_EQ(span_text(subscript.source), "0:12-0:19");
    const postfix_expr &member = as<postfix_expr>(*subscript.obj);
    EXPECT_EQ(span_text(member.source), "0:12-0:15");
    EXPECT_EQ(span_text(member.obj->source), "0:12-0:13");
}

TEST(ParserSpan, PrefixOperatorStartsAtTheOperator) {
    // fn main() { -<-p; }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(tok(MINUS), 0, 12),
        at(tok(DEREFERENCE), 0, 13),
        at(ident("p"), 0, 15),
        at(tok(SEMI_COLON), 0, 16),
        at(tok(CURLY_CLOSE), 0, 18),
    });
    const unary_expr &negation = as<unary_expr>(only_expression(*root));
    EXPECT_EQ(span_text(negation.source), "0:12-0:16");
    EXPECT_EQ(span_text(negation.right->source), "0:13-0:16");
}

// parser.cpp: a parenthesised expression keeps the span of what is inside the parentheses.
// the grammar files say nothing about spans, so this pins the documented behaviour
TEST(ParserSpan, ParenthesesAreNotPartOfTheSpan) {
    // fn main() { (a + b); }
    const ast_root_ptr root = parse({
        at(tok(FN), 0, 0),
        at(ident("main"), 0, 3),
        at(tok(PARENTHESES_OPEN), 0, 7),
        at(tok(PARENTHESES_CLOSE), 0, 8),
        at(tok(CURLY_OPEN), 0, 10),
        at(tok(PARENTHESES_OPEN), 0, 12),
        at(ident("a"), 0, 13),
        at(tok(PLUS), 0, 15),
        at(ident("b"), 0, 17),
        at(tok(PARENTHESES_CLOSE), 0, 18),
        at(tok(SEMI_COLON), 0, 19),
        at(tok(CURLY_CLOSE), 0, 21),
    });
    const expr_stmt &statement = as<expr_stmt>(only_statement(*root));
    EXPECT_EQ(span_text(statement.expression->source), "0:13-0:18");
    // the statement itself still starts at the "("
    EXPECT_EQ(span_text(statement.source), "0:12-0:20");
}

TEST(ParserSpan, TriviaIsNotPartOfASpan) {
    // "  # c\n  inv { x }  ;  " with the trivia as tokens
    const ast_root_ptr root = parse({
        at(whitespace("  "), 0, 0),
        at(comment("# c"), 0, 2),
        at(whitespace("\n  "), 0, 5),
        at(tok(INV), 1, 2),
        at(whitespace(" "), 1, 5),
        at(tok(CURLY_OPEN), 1, 6),
        at(ident("x"), 1, 8),
        at(tok(CURLY_CLOSE), 1, 10),
        at(whitespace("  "), 1, 11),
        at(tok(SEMI_COLON), 1, 13),
        at(whitespace("  "), 1, 14),
    });
    EXPECT_EQ(span_text(root->declarations.at(0)->source), "1:2-1:14");
}

} // namespace
