#include "../src/ast/ast_root.h"
#include "../src/ast/declaration.h"
#include "../src/ast/expression.h"
#include "../src/ast/expression_base.h"
#include "../src/ast/source.h"
#include "../src/ast/statement.h"
#include "../src/ast/statement_base.h"
#include "../src/ast/type.h"
#include "../src/parser/parser.h"
#include "../src/tokeniser/tokeniser.h"
#include "../src/tokens/token.h"
#include "ast_dump.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Integration tests: source text goes through tokenise and then parse, and the tests check the
// AST (as an ast_dump s-expression) or the error. They cover what only shows up when the two
// stages meet. Per-token and per-rule tables belong in the tokeniser and parser unit tests.

namespace {

// ---------------------------------------------------------------------------------------------
// running the two stages
// ---------------------------------------------------------------------------------------------

ast_root_ptr parse_source(std::string_view source) {
    return parse(tokenise(to_lines(source)));
}

std::string parse_and_dump(std::string_view source) {
    return dump(*parse_source(source));
}

// most tests only care about a few statements, so this gives them a program to live in
std::string in_main(std::string_view statements) {
    return std::format("fn main() {{ {} }}", statements);
}

// throws std::bad_cast (failing the test) if node is not a node_type
template <typename node_type, typename base_type> const node_type &as(const base_type &node) {
    return dynamic_cast<const node_type &>(node);
}

// the function at the given index of the top level declarations
const function_decl &function_at(const program &root, size_t index) {
    return as<function_decl>(*root.declarations.at(index));
}

// the dumps of the statements of main, separated by spaces, without the wrapper around them
std::string dump_statements(std::string_view statements) {
    const ast_root_ptr root = parse_source(in_main(statements));
    std::string text;
    for (const stmt_ptr &statement : function_at(*root, 0).body->statements) {
        if (!text.empty()) {
            text += ' ';
        }
        text += dump(*statement);
    }
    return text;
}

// the dump of one expression, parsed as an expression statement in main
std::string dump_expression(std::string_view expression) {
    const ast_root_ptr root = parse_source(in_main(std::format("{};", expression)));
    const std::vector<stmt_ptr> &statements = function_at(*root, 0).body->statements;
    if (statements.size() != 1) {
        return std::format("<{} statements>", statements.size());
    }
    return dump(*as<expr_stmt>(*statements.front()).expression);
}

// source with separator after every token, and its own whitespace and comments dropped
std::string respaced(std::string_view source, std::string_view separator) {
    std::string text;
    for (const token &tok : tokenise(to_lines(source))) {
        if (tok.type == token_type::WHITESPACE || tok.type == token_type::COMMENT) {
            continue;
        }
        text += tok.lexeme;
        text += separator;
    }
    return text;
}

std::string repeated(std::string_view text, size_t count) {
    std::string result;
    result.reserve(text.size() * count);
    for (size_t i = 0; i < count; i++) {
        result += text;
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// positions
// tests name a place by the text found there, so nobody has to count columns by hand
// ---------------------------------------------------------------------------------------------

// 0-indexed, like token positions and the positions in error messages
struct text_position {
    int line;
    int column;
};

// the position reached by reading text from start
text_position advanced(const text_position &start, std::string_view text) {
    text_position position = start;
    for (const char ch : text) {
        if (ch == '\n') {
            position.line++;
            position.column = 0;
        } else {
            position.column++;
        }
    }
    return position;
}

// where the given occurrence (0 = first) of needle starts in source
text_position position_of(std::string_view source, std::string_view needle, size_t occurrence = 0) {
    size_t offset = source.find(needle);
    for (size_t i = 0; i < occurrence && offset != std::string_view::npos; i++) {
        offset = source.find(needle, offset + 1);
    }
    if (offset == std::string_view::npos) {
        ADD_FAILURE() << "the test source has no occurrence " << occurrence << " of \"" << needle
                      << '"';
        return {-1, -1};
    }
    return advanced({0, 0}, source.substr(0, offset));
}

// true if span covers exactly the given occurrence of text in source
testing::AssertionResult covers(std::string_view source, const source_span &span,
                                std::string_view text, size_t occurrence = 0) {
    const text_position start = position_of(source, text, occurrence);
    const text_position end = advanced(start, text);
    if (span.start_line == start.line && span.start_col == start.column &&
        span.end_line == end.line && span.end_col == end.column) {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure()
           << std::format("span is {}:{} to {}:{}, but \"{}\" is at {}:{} to {}:{}",
                          span.start_line, span.start_col, span.end_line, span.end_col, text,
                          start.line, start.column, end.line, end.column);
}

// ---------------------------------------------------------------------------------------------
// error assertions
// ---------------------------------------------------------------------------------------------

// true if running source throws a std::runtime_error whose message has the given start and end
testing::AssertionResult fails_with(std::string_view source, std::string_view message_start,
                                    std::string_view message_end) {
    try {
        const ast_root_ptr root = parse_source(source);
        return testing::AssertionFailure() << "no error, parsed as " << dump(*root);
    } catch (const std::runtime_error &error) {
        const std::string_view message = error.what();
        if (message.starts_with(message_start) && message.ends_with(message_end)) {
            return testing::AssertionSuccess();
        }
        return testing::AssertionFailure() << "the error was \"" << message << "\", expected \""
                                           << message_start << " ... " << message_end << '"';
    }
}

testing::AssertionResult fails_with_exactly(std::string_view source, std::string_view message) {
    return fails_with(source, message, message);
}

// the tokeniser stops at the character shown (written the way the message shows it)
testing::AssertionResult lexer_rejects(std::string_view source, std::string_view shown,
                                       const text_position &where) {
    return fails_with_exactly(source, std::format("LEX001: unexpected character '{}' at line {}, "
                                                  "column {}",
                                                  shown, where.line, where.column));
}

// as above, at the first place the character appears in source
testing::AssertionResult lexer_rejects(std::string_view source, std::string_view character) {
    return lexer_rejects(source, character, position_of(source, character));
}

// the source ends inside the token that starts at unfinished
testing::AssertionResult lexer_rejects_end(std::string_view source, std::string_view unfinished) {
    const text_position start = position_of(source, unfinished);
    return fails_with(
        source, "LEX002: unexpected end of input at line ",
        std::format(" (unfinished token starts at line {}, column {})", start.line, start.column));
}

// the parser stops at the token with the given lexeme
testing::AssertionResult parser_rejects_at(std::string_view source, std::string_view lexeme,
                                           const text_position &where) {
    return fails_with(source, std::format("SYN001: unexpected token '{}'", lexeme),
                      std::format(" at line {}, column {}", where.line, where.column));
}

// as above, with the token starting where marker is first found in source
testing::AssertionResult parser_rejects(std::string_view source, std::string_view lexeme,
                                        std::string_view marker) {
    return parser_rejects_at(source, lexeme, position_of(source, marker));
}

// as above, at the first place the lexeme appears in source
testing::AssertionResult parser_rejects(std::string_view source, std::string_view lexeme) {
    return parser_rejects(source, lexeme, lexeme);
}

testing::AssertionResult parser_rejects_end(std::string_view source) {
    // the message goes on to give the position just past the last token
    return fails_with(source, "SYN001: unexpected end of input at line ", "");
}

// an expression and the dump it must give
struct expression_case {
    std::string_view source;
    std::string_view expected;
};

template <size_t count> void expect_expressions(const std::array<expression_case, count> &cases) {
    for (const expression_case &test_case : cases) {
        SCOPED_TRACE(test_case.source);
        EXPECT_EQ(dump_expression(test_case.source), test_case.expected);
    }
}

// a source the parser must reject, the lexeme of the token it stops at, and text that starts at
// that token (to tell it from earlier tokens with the same lexeme)
struct rejected_case {
    std::string_view source;
    std::string_view lexeme;
    std::string_view marker;
};

template <size_t count> void expect_rejections(const std::array<rejected_case, count> &cases) {
    for (const rejected_case &test_case : cases) {
        SCOPED_TRACE(test_case.source);
        EXPECT_TRUE(parser_rejects(test_case.source, test_case.lexeme, test_case.marker));
    }
}

// every construct of the grammar on a few lines, shared by the layout tests
constexpr std::string_view everything_source = R"(define let g: [3]->int;
struct P { x: int; };
impl P { fn get(): int { return this.x; } update fn set(v: int) { this.x = v; } };
update fn tick(s: P)(n: int) { s.x = s.x + n; }
inv { g[0] != g[1] };
fn main() {
  imut state p: P = P{x: 1};
  for (let i: int = 0; i < 3; i = i + 1) { print("a\n", 'c', 1.5, 2b11, 16bff, true); }
  if (x<-1) { tick(p)(1); } elif (!x) { return; } else { step { p.set(float(2) ~ -1); } reject { } }
  while (false) { let q: ->int = new(int, 4); @q; <-q; [1, 2]; }
}
)";

constexpr std::string_view everything_dump =
    "(program "
    "(let g ([] int:3 (-> int))) "
    "(struct P (x int)) "
    "(impl P "
    "(fn get () int (block (return (. this x)))) "
    "(update-fn set () ((v int)) (block (expr (= (. this x) v))))) "
    "(update-fn tick ((s P)) ((n int)) (block (expr (= (. s x) (+ (. s x) n))))) "
    "(inv (!= (index g int:0) (index g int:1))) "
    "(fn main () void (block "
    "(imut-state p P (struct-init P (x int:1))) "
    "(for (let i int int:0) (< i int:3) (= i (+ i int:1)) (block "
    "(expr (call print str:\"a\\n\" char:'c' float:1.5 int:3 int:255 bool:true)))) "
    "(if ((< x (- int:1)) (block (expr (update-call tick (p) (int:1))))) "
    "((! x) (block (return))) "
    "(else (block (step (block (expr (call (. p set) (~ (convert float int:2) (- int:1))))) "
    "(reject (block)))))) "
    "(while bool:false (block (let q (-> int) (new int int:4)) (expr (@ q)) (expr (<- q)) "
    "(expr (array int:1 int:2)))))))";

} // namespace

// ---------------------------------------------------------------------------------------------
// print and scan
// they used to be keywords. now they are IDENTIFIER tokens and the parser sees ordinary names
// ---------------------------------------------------------------------------------------------

TEST(PrintAndScan, AreOrdinaryCalls) {
    EXPECT_EQ(parse_and_dump("fn main() { print(x); }"),
              "(program (fn main () void (block (expr (call print x)))))");
    EXPECT_EQ(dump_statements("scan(@x);"), "(expr (call scan (@ x)))");
    EXPECT_EQ(dump_statements("print();"), "(expr (call print))");
    EXPECT_EQ(dump_statements(R"(print("total: ", n + 1, '\n');)"),
              R"((expr (call print str:"total: " (+ n int:1) char:'\n')))");
    EXPECT_EQ(dump_statements("scan(@a[0], @p.x);"),
              "(expr (call scan (@ (index a int:0)) (@ (. p x))))");
}

TEST(PrintAndScan, CallsAreExpressions) {
    // a call is a postfix expression, so it can sit anywhere an expression can
    const std::array cases{
        expression_case{"x = scan(@y)", "(= x (call scan (@ y)))"},
        expression_case{"print(scan(@x))", "(call print (call scan (@ x)))"},
        expression_case{"print(x) + scan(@y) * 2",
                        "(+ (call print x) (* (call scan (@ y)) int:2))"},
        expression_case{"print(x)[0].y", "(. (index (call print x) int:0) y)"},
        expression_case{"print(s)(x)", "(update-call print (s) (x))"},
        expression_case{"print<-1", "(< print (- int:1))"},
    };
    expect_expressions(cases);
}

TEST(PrintAndScan, AreValidVariableNames) {
    EXPECT_EQ(dump_statements("let print: int = 1; print = print + 1;"),
              "(let print int int:1) (expr (= print (+ print int:1)))");
    EXPECT_EQ(dump_statements("imut state scan: ->char = @print;"),
              "(imut-state scan (-> char) (@ print))");
    EXPECT_EQ(parse_and_dump("define let print: [2]int;\ndefine state scan: bool = false;\n"),
              "(program (let print ([] int:2 int)) (state scan bool bool:false))");
    EXPECT_EQ(dump_statements("for (let scan: int = 0; scan < print; scan = scan + 1) { }"),
              "(for (let scan int int:0) (< scan print) (= scan (+ scan int:1)) (block))");
}

TEST(PrintAndScan, AreValidFunctionAndParameterNames) {
    EXPECT_EQ(parse_and_dump("fn print(scan: int): int { return scan; }"),
              "(program (fn print ((scan int)) int (block (return scan))))");
    EXPECT_EQ(
        parse_and_dump("update fn scan(print: int)(scan: int) { print = scan; }"),
        "(program (update-fn scan ((print int)) ((scan int)) (block (expr (= print scan)))))");
}

TEST(PrintAndScan, AreValidFieldMethodAndTypeNames) {
    constexpr std::string_view source = R"(
struct print { print: int; scan: bool; };
impl print {
    fn print(): int { return this.print; }
    update fn scan(print: int) { this.scan = print > 0; }
};
fn main() {
    let scan: print = print{print: 1, scan: true};
    scan.scan(scan.print());
    print(scan.print, scan.scan);
}
)";
    EXPECT_EQ(parse_and_dump(source),
              "(program "
              "(struct print (print int) (scan bool)) "
              "(impl print "
              "(fn print () int (block (return (. this print)))) "
              "(update-fn scan () ((print int)) (block (expr (= (. this scan) (> print int:0)))))) "
              "(fn main () void (block "
              "(let scan print (struct-init print (print int:1) (scan bool:true))) "
              "(expr (call (. scan scan) (call (. scan print)))) "
              "(expr (call print (. scan print) (. scan scan))))))");
}

TEST(PrintAndScan, LongerNamesStartingWithThemAreSeparateIdentifiers) {
    EXPECT_EQ(dump_statements("printer(scanned, print_, scan2, Print, SCAN);"),
              "(expr (call printer scanned print_ scan2 Print SCAN))");
}

TEST(PrintAndScan, StatementFormWithoutParenthesesIsNotACall) {
    // with no print keyword, "print x" is two operands in a row
    EXPECT_TRUE(parser_rejects("fn main() { print x; }", "x"));
    EXPECT_TRUE(parser_rejects("fn main() { scan @x; }", "@"));
    EXPECT_TRUE(parser_rejects(R"(fn main() { print "hi"; })", R"("hi")"));
}

// ---------------------------------------------------------------------------------------------
// "<-" is always one DEREFERENCE token. the parser splits it into "<" "-" after an operand
// ---------------------------------------------------------------------------------------------

TEST(DereferenceSplit, AfterAnOperandMeansLessThanMinus) {
    const std::array cases{
        expression_case{"x<-1", "(< x (- int:1))"},
        expression_case{"x < -1", "(< x (- int:1))"},
        expression_case{"a[0]<-b", "(< (index a int:0) (- b))"},
        expression_case{"f()<-1", "(< (call f) (- int:1))"},
        expression_case{"(x)<-y", "(< x (- y))"},
        expression_case{"p.x<-1", "(< (. p x) (- int:1))"},
        expression_case{"this<-1", "(< this (- int:1))"},
        expression_case{"1<-2", "(< int:1 (- int:2))"},
        expression_case{"1.5<-.5", "(< float:1.5 (- float:0.5))"},
        expression_case{"'a'<-1", "(< char:'a' (- int:1))"},
        expression_case{"true<-1", "(< bool:true (- int:1))"},
        expression_case{"[1]<-x", "(< (array int:1) (- x))"},
        expression_case{"P{}<-x", "(< (struct-init P) (- x))"},
        expression_case{"int(x)<-1", "(< (convert int x) (- int:1))"},
        expression_case{"new(int, 1)<-p", "(< (new int int:1) (- p))"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, WhitespaceAroundTheArrowDoesNotChangeTheSplit) {
    // the parser never sees whitespace, so "directly after an operand" is about tokens
    const std::array cases{
        expression_case{"x <- 1", "(< x (- int:1))"},
        expression_case{"x<- 1", "(< x (- int:1))"},
        expression_case{"x <-1", "(< x (- int:1))"},
        expression_case{"x\n<-\n1", "(< x (- int:1))"},
        expression_case{"x # the bound\n<- 1", "(< x (- int:1))"},
        expression_case{"x <# split by hand\n- 1", "(< x (- int:1))"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, SplitHalvesKeepTheirPrecedence) {
    const std::array cases{
        expression_case{"a + b<-c", "(< (+ a b) (- c))"},
        expression_case{"a<-b + c", "(< a (+ (- b) c))"},
        expression_case{"a<-b * c", "(< a (* (- b) c))"},
        expression_case{"a<-b ~ 2", "(< a (- (~ b int:2)))"},
        expression_case{"x<-1<-2", "(< (< x (- int:1)) (- int:2))"},
        expression_case{"x<-1 == y", "(== (< x (- int:1)) y)"},
        expression_case{"x = y<-1", "(= x (< y (- int:1)))"},
        expression_case{"a && b<-c || d", "(|| (&& a (< b (- c))) d)"},
        expression_case{"a | b<-c & d", "(< (| a b) (& (- c) d))"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, WithNoOperandBeforeItIsADereference) {
    const std::array cases{
        expression_case{"<-p", "(<- p)"},
        expression_case{"<-<-pp", "(<- (<- pp))"},
        expression_case{"<- <- pp", "(<- (<- pp))"},
        expression_case{"x = <-p", "(= x (<- p))"},
        expression_case{"x=<-p", "(= x (<- p))"},
        expression_case{"x < <-p", "(< x (<- p))"},
        expression_case{"x > <-p", "(> x (<- p))"},
        expression_case{"x <= <-p", "(<= x (<- p))"},
        expression_case{"x == <-p", "(== x (<- p))"},
        expression_case{"a + <-p", "(+ a (<- p))"},
        expression_case{"a - <-p", "(- a (<- p))"},
        expression_case{"a * <-p", "(* a (<- p))"},
        expression_case{"a && <-p", "(&& a (<- p))"},
        expression_case{"-<-p", "(- (<- p))"},
        expression_case{"!<-p", "(! (<- p))"},
        expression_case{"@<-p", "(@ (<- p))"},
        expression_case{"<-@x", "(<- (@ x))"},
        expression_case{"2 ~ <-p", "(~ int:2 (<- p))"},
        expression_case{"<-p ~ 2", "(~ (<- p) int:2)"},
        expression_case{"(<-p)", "(<- p)"},
        expression_case{"f(<-p, <-q)", "(call f (<- p) (<- q))"},
        expression_case{"a[<-p]", "(index a (<- p))"},
        expression_case{"[<-p, <-q]", "(array (<- p) (<- q))"},
        expression_case{"P{x: <-p}", "(struct-init P (x (<- p)))"},
        expression_case{"int(<-p)", "(convert int (<- p))"},
        expression_case{"new(int, <-p)", "(new int (<- p))"},
        expression_case{"<-p = <-q", "(= (<- p) (<- q))"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, DereferenceBindsLooserThanPostfix) {
    const std::array cases{
        expression_case{"<-p[0]", "(<- (index p int:0))"},
        expression_case{"<-p.x", "(<- (. p x))"},
        expression_case{"<-f()", "(<- (call f))"},
        expression_case{"(<-p).x", "(. (<- p) x)"},
        expression_case{"(<-p)[0]", "(index (<- p) int:0)"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, BothMeaningsInOneExpression) {
    const std::array cases{
        expression_case{"<-p<-1", "(< (<- p) (- int:1))"},
        // the first arrow follows an operand and is split. what follows the "-" is a new operand
        expression_case{"x<--1", "(< x (- (- int:1)))"},
        expression_case{"x<-<-p", "(< x (- (<- p)))"},
        expression_case{"x<-<-<-pp", "(< x (- (<- (<- pp))))"},
        expression_case{"<-p<-<-q", "(< (<- p) (- (<- q)))"},
        expression_case{"x<-!y", "(< x (- (! y)))"},
        expression_case{"x<-@y", "(< x (- (@ y)))"},
        expression_case{"x<-(1)", "(< x (- int:1))"},
        expression_case{"f(a<-b, <-c)", "(call f (< a (- b)) (<- c))"},
        expression_case{"a[i<-1]", "(index a (< i (- int:1)))"},
        expression_case{"(<-p)<-(<-q)", "(< (<- p) (- (<- q)))"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, NeighbouringOperatorsWinByMaximalMunch) {
    const std::array cases{
        // "<<" is matched before "<-" can be, so this is a shift by a negative amount
        expression_case{"x<<-1", "(<< x (- int:1))"},
        expression_case{"x<=-1", "(<= x (- int:1))"},
        expression_case{"x<<<-p", "(<< x (<- p))"},
    };
    expect_expressions(cases);
}

TEST(DereferenceSplit, AStatementBoundaryEndsTheOperand) {
    EXPECT_EQ(dump_statements("x; <-p = 5;"), "(expr x) (expr (= (<- p) int:5))");
    EXPECT_EQ(dump_statements("{ } <-p = 1;"), "(block) (expr (= (<- p) int:1))");
    EXPECT_EQ(dump_statements("if (x) { } <-p = 1;"), "(if (x (block))) (expr (= (<- p) int:1))");
    // "return" is a keyword, not an operand
    EXPECT_EQ(dump_statements("return <-p;"), "(return (<- p))");
    EXPECT_EQ(dump_statements("return<-p;"), "(return (<- p))");
    EXPECT_EQ(dump_statements("return x<-p;"), "(return (< x (- p)))");
}

TEST(DereferenceSplit, InDeclarationsAndControlFlow) {
    EXPECT_EQ(dump_statements("let v: int = <-p;"), "(let v int (<- p))");
    EXPECT_EQ(dump_statements("let b: bool = x<-1;"), "(let b bool (< x (- int:1)))");
    EXPECT_EQ(dump_statements("let a: [<-p]int;"), "(let a ([] (<- p) int))");
    EXPECT_EQ(dump_statements("let a: [n<-1]int;"), "(let a ([] (< n (- int:1)) int))");
    EXPECT_EQ(dump_statements("for (let i: int = <-p; i<-1; i = <-q) { }"),
              "(for (let i int (<- p)) (< i (- int:1)) (= i (<- q)) (block))");
    EXPECT_EQ(dump_statements("while (<-p<-1) { }"), "(while (< (<- p) (- int:1)) (block))");
    EXPECT_EQ(dump_statements("if (<-p) { } elif (x<-1) { }"),
              "(if ((<- p) (block)) ((< x (- int:1)) (block)))");
    EXPECT_EQ(dump_statements("inv { <-p<-1 };"), "(inv (< (<- p) (- int:1)))");
}

TEST(DereferenceSplit, NothingAfterTheSplitIsAnError) {
    EXPECT_TRUE(parser_rejects("fn main() { x<-; }", ";"));
    EXPECT_TRUE(parser_rejects("fn main() { f(x<-); }", ")", ");"));
    EXPECT_TRUE(parser_rejects("fn main() { <-; }", ";"));
    EXPECT_TRUE(parser_rejects_end("fn main() { x<-"));
    EXPECT_TRUE(parser_rejects_end("fn main() { <-"));
}

TEST(DereferenceSplit, SplitHalvesHaveTheirOwnSpans) {
    constexpr std::string_view source = "fn main() { limit<-12; }";
    const ast_root_ptr root = parse_source(source);
    const expr_stmt &statement = as<expr_stmt>(*function_at(*root, 0).body->statements.at(0));
    const binary_expr &comparison = as<binary_expr>(*statement.expression);
    EXPECT_TRUE(covers(source, comparison.source, "limit<-12"));
    EXPECT_TRUE(covers(source, comparison.left->source, "limit"));
    // the "-" made by the split starts one column into the "<-" token
    EXPECT_TRUE(covers(source, comparison.right->source, "-12"));
    EXPECT_TRUE(covers(source, as<unary_expr>(*comparison.right).right->source, "12"));
}

// ---------------------------------------------------------------------------------------------
// "->" is one POINTER token, which the grammar only allows in types
// ---------------------------------------------------------------------------------------------

TEST(PointerArrow, InTypes) {
    EXPECT_EQ(dump_statements("let p: ->int;"), "(let p (-> int))");
    EXPECT_EQ(dump_statements("let p:->int=@x;"), "(let p (-> int) (@ x))");
    EXPECT_EQ(dump_statements("let pp: ->->int;"), "(let pp (-> (-> int)))");
    EXPECT_EQ(dump_statements("let pp: -> -> int;"), "(let pp (-> (-> int)))");
    EXPECT_EQ(dump_statements("let a: [3]->int;"), "(let a ([] int:3 (-> int)))");
    EXPECT_EQ(dump_statements("let a: ->[3]int;"), "(let a (-> ([] int:3 int)))");
    EXPECT_EQ(dump_statements("let n: ->Node = new(->Node, 1);"),
              "(let n (-> Node) (new (-> Node) int:1))");
    EXPECT_EQ(parse_and_dump("fn f(p:->int):->int{return p;}"),
              "(program (fn f ((p (-> int))) (-> int) (block (return p))))");
}

TEST(PointerArrow, MinusAndGreaterThanNeedNotTouch) {
    const std::array cases{
        expression_case{"a-b", "(- a b)"},
        expression_case{"a -1", "(- a int:1)"},
        expression_case{"a--b", "(- a (- b))"},
        expression_case{"a>-b", "(> a (- b))"},
        expression_case{"a>=-b", "(>= a (- b))"},
        expression_case{"a-b>c", "(> (- a b) c)"},
        expression_case{"a>b-c", "(> a (- b c))"},
        expression_case{"x=-1", "(= x (- int:1))"},
        expression_case{"f(-1)[-2]", "(index (call f (- int:1)) (- int:2))"},
    };
    expect_expressions(cases);
}

TEST(PointerArrow, InAnExpressionIsAnError) {
    // no valid expression has "-" directly before ">", so unlike "<-" there is nothing to split
    const std::array cases{
        rejected_case{"fn main() { a->b; }", "->", "->"},
        rejected_case{"fn main() { a -> b; }", "->", "->"},
        rejected_case{"fn main() { p->x = 1; }", "->", "->"},
        rejected_case{"fn main() { a-->b; }", "->", "->"},
        rejected_case{"fn main() { ->p; }", "->", "->"},
        rejected_case{"fn main() { x = ->p; }", "->", "->"},
        // written apart, the "-" is a subtraction with nothing valid after it
        rejected_case{"fn main() { a - > b; }", ">", ">"},
    };
    expect_rejections(cases);
}

TEST(PointerArrow, SplitInATypeIsAnError) {
    EXPECT_TRUE(parser_rejects("fn main() { let p: - > int; }", "-"));
    EXPECT_TRUE(parser_rejects("fn main() { let p: <-int; }", "<-"));
}

// ---------------------------------------------------------------------------------------------
// operators that share a first character
// ---------------------------------------------------------------------------------------------

TEST(OperatorMunch, Shifts) {
    const std::array cases{
        expression_case{"a<<b", "(<< a b)"},
        expression_case{"a>>b", "(>> a b)"},
        expression_case{"a << b >> c", "(>> (<< a b) c)"},
        expression_case{"a>>b>>c", "(>> (>> a b) c)"},
        expression_case{"1<<2+3", "(<< int:1 (+ int:2 int:3))"},
        expression_case{"a<b<<c", "(< a (<< b c))"},
        expression_case{"a>>b>c", "(> (>> a b) c)"},
        expression_case{"a>>-b", "(>> a (- b))"},
        expression_case{"x&1<<2", "(& x (<< int:1 int:2))"},
    };
    expect_expressions(cases);
}

TEST(OperatorMunch, ShiftsWrittenApartAreTwoComparisons) {
    const std::array cases{
        rejected_case{"fn main() { a > > b; }", ">", "> b"},
        rejected_case{"fn main() { a < < b; }", "<", "< b"},
        rejected_case{"fn main() { a>>>b; }", ">", ">b"},
        rejected_case{"fn main() { a<<<b; }", "<", "<b"},
    };
    expect_rejections(cases);
}

TEST(OperatorMunch, TwoCharacterOperators) {
    const std::array cases{
        expression_case{"a=b", "(= a b)"},
        expression_case{"a==b", "(== a b)"},
        expression_case{"a!=b", "(!= a b)"},
        expression_case{"a=!b", "(= a (! b))"},
        expression_case{"a==!b", "(== a (! b))"},
        expression_case{"a!=!b", "(!= a (! b))"},
        expression_case{"a<=b", "(<= a b)"},
        expression_case{"a>=b", "(>= a b)"},
        expression_case{"a&b", "(& a b)"},
        expression_case{"a&&b", "(&& a b)"},
        expression_case{"a|b", "(| a b)"},
        expression_case{"a||b", "(|| a b)"},
        expression_case{"a=-b", "(= a (- b))"},
        expression_case{"a==-b", "(== a (- b))"},
        expression_case{"a=@b", "(= a (@ b))"},
        expression_case{"!!a", "(! (! a))"},
        expression_case{"--a", "(- (- a))"},
        expression_case{"a~-b", "(~ a (- b))"},
        expression_case{"a*-b", "(* a (- b))"},
        expression_case{R"(a\b/c)", R"((/ (\ a b) c))"},
        expression_case{"a^b|c&d", "(| (^ a b) (& c d))"},
    };
    expect_expressions(cases);
}

TEST(OperatorMunch, OperatorsTheLanguageDoesNotHave) {
    const std::array cases{
        rejected_case{"fn main() { a===b; }", "=", "=b"},
        rejected_case{"fn main() { a!==b; }", "=", "=b"},
        rejected_case{"fn main() { a&&&b; }", "&", "&b"},
        rejected_case{"fn main() { a|||b; }", "|", "|b"},
        rejected_case{"fn main() { a>>=b; }", "=", "=b"},
        rejected_case{"fn main() { a<<=b; }", "=", "=b"},
        rejected_case{"fn main() { a=>b; }", ">", ">b"},
        rejected_case{"fn main() { a=<b; }", "<", "<b"},
        rejected_case{"fn main() { a<>b; }", ">", ">b"},
        rejected_case{"fn main() { i++; }", "+", "+;"},
        rejected_case{"fn main() { ++i; }", "+", "++"},
        rejected_case{"fn main() { x += 1; }", "=", "= 1"},
        rejected_case{"fn main() { x = +1; }", "+", "+1"},
        rejected_case{"fn main() { a ** b; }", "*", "* b"},
        // there is no "//" comment or operator: it is two divisions
        rejected_case{"fn main() { a // b; }", "/", "/ b"},
        rejected_case{"fn main() { x = 1; // note\n}", "/", "// note"},
    };
    expect_rejections(cases);
    // these characters are not in the language at all, so the tokeniser stops first
    EXPECT_TRUE(lexer_rejects("fn main() { a % b; }", "%"));
    EXPECT_TRUE(lexer_rejects("fn main() { a ? b : c; }", "?"));
}

// ---------------------------------------------------------------------------------------------
// "." is member access, or part of a float when a digit is next to it
// ---------------------------------------------------------------------------------------------

TEST(DotsAndNumbers, MemberAccess) {
    const std::array cases{
        expression_case{"a.b", "(. a b)"},
        expression_case{"a.b.c", "(. (. a b) c)"},
        expression_case{"a . b\n.c", "(. (. a b) c)"},
        expression_case{"a.b.c()", "(call (. (. a b) c))"},
        expression_case{"a[0].x", "(. (index a int:0) x)"},
        expression_case{"a.x[0]", "(index (. a x) int:0)"},
        expression_case{"f().x", "(. (call f) x)"},
        expression_case{"this.x.y", "(. (. this x) y)"},
        expression_case{"P{x: 1}.x", "(. (struct-init P (x int:1)) x)"},
        expression_case{"a.x1.y_2", "(. (. a x1) y_2)"},
    };
    expect_expressions(cases);
}

TEST(DotsAndNumbers, FloatForms) {
    const std::array cases{
        expression_case{".5", "float:0.5"},
        expression_case{"5.", "float:5"},
        expression_case{"1.5", "float:1.5"},
        expression_case{"x = .5 + 5.", "(= x (+ float:0.5 float:5))"},
        expression_case{"-.5", "(- float:0.5)"},
        expression_case{"a[.5]", "(index a float:0.5)"},
        expression_case{"f(1.,.5,2.25)", "(call f float:1 float:0.5 float:2.25)"},
        expression_case{"1.-.5", "(- float:1 float:0.5)"},
    };
    expect_expressions(cases);
}

TEST(DotsAndNumbers, MemberAccessOnALiteralNeedsTheDotKeptApart) {
    // the grammar lets any primary expression take a suffix, including a literal
    const std::array cases{
        expression_case{"5 .foo", "(. int:5 foo)"},
        expression_case{"(5).foo", "(. int:5 foo)"},
        expression_case{"1 .method()", "(call (. int:1 method))"},
        expression_case{"1.5 .foo", "(. float:1.5 foo)"},
        expression_case{"(1.5).foo", "(. float:1.5 foo)"},
        expression_case{R"("abc".len)", R"((. str:"abc" len))"},
        expression_case{"'c'.code", "(. char:'c' code)"},
        expression_case{"true.x", "(. bool:true x)"},
        expression_case{"16bff.x", "(. int:255 x)"},
        expression_case{"[1, 2].len", "(. (array int:1 int:2) len)"},
    };
    expect_expressions(cases);
}

TEST(DotsAndNumbers, DotNextToADigitBelongsToTheNumber) {
    const std::array cases{
        // FLOAT "5." then IDENTIFIER "foo": two operands in a row
        rejected_case{"fn main() { 5.foo; }", "foo", "foo"},
        rejected_case{"fn main() { 1.method(); }", "method", "method"},
        rejected_case{"fn main() { 1.f; }", "f", "f;"},
        // IDENTIFIER "a" then FLOAT ".5"
        rejected_case{"fn main() { a.5; }", ".5", ".5"},
        rejected_case{"fn main() { a.b.5; }", ".5", ".5"},
        rejected_case{"fn main() { a[0].5; }", ".5", ".5"},
        rejected_case{"fn main() { f().5; }", ".5", ".5"},
    };
    expect_rejections(cases);
}

TEST(DotsAndNumbers, ASecondDotInANumberIsALexerError) {
    constexpr std::string_view two_dots = "fn main() { 5..foo; }";
    EXPECT_TRUE(lexer_rejects(two_dots, ".", position_of(two_dots, ".foo")));
    constexpr std::string_view version = "fn main() { x = 1.2.3; }";
    EXPECT_TRUE(lexer_rejects(version, ".", position_of(version, ".3")));
    constexpr std::string_view member = "fn main() { 1.5.foo; }";
    EXPECT_TRUE(lexer_rejects(member, ".", position_of(member, ".foo")));
    constexpr std::string_view range = "fn main() { a[0..3]; }";
    EXPECT_TRUE(lexer_rejects(range, ".", position_of(range, ".3")));
}

TEST(DotsAndNumbers, MemberNameMustBeAnIdentifier) {
    EXPECT_TRUE(parser_rejects("fn main() { a.int; }", "int"));
    EXPECT_TRUE(parser_rejects("fn main() { a.main; }", "main", "main;"));
    EXPECT_TRUE(parser_rejects("fn main() { a.(b); }", "(", "(b"));
    EXPECT_TRUE(parser_rejects("fn main() { a.; }", ";"));
    EXPECT_TRUE(parser_rejects("fn main() { a..b; }", ".", ".b"));
}

TEST(DotsAndNumbers, LiteralValuesAreConverted) {
    const std::array cases{
        expression_case{"2b11", "int:3"},
        expression_case{"16bff", "int:255"},
        expression_case{"16bFF", "int:255"},
        expression_case{"16bff+2b1-10", "(- (+ int:255 int:1) int:10)"},
        expression_case{"007", "int:7"},
        expression_case{"2147483647", "int:2147483647"},
        expression_case{"16b7fffffff", "int:2147483647"},
        expression_case{"2b0", "int:0"},
        // without the base prefix these are ordinary decimals
        expression_case{"2 + 16", "(+ int:2 int:16)"},
        expression_case{"20", "int:20"},
        expression_case{"160", "int:160"},
    };
    expect_expressions(cases);
}

TEST(DotsAndNumbers, IntegerTooBigForTheTokeniserToNoticeIsAParserError) {
    // the tokeniser takes any run of digits. the parser finds the value does not fit an int
    constexpr std::string_view decimal = "fn main() {\n    x = 2147483648;\n}\n";
    EXPECT_TRUE(fails_with_exactly(
        decimal, "SYN002: literal '2147483648' is out of range at line 1, column 8"));
    EXPECT_TRUE(fails_with(in_main("x = 16b80000000;"),
                           "SYN002: literal '16b80000000' is out of range", "column 16"));
    EXPECT_TRUE(fails_with(in_main(std::format("x = 2b{};", repeated("1", 32))),
                           "SYN002: literal '2b1111", "column 16"));
    // a literal has no sign, so the most negative int cannot be written directly
    EXPECT_TRUE(fails_with(in_main("x = -2147483648;"),
                           "SYN002: literal '2147483648' is out of range", "column 17"));
}

TEST(DotsAndNumbers, LettersStraightAfterANumberAreLexerErrors) {
    constexpr std::string_view glued = "fn main() { x = 123abc; }";
    EXPECT_TRUE(lexer_rejects(glued, "a", position_of(glued, "abc")));
    constexpr std::string_view exponent = "fn main() { x = 1e5; }";
    EXPECT_TRUE(lexer_rejects(exponent, "e"));
    constexpr std::string_view c_style_hex = "fn main() { y = 0xff; }";
    EXPECT_TRUE(lexer_rejects(c_style_hex, "x", position_of(c_style_hex, "xff")));
    constexpr std::string_view grouped = "fn main() { x = 1_000; }";
    EXPECT_TRUE(lexer_rejects(grouped, "_"));
    constexpr std::string_view suffix = "fn main() { x = 1.5f; }";
    EXPECT_TRUE(lexer_rejects(suffix, "f", position_of(suffix, "f;")));
    constexpr std::string_view keyword = "fn main() { return 1if; }";
    EXPECT_TRUE(lexer_rejects(keyword, "i", position_of(keyword, "if;")));
    constexpr std::string_view bad_base = "fn main() { x = 3b11; }";
    EXPECT_TRUE(lexer_rejects(bad_base, "b"));
    constexpr std::string_view bad_digit = "fn main() { x = 2b12; }";
    EXPECT_TRUE(lexer_rejects(bad_digit, "2", position_of(bad_digit, "2;")));
    constexpr std::string_view no_digits = "fn main() { x = 16b; }";
    EXPECT_TRUE(lexer_rejects(no_digits, ";"));
    constexpr std::string_view upper_prefix = "fn main() { x = 16BFF; }";
    EXPECT_TRUE(lexer_rejects(upper_prefix, "B"));
}

TEST(DotsAndNumbers, SpaceInsideABaseLiteralMakesTwoOperands) {
    EXPECT_TRUE(parser_rejects("fn main() { x = 2 b11; }", "b11"));
    EXPECT_TRUE(parser_rejects("fn main() { x = 16 bff; }", "bff"));
}

// ---------------------------------------------------------------------------------------------
// keywords and identifiers
// ---------------------------------------------------------------------------------------------

TEST(KeywordMunch, IdentifiersThatStartWithOrSitInsideKeywords) {
    constexpr std::array<std::string_view, 48> names{
        "intx",    "int_",   "int8",    "Int",       "INT",    "floats",  "chars",   "booly",
        "bytes",   "iff",    "elifs",   "elsewhere", "whiles", "fortune", "returns", "stepped",
        "rejects", "invert", "updates", "lets",      "imuts",  "states",  "defined", "fns",
        "structs", "impls",  "thiss",   "newer",     "truex",  "falsey",  "mainx",   "_main",
        "i",       "in",     "imp",     "fo",        "retur",  "sta",     "stru",    "tru",
        "fals",    "updat",  "whil",    "defin",     "eli",    "by",      "ne",      "rejec",
    };
    std::string arguments;
    std::string expected = "(call f";
    for (const std::string_view &name : names) {
        arguments += arguments.empty() ? "" : ", ";
        arguments += name;
        expected += ' ';
        expected += name;
    }
    expected += ')';
    EXPECT_EQ(dump_expression(std::format("f({})", arguments)), expected);
}

TEST(KeywordMunch, ReturnGluedToWhatFollows) {
    EXPECT_EQ(dump_statements("returnx;"), "(expr returnx)");
    EXPECT_EQ(dump_statements("return1;"), "(expr return1)");
    EXPECT_EQ(dump_statements("return_;"), "(expr return_)");
    EXPECT_EQ(dump_statements("return x;"), "(return x)");
    EXPECT_EQ(dump_statements("return;"), "(return)");
    // anything that cannot continue an identifier ends the keyword
    EXPECT_EQ(dump_statements("return-1;"), "(return (- int:1))");
    EXPECT_EQ(dump_statements("return!x;"), "(return (! x))");
    EXPECT_EQ(dump_statements("return@x;"), "(return (@ x))");
    EXPECT_EQ(dump_statements("return(x);"), "(return x)");
    EXPECT_EQ(dump_statements("return[1];"), "(return (array int:1))");
    EXPECT_EQ(dump_statements("return.5;"), "(return float:0.5)");
    EXPECT_EQ(dump_statements(R"(return"s";)"), R"((return str:"s"))");
    EXPECT_EQ(dump_statements("return'c';"), "(return char:'c')");
    EXPECT_EQ(dump_statements("return#\n;"), "(return)");
}

TEST(KeywordMunch, StatementKeywordsGluedToIdentifiers) {
    EXPECT_EQ(dump_statements("if(x){}else{}"), "(if (x (block)) (else (block)))");
    EXPECT_EQ(dump_statements("iff(x);"), "(expr (call iff x))");
    EXPECT_TRUE(parser_rejects("fn main() { iff (x) { } }", "{", "{ }"));

    EXPECT_EQ(dump_statements("let x:int=1;"), "(let x int int:1)");
    EXPECT_TRUE(parser_rejects("fn main() { letx: int = 1; }", ":"));

    EXPECT_EQ(dump_statements("imut state x: int = 1;"), "(imut-state x int int:1)");
    EXPECT_TRUE(parser_rejects("fn main() { imutstate x: int = 1; }", "x"));

    EXPECT_EQ(dump_statements("while(x){}"), "(while x (block))");
    EXPECT_EQ(dump_statements("whilex(x);"), "(expr (call whilex x))");

    EXPECT_EQ(dump_statements("step{x;}reject{y;}"),
              "(step (block (expr x)) (reject (block (expr y))))");
    // an identifier followed by "{" starts a struct initialiser, so the error comes after it
    EXPECT_TRUE(parser_rejects_at("fn main() { stepx { } }", "}", {0, 22}));

    // "elsex" is a new statement, not an else branch
    EXPECT_EQ(dump_statements("if (a) { } elsex;"), "(if (a (block))) (expr elsex)");
    EXPECT_EQ(dump_statements("if(a){}elif(b){}else{}"),
              "(if (a (block)) (b (block)) (else (block)))");
    // there is no "else if": else takes a block
    EXPECT_TRUE(parser_rejects("fn main() { if (a) { } else if (b) { } }", "if", "if (b)"));
}

TEST(KeywordMunch, DeclarationKeywordsGluedToIdentifiers) {
    EXPECT_TRUE(parser_rejects("fnmain() { }", "fnmain"));
    EXPECT_TRUE(parser_rejects("definelet x: int;", "definelet"));
    EXPECT_TRUE(parser_rejects("define letx: int;", "letx"));
    EXPECT_TRUE(parser_rejects("updatefn f()() { }", "updatefn"));
    EXPECT_TRUE(parser_rejects("update fnf()() { }", "fnf"));
    EXPECT_TRUE(parser_rejects("structP { };", "structP"));
    EXPECT_TRUE(parser_rejects("inv{x};\ninvx{x};", "invx"));
    EXPECT_EQ(parse_and_dump("define let x:int;struct P{};impl P{};inv{x};"),
              "(program (let x int) (struct P) (impl P) (inv x))");
}

TEST(KeywordMunch, ExpressionKeywordsGluedToIdentifiers) {
    const std::array cases{
        expression_case{"int(x)", "(convert int x)"},
        expression_case{"int (x)", "(convert int x)"},
        expression_case{"intx(x)", "(call intx x)"},
        expression_case{"int_(x)", "(call int_ x)"},
        expression_case{"x = true", "(= x bool:true)"},
        expression_case{"x = truex", "(= x truex)"},
        expression_case{"truefalse", "truefalse"},
        expression_case{"true&&false", "(&& bool:true bool:false)"},
        expression_case{"!true", "(! bool:true)"},
        expression_case{"this.x", "(. this x)"},
        expression_case{"thisx.y", "(. thisx y)"},
        expression_case{"new(int,4)", "(new int int:4)"},
        expression_case{"newx(a, 4)", "(call newx a int:4)"},
    };
    expect_expressions(cases);
    // only the keyword takes a type as its first argument. elsewhere "int" starts a conversion
    EXPECT_TRUE(parser_rejects("fn main() { newx(int, 4); }", ","));
}

TEST(KeywordMunch, MainIsOnlyAllowedAsTheMainFunction) {
    EXPECT_EQ(parse_and_dump("fn main(){}"), "(program (fn main () void (block)))");
    EXPECT_EQ(parse_and_dump("fn mainx(): int { return 0; }"),
              "(program (fn mainx () int (block (return int:0))))");
    EXPECT_EQ(parse_and_dump("fn main_(main2: int): Main { return MAIN; }"),
              "(program (fn main_ ((main2 int)) Main (block (return MAIN))))");
    EXPECT_TRUE(parser_rejects("fn main(): int { return 0; }", ":"));
    EXPECT_TRUE(parser_rejects("fn main(argc: int) { }", "argc"));
    EXPECT_TRUE(parser_rejects("fn f(): int { return main(); }", "main"));
    EXPECT_TRUE(parser_rejects("fn f(): int { let main: int = 1; return 0; }", "main"));
    EXPECT_TRUE(parser_rejects("update fn main()() { }", "main"));
    EXPECT_TRUE(parser_rejects("struct main { };", "main"));
    EXPECT_TRUE(parser_rejects("impl P { fn main() { } };", "main"));
}

TEST(KeywordMunch, NamesTheSpecMentionsButTheGrammarDoesNotReserve) {
    // the grammar has no null literal and no void type, so both are plain identifiers
    EXPECT_EQ(dump_statements("p = null;"), "(expr (= p null))");
    const ast_root_ptr root = parse_source("fn f(): void { }");
    const named_type_syntax &result = as<named_type_syntax>(*function_at(*root, 0).return_type);
    EXPECT_EQ(result.name, "void");
}

// ---------------------------------------------------------------------------------------------
// layout: whitespace and comments never change the tree
// ---------------------------------------------------------------------------------------------

TEST(Layout, EverythingProgramParses) {
    EXPECT_EQ(parse_and_dump(everything_source), everything_dump);
}

TEST(Layout, SmallestProgramCompactAndSpreadOut) {
    constexpr std::string_view expected = "(program (fn main () void (block (let x int int:1))))";
    EXPECT_EQ(parse_and_dump("fn main(){let x:int=1;}"), expected);
    EXPECT_EQ(parse_and_dump("fn main() { let x: int = 1; }\n"), expected);
    EXPECT_EQ(parse_and_dump(
                  "\n\n  fn\n\tmain\n (\n )\n{\n\n\tlet\n\tx\n\t:\n\tint\n\t=\n\t1\n\t;\n}\n\n"),
              expected);
}

TEST(Layout, CompactProgramMatchesTheReadableOne) {
    constexpr std::string_view compact =
        "define let g:[3]->int;struct P{x:int;};impl P{fn get():int{return this.x;}"
        "update fn set(v:int){this.x=v;}};update fn tick(s:P)(n:int){s.x=s.x+n;}inv{g[0]!=g[1]};"
        "fn main(){imut state p:P=P{x:1};for(let i:int=0;i<3;i=i+1){"
        R"(print("a\n",'c',1.5,2b11,16bff,true);})"
        "if(x<-1){tick(p)(1);}elif(!x){return;}else{step{p.set(float(2)~-1);}reject{}}"
        "while(false){let q:->int=new(int,4);@q;<-q;[1,2];}}";
    EXPECT_EQ(parse_and_dump(compact), everything_dump);
}

TEST(Layout, AnySeparatorBetweenTokensGivesTheSameTree) {
    constexpr std::array<std::string_view, 12> separators{
        " ",
        "\n",
        "\t",
        "\r\n",
        "\r",
        "  \t\n\n  ",
        " # note\n",
        "#\n",
        "#\r\n",
        "# comment text is never tokenised: \"open string, 'c, 12abc, $ and fn main() { }\n",
        "\n# one\n# two\n\n",
        " #####\n ",
    };
    for (const std::string_view &separator : separators) {
        SCOPED_TRACE(testing::PrintToString(separator));
        EXPECT_EQ(parse_and_dump(respaced(everything_source, separator)), everything_dump);
    }
}

TEST(Layout, CommentAtTheEndOfTheFileWithoutANewline) {
    EXPECT_EQ(parse_and_dump("fn main() { } # done"), "(program (fn main () void (block)))");
    EXPECT_EQ(parse_and_dump("fn main() { }#"), "(program (fn main () void (block)))");
    EXPECT_EQ(parse_and_dump("define let x: int;\n# the end"), "(program (let x int))");
}

TEST(Layout, CommentedOutCodeIsSkipped) {
    constexpr std::string_view source = R"(# fn old() { syntax error here
fn main() {
    # let x: int = "unterminated;
    let y: int = 2; # }
    # }
	# return 'ab';
}
# fn after(): int { return $; }
)";
    EXPECT_EQ(parse_and_dump(source), "(program (fn main () void (block (let y int int:2))))");
}

TEST(Layout, CommentsDirectlyAgainstTokens) {
    EXPECT_EQ(dump_statements("x#c\n=#c\n1#c\n;#c\n"), "(expr (= x int:1))");
    EXPECT_EQ(dump_statements("let#\nx#\n:#\n->#\nint#\n;"), "(let x (-> int))");
    EXPECT_EQ(dump_statements("x = 1.#\n+ .5#\n;"), "(expr (= x (+ float:1 float:0.5)))");
    EXPECT_EQ(dump_statements("x = 2b11#\n+ 16bff#\n;"), "(expr (= x (+ int:3 int:255)))");
    EXPECT_EQ(dump_statements("f(a,#\n<-p#\n);"), "(expr (call f a (<- p)))");
    // a comment ends an identifier, so these are two names
    EXPECT_TRUE(parser_rejects("fn main() { ret#\nurn; }", "urn"));
}

TEST(Layout, HashInsideAStringOrCharIsNotAComment) {
    EXPECT_EQ(dump_statements("print(\"# not a comment\"); # a comment\n"),
              R"((expr (call print str:"# not a comment")))");
    EXPECT_EQ(dump_statements("x = '#'; # a comment\n"), "(expr (= x char:'#'))");
    EXPECT_EQ(dump_statements("print(\"a\", # \"b\",\n \"c\");"),
              R"((expr (call print str:"a" str:"c")))");
}

// ---------------------------------------------------------------------------------------------
// string and char literals
// ---------------------------------------------------------------------------------------------

TEST(TextLiterals, StringSpanningLines) {
    constexpr std::string_view source = "fn main() {\n"
                                        "    print(\"line one\n"
                                        "line two\n"
                                        "\");\n"
                                        "}\n";
    EXPECT_EQ(
        parse_and_dump(source),
        R"((program (fn main () void (block (expr (call print str:"line one\nline two\n"))))))");
    // a real line break and the \n escape give the same value
    EXPECT_EQ(parse_and_dump(source),
              parse_and_dump(R"(fn main() { print("line one\nline two\n"); })"));
}

TEST(TextLiterals, StringSpanningLinesKeepsIndentationAndBlankLines) {
    EXPECT_EQ(dump_expression("\"a\n\n\t  b\""), R"(str:"a\n\n\t  b")");
    EXPECT_EQ(dump_expression("\"\n\""), R"(str:"\n")");
}

TEST(TextLiterals, StringsHoldingTextThatLooksLikeCode) {
    const std::array cases{
        expression_case{R"("")", R"(str:"")"},
        expression_case{R"("# not a comment")", R"(str:"# not a comment")"},
        expression_case{R"("// nor is this /* or this */")",
                        R"(str:"// nor is this /* or this */")"},
        expression_case{R"("fn main() { return x<-1; }")", R"(str:"fn main() { return x<-1; }")"},
        expression_case{R"("if while let define 2b11 16bff 1.5.5 12abc $")",
                        R"(str:"if while let define 2b11 16bff 1.5.5 12abc $")"},
        expression_case{R"("it's")", R"(str:"it's")"},
        expression_case{R"("say \"hi\"")", R"(str:"say \"hi\"")"},
        expression_case{R"("\"")", R"(str:"\"")"},
        expression_case{R"("ends with a backslash \\")", R"(str:"ends with a backslash \\")"},
        expression_case{R"("\\\"")", R"(str:"\\\"")"},
        expression_case{R"("tab\tcr\rnul\0nl\nquote\'")", R"(str:"tab\tcr\rnul\0nl\nquote'")"},
        expression_case{"\"raw\ttab\"", R"(str:"raw\ttab")"},
        expression_case{R"("}); ] ) ,")", R"(str:"}); ] ) ,")"},
    };
    expect_expressions(cases);
}

TEST(TextLiterals, StringsNextToOtherTokens) {
    EXPECT_EQ(dump_statements(R"(print("a","b",'c',"d");)"),
              R"((expr (call print str:"a" str:"b" char:'c' str:"d")))");
    EXPECT_EQ(dump_statements(R"(let s:[2]char="hi";)"), R"((let s ([] int:2 char) str:"hi"))");
    EXPECT_EQ(dump_statements(R"(x="a"=="b";)"), R"((expr (= x (== str:"a" str:"b"))))");
    // there is no implicit joining of neighbouring strings
    EXPECT_TRUE(parser_rejects(R"(fn main() { print("a" "b"); })", R"("b")"));
    EXPECT_TRUE(parser_rejects(R"(fn main() { print("a""b"); })", R"("b")"));
}

TEST(TextLiterals, CharsThatAreSpecialElsewhere) {
    const std::array cases{
        expression_case{"'#'", "char:'#'"},
        expression_case{"'\"'", "char:'\"'"},
        expression_case{R"('\"')", "char:'\"'"},
        expression_case{R"('\'')", R"(char:'\'')"},
        expression_case{R"('\\')", R"(char:'\\')"},
        expression_case{R"('\n')", R"(char:'\n')"},
        expression_case{R"('\t')", R"(char:'\t')"},
        expression_case{R"('\r')", R"(char:'\r')"},
        expression_case{R"('\0')", R"(char:'\0')"},
        expression_case{"' '", "char:' '"},
        expression_case{"'\t'", R"(char:'\t')"},
        expression_case{"'}'", "char:'}'"},
        expression_case{"';'", "char:';'"},
        expression_case{"'/'", "char:'/'"},
        expression_case{"'a'=='b'", "(== char:'a' char:'b')"},
        expression_case{"f('(',')')", "(call f char:'(' char:')')"},
        expression_case{"[',', ',']", "(array char:',' char:',')"},
    };
    expect_expressions(cases);
}

TEST(TextLiterals, MalformedLiteralsAreLexerErrors) {
    constexpr std::string_view empty_char = "fn main() { x = ''; }";
    EXPECT_TRUE(lexer_rejects(empty_char, "'", position_of(empty_char, "';")));
    constexpr std::string_view long_char = "fn main() { x = 'ab'; }";
    EXPECT_TRUE(lexer_rejects(long_char, "b"));
    constexpr std::string_view bad_char_escape = R"(fn main() { x = '\q'; })";
    EXPECT_TRUE(lexer_rejects(bad_char_escape, "q"));
    constexpr std::string_view bad_string_escape = R"(fn main() { print("a\qb"); })";
    EXPECT_TRUE(lexer_rejects(bad_string_escape, "q"));
    // unlike a string, a char cannot hold a real line break
    constexpr std::string_view broken_char = "fn main() { x = '\n'; }";
    EXPECT_TRUE(lexer_rejects(broken_char, "\\n", position_of(broken_char, "\n")));
    constexpr std::string_view open_char = "fn main() {\n    x = 'a\n}\n";
    EXPECT_TRUE(lexer_rejects(open_char, "\\n", position_of(open_char, "\n}")));
}

// ---------------------------------------------------------------------------------------------
// input shape
// ---------------------------------------------------------------------------------------------

TEST(InputShape, EmptyAndBlankSources) {
    EXPECT_EQ(parse_and_dump(""), "(program)");
    EXPECT_EQ(parse_and_dump("\n"), "(program)");
    EXPECT_EQ(parse_and_dump(" "), "(program)");
    EXPECT_EQ(parse_and_dump("\n\n   \n\t\t\n\r\n"), "(program)");
    EXPECT_EQ(parse_and_dump("# only a comment\n"), "(program)");
    EXPECT_EQ(parse_and_dump("# only a comment"), "(program)");
    EXPECT_EQ(parse_and_dump("#"), "(program)");
    EXPECT_EQ(parse_and_dump("\n  # one\n\n\t# two\n\n"), "(program)");
}

TEST(InputShape, NoLinesAndEmptyLines) {
    // to_lines never makes an empty line, but the tokeniser's input type allows one
    EXPECT_TRUE(parse(tokenise({}))->declarations.empty());
    EXPECT_TRUE(parse(tokenise({{}, {}}))->declarations.empty());
    const std::vector<std::vector<char>> lines{{}, {'i', 'n', 'v', '{'}, {}, {'x', '}', ';'}, {}};
    EXPECT_EQ(dump(*parse(tokenise(lines))), "(program (inv x))");
}

TEST(InputShape, NoTrailingNewline) {
    EXPECT_EQ(parse_and_dump("fn main() { }"), "(program (fn main () void (block)))");
    EXPECT_EQ(parse_and_dump("define let x: int;"), "(program (let x int))");
    EXPECT_EQ(parse_and_dump("fn main() {\n    return;\n}"),
              "(program (fn main () void (block (return))))");
    EXPECT_EQ(parse_and_dump(everything_source.substr(0, everything_source.size() - 1)),
              everything_dump);
}

TEST(InputShape, BlankLinesAroundTheProgram) {
    EXPECT_EQ(parse_and_dump("\n\n\nfn main() { }\n\n\n"), "(program (fn main () void (block)))");
}

// main.cpp reads lines with getline from a text stream. on Windows that already turns "\r\n" into
// '\n'. a '\r' reaches the tokeniser when a file with Windows line endings is read on Linux or
// macOS, and then every line ends "\r\n", which is the shape tested here
TEST(InputShape, WindowsLineEndings) {
    std::string source;
    for (const char ch : everything_source) {
        source += ch == '\n' ? "\r\n" : std::string{ch};
    }
    EXPECT_EQ(parse_and_dump(source), everything_dump);
    EXPECT_EQ(parse_and_dump("fn main() {\r\n    x = 1; # note\r\n}\r\n"),
              "(program (fn main () void (block (expr (= x int:1)))))");
    EXPECT_EQ(parse_and_dump("fn main() { }\r\n# last line, no newline\r"),
              "(program (fn main () void (block)))");
}

TEST(InputShape, WindowsLineEndingsDoNotMoveErrorPositions) {
    constexpr std::string_view unix_source = "fn main() {\n    let x: int = 1\n    x = 2 $ 3;\n}\n";
    constexpr std::string_view windows_source =
        "fn main() {\r\n    let x: int = 1\r\n    x = 2 $ 3;\r\n}\r\n";
    EXPECT_TRUE(lexer_rejects(unix_source, "$", {2, 10}));
    EXPECT_TRUE(lexer_rejects(windows_source, "$", {2, 10}));

    constexpr std::string_view unix_syntax = "fn main() {\n    let x: int = 1\n    x = 2;\n}\n";
    constexpr std::string_view windows_syntax =
        "fn main() {\r\n    let x: int = 1\r\n    x = 2;\r\n}\r\n";
    EXPECT_TRUE(parser_rejects_at(unix_syntax, "x", {2, 4}));
    EXPECT_TRUE(parser_rejects_at(windows_syntax, "x", {2, 4}));
}

TEST(InputShape, WindowsLineEndingInsideAStringStaysInItsValue) {
    // the grammar does not say otherwise: a string holds exactly what is between its quotes
    EXPECT_EQ(dump_expression("\"a\r\nb\""), R"(str:"a\r\nb")");
}

TEST(InputShape, Tabs) {
    EXPECT_EQ(parse_and_dump("fn\tmain()\t{\n\t\tlet\tx:\tint\t=\t1;\n\t}\n"),
              "(program (fn main () void (block (let x int int:1))))");
    // a tab is one column
    constexpr std::string_view source = "fn main() {\n\t\tx\t=\t1\t$;\n}\n";
    EXPECT_TRUE(lexer_rejects(source, "$", {1, 8}));
    constexpr std::string_view syntax = "fn main() {\n\t\tx\t=\t1\t2;\n}\n";
    EXPECT_TRUE(parser_rejects_at(syntax, "2", {1, 8}));
}

TEST(InputShape, BytesOutsideTheLanguageAreOnlyAllowedInTextAndComments) {
    EXPECT_EQ(
        parse_and_dump("fn main() { print(\"caf\xC3\xA9 \xE2\x9C\x93\"); } # na\xC3\xAFve\n"),
        "(program (fn main () void (block (expr (call print str:\"caf\xC3\xA9 \xE2\x9C\x93\")))))");
    EXPECT_TRUE(lexer_rejects("fn main() { caf\xC3\xA9 = 1; }", "\xC3"));
    EXPECT_TRUE(lexer_rejects("fn main() {\f}", "\f"));
    EXPECT_TRUE(lexer_rejects("fn main() {\v}", "\v"));
    // a byte order mark, which some Windows editors put at the start of a file
    EXPECT_TRUE(lexer_rejects("\xEF\xBB\xBF"
                              "fn main() { }",
                              "\xEF", {0, 0}));
}

// ---------------------------------------------------------------------------------------------
// whole programs
// between them these use every rule of the three grammar files at least once
// ---------------------------------------------------------------------------------------------

TEST(Programs, StructWithImplAndUpdateMethods) {
    constexpr std::string_view source =
        R"(# a bank account whose balance can only change inside a step
struct Account {
    id: int;
    balance: float;
    owner: [16]char;
    open: bool;
};

impl Account {
    fn is_open(): bool {
        return this.open;
    }

    fn can_withdraw(amount: float): bool {
        return this.open && amount > 0. && this.balance - amount >= 0.;
    }

    update fn deposit(amount: float) {
        this.balance = this.balance + amount;
    }

    update fn close() {
        this.open = false;
        return;
    }
};

fn make_account(id: int, owner: [16]char): Account {
    return Account{id: id, balance: 0.0, owner: owner, open: true};
}

fn main() {
    state acct: Account = make_account(1, "quinn");
    inv { acct.balance >= 0. };
    step {
        acct.deposit(12.5);
    }
    if (acct.can_withdraw(20.)) {
        step { acct.deposit(-20.); } reject { print("rejected\n"); }
    } else {
        print("insufficient funds: ", acct.balance, '\n');
    }
    step { acct.close(); }
}
)";
    EXPECT_EQ(
        parse_and_dump(source),
        "(program "
        "(struct Account (id int) (balance float) (owner ([] int:16 char)) (open bool)) "
        "(impl Account "
        "(fn is_open () bool (block (return (. this open)))) "
        "(fn can_withdraw ((amount float)) bool (block (return "
        "(&& (&& (. this open) (> amount float:0)) (>= (- (. this balance) amount) float:0))))) "
        "(update-fn deposit () ((amount float)) (block "
        "(expr (= (. this balance) (+ (. this balance) amount))))) "
        "(update-fn close () () (block (expr (= (. this open) bool:false)) (return)))) "
        "(fn make_account ((id int) (owner ([] int:16 char))) Account (block (return "
        "(struct-init Account (id id) (balance float:0) (owner owner) (open bool:true))))) "
        "(fn main () void (block "
        "(state acct Account (call make_account int:1 str:\"quinn\")) "
        "(inv (>= (. acct balance) float:0)) "
        "(step (block (expr (call (. acct deposit) float:12.5)))) "
        "(if ((call (. acct can_withdraw) float:20) (block "
        "(step (block (expr (call (. acct deposit) (- float:20)))) "
        "(reject (block (expr (call print str:\"rejected\\n\"))))))) "
        "(else (block "
        "(expr (call print str:\"insufficient funds: \" (. acct balance) char:'\\n'))))) "
        "(step (block (expr (call (. acct close))))))))");
}

TEST(Programs, StateMachineWithStepsAndInvariants) {
    constexpr std::string_view source = R"(# traffic light controller
define imut RED: int = 0;
define imut GREEN: int = 1;
define imut AMBER: int = 2;
define state light: int = RED;
define state ticks: int = 0;
define imut state limit: int = 16b10;
define let log: [4]int;
define let verbose: bool = false;

inv { light >= RED && light <= AMBER };
inv { ticks <= limit };

fn next(colour: int): int {
    if (colour == RED) {
        return GREEN;
    } elif (colour == GREEN) {
        return AMBER;
    } elif (colour == AMBER) {
        return RED;
    }
    return -1;
}

update fn advance(l: int, t: int)(by: int) {
    l = next(l);
    t = t + by;
}

update fn reset(l: int, t: int)() {
    l = RED;
    t = 0;
}

fn main() {
    let rounds: int;
    scan(@rounds);
    for (let i: int = 0; i < rounds; i = i + 1) {
        step {
            advance(light, ticks)(1);
            log[i \ 4] = light;
        } reject {
            print("invariant broken at ", i, '\n');
            step { reset(light, ticks)(); }
            return;
        }
    }
    while (light != RED) {
        step { advance(light, ticks)(0); }
    }
    {
        inv { ticks > 0 || rounds == 0 };
        print(ticks);
    }
}
)";
    EXPECT_EQ(parse_and_dump(source),
              "(program "
              "(imut RED int int:0) "
              "(imut GREEN int int:1) "
              "(imut AMBER int int:2) "
              "(state light int RED) "
              "(state ticks int int:0) "
              "(imut-state limit int int:16) "
              "(let log ([] int:4 int)) "
              "(let verbose bool bool:false) "
              "(inv (&& (>= light RED) (<= light AMBER))) "
              "(inv (<= ticks limit)) "
              "(fn next ((colour int)) int (block "
              "(if ((== colour RED) (block (return GREEN))) "
              "((== colour GREEN) (block (return AMBER))) "
              "((== colour AMBER) (block (return RED)))) "
              "(return (- int:1)))) "
              "(update-fn advance ((l int) (t int)) ((by int)) (block "
              "(expr (= l (call next l))) (expr (= t (+ t by))))) "
              "(update-fn reset ((l int) (t int)) () (block (expr (= l RED)) (expr (= t int:0)))) "
              "(fn main () void (block "
              "(let rounds int) "
              "(expr (call scan (@ rounds))) "
              "(for (let i int int:0) (< i rounds) (= i (+ i int:1)) (block "
              "(step (block "
              "(expr (update-call advance (light ticks) (int:1))) "
              "(expr (= (index log (\\ i int:4)) light))) "
              "(reject (block "
              "(expr (call print str:\"invariant broken at \" i char:'\\n')) "
              "(step (block (expr (update-call reset (light ticks) ())))) "
              "(return)))))) "
              "(while (!= light RED) (block "
              "(step (block (expr (update-call advance (light ticks) (int:0))))))) "
              "(block (inv (|| (> ticks int:0) (== rounds int:0))) (expr (call print ticks))))))");
}

TEST(Programs, PointersAndArrays) {
    constexpr std::string_view source = R"(struct Node {
    value: int;
    next: ->Node;
};

define let table: [2][3]int = [[1, 2, 3], [4, 5, 6]];
define let heads: [4]->Node;
define let grid: ->[3]int;
define imut empty: [0]int = [];

fn sum(values: ->int, count: int): int {
    let total: int = 0;
    for (let i: int = 0; i < count; i = i + 1) {
        total = total + <-(values + i);
    }
    return total;
}

fn push(head: ->Node, value: int): ->Node {
    let node: ->Node = new(Node, 1);
    <-node = Node{value: value, next: head};
    return node;
}

fn swap(a: ->int, b: ->int): bool {
    imut tmp: int = <-a;
    <-a = <-b;
    <-b = tmp;
    return <-a<-tmp;
}

fn main() {
    let buffer: ->int = new(int, table[1][2] * 2);
    let pp: ->->int = @buffer;
    <-<-pp = 7;
    buffer[1] = <-buffer + 1;
    grid = @table[0];
    print(sum(@table[0][0], 6), (<-grid)[2]);
    let list: ->Node = push(push(heads[0], 1), 2);
    print((<-list).value, (<-(<-list).next).value);
    if (swap(@buffer[0], @buffer[1]) || buffer[0]<-1) {
        print(new(->[2]byte, 3) != heads);
    }
    let bytes: [2]byte = [byte(255), byte('a')];
    let none: Node = Node{};
}
)";
    EXPECT_EQ(parse_and_dump(source),
              "(program "
              "(struct Node (value int) (next (-> Node))) "
              "(let table ([] int:2 ([] int:3 int)) "
              "(array (array int:1 int:2 int:3) (array int:4 int:5 int:6))) "
              "(let heads ([] int:4 (-> Node))) "
              "(let grid (-> ([] int:3 int))) "
              "(imut empty ([] int:0 int) (array)) "
              "(fn sum ((values (-> int)) (count int)) int (block "
              "(let total int int:0) "
              "(for (let i int int:0) (< i count) (= i (+ i int:1)) (block "
              "(expr (= total (+ total (<- (+ values i))))))) "
              "(return total))) "
              "(fn push ((head (-> Node)) (value int)) (-> Node) (block "
              "(let node (-> Node) (new Node int:1)) "
              "(expr (= (<- node) (struct-init Node (value value) (next head)))) "
              "(return node))) "
              "(fn swap ((a (-> int)) (b (-> int))) bool (block "
              "(imut tmp int (<- a)) "
              "(expr (= (<- a) (<- b))) "
              "(expr (= (<- b) tmp)) "
              "(return (< (<- a) (- tmp))))) "
              "(fn main () void (block "
              "(let buffer (-> int) (new int (* (index (index table int:1) int:2) int:2))) "
              "(let pp (-> (-> int)) (@ buffer)) "
              "(expr (= (<- (<- pp)) int:7)) "
              "(expr (= (index buffer int:1) (+ (<- buffer) int:1))) "
              "(expr (= grid (@ (index table int:0)))) "
              "(expr (call print (call sum (@ (index (index table int:0) int:0)) int:6) "
              "(index (<- grid) int:2))) "
              "(let list (-> Node) (call push (call push (index heads int:0) int:1) int:2)) "
              "(expr (call print (. (<- list) value) (. (<- (. (<- list) next)) value))) "
              "(if ((|| (call swap (@ (index buffer int:0)) (@ (index buffer int:1))) "
              "(< (index buffer int:0) (- int:1))) (block "
              "(expr (call print (!= (new (-> ([] int:2 byte)) int:3) heads)))))) "
              "(let bytes ([] int:2 byte) (array (convert byte int:255) (convert byte char:'a'))) "
              "(let none Node (struct-init Node)))))");
}

TEST(Programs, NumericCodeAndPrecedence) {
    constexpr std::string_view source = R"(define imut MASK: int = 16bFF;
define imut FLAGS: byte = byte(2b1010);

fn clamp(x: float, lo: float, hi: float): float {
    if (x < lo) { return lo; } elif (x > hi) { return hi; } else { return x; }
}

fn hash(seed: int, c: char): int {
    return (seed << 5 ^ seed >> 2 | int(c) & MASK) \ 3;
}

fn poly(x: float): float {
    return -x ~ 2 + 3. * x - .5 / 2 ~ -1;
}

fn main() {
    let a: int = 1 + 2 * 3 - 4 / 2;
    let b: int = (1 + 2) * (3 - 4) \ 2;
    let c: float = 2 ~ 3 ~ 2;
    let ok: bool = a < b == b >= a && !(a != b) || a <= 0;
    let even: bool = a & MASK == 0;
    let neg: int = - -a - -1;
    let ch: char = char(int('a') + 1);
    let f: float = float(a) / 2. + clamp(c, 0., 1.);
    let t: bool = bool(a) && true || false;
    a = b = a + 1;
    while (a > 0 && !even) {
        a = a - 1;
    }
    print(hash(a, ch), poly(f));
}
)";
    EXPECT_EQ(parse_and_dump(source),
              "(program "
              "(imut MASK int int:255) "
              "(imut FLAGS byte (convert byte int:10)) "
              "(fn clamp ((x float) (lo float) (hi float)) float (block "
              "(if ((< x lo) (block (return lo))) ((> x hi) (block (return hi))) "
              "(else (block (return x)))))) "
              "(fn hash ((seed int) (c char)) int (block (return "
              "(\\ (| (^ (<< seed int:5) (>> seed int:2)) (& (convert int c) MASK)) int:3)))) "
              "(fn poly ((x float)) float (block (return "
              "(- (+ (- (~ x int:2)) (* float:3 x)) (/ float:0.5 (~ int:2 (- int:1))))))) "
              "(fn main () void (block "
              "(let a int (- (+ int:1 (* int:2 int:3)) (/ int:4 int:2))) "
              "(let b int (\\ (* (+ int:1 int:2) (- int:3 int:4)) int:2)) "
              "(let c float (~ int:2 (~ int:3 int:2))) "
              "(let ok bool (|| (&& (== (< a b) (>= b a)) (! (!= a b))) (<= a int:0))) "
              "(let even bool (== (& a MASK) int:0)) "
              "(let neg int (- (- (- a)) (- int:1))) "
              "(let ch char (convert char (+ (convert int char:'a') int:1))) "
              "(let f float (+ (/ (convert float a) float:2) (call clamp c float:0 float:1))) "
              "(let t bool (|| (&& (convert bool a) bool:true) bool:false)) "
              "(expr (= a (= b (+ a int:1)))) "
              "(while (&& (> a int:0) (! even)) (block (expr (= a (- a int:1))))) "
              "(expr (call print (call hash a ch) (call poly f))))))");
}

// ---------------------------------------------------------------------------------------------
// errors: the tokeniser runs to the end before the parser starts
// ---------------------------------------------------------------------------------------------

TEST(LexerErrors, StrayCharacterInsideValidCode) {
    constexpr std::string_view source = R"(fn main() {
    let x: int = 1;
    let y: int = x $ 2;
}
)";
    EXPECT_TRUE(lexer_rejects(source, "$"));
    // spelt out once, so the position helper is checked against a hand count
    EXPECT_TRUE(
        fails_with_exactly(source, "LEX001: unexpected character '$' at line 2, column 19"));
}

TEST(LexerErrors, WinOverEarlierSyntaxErrors) {
    // line 0 is not valid syntax, but the parser never gets to say so
    constexpr std::string_view source = "fn fn fn\nlet let\n    x = 12abc;\n";
    EXPECT_TRUE(lexer_rejects(source, "a", position_of(source, "abc")));
    EXPECT_TRUE(parser_rejects_at("fn fn fn\nlet let\n    x = 12;\n", "fn", {0, 3}));
}

TEST(LexerErrors, FirstOfSeveralIsReported) {
    constexpr std::string_view source = "fn main() {\n    x = 1 $ 2;\n    y = 3 ? 4;\n}\n";
    EXPECT_TRUE(lexer_rejects(source, "$"));
}

TEST(LexerErrors, PositionAfterTabsCommentsAndAMultiLineString) {
    constexpr std::string_view source = "# header\n"
                                        "fn main() {\n"
                                        "\tprint(\"one\n"
                                        "two\");\t`\n"
                                        "}\n";
    EXPECT_TRUE(lexer_rejects(source, "`", {3, 7}));
}

TEST(LexerErrors, UnterminatedStringRunsToTheEndOfTheInput) {
    constexpr std::string_view source = "fn main() {\n    print(\"abc);\n}\n";
    EXPECT_TRUE(lexer_rejects_end(source, "\"abc"));
    EXPECT_TRUE(fails_with_exactly(source, "LEX002: unexpected end of input at line 2, column 2 "
                                           "(unfinished token starts at line 1, column 10)"));

    constexpr std::string_view no_newline = "fn main() { print(\"abc";
    EXPECT_TRUE(fails_with_exactly(no_newline,
                                   "LEX002: unexpected end of input at line 0, column 22 "
                                   "(unfinished token starts at line 0, column 18)"));
}

TEST(LexerErrors, StringClosedByAnEscapedQuoteIsUnterminated) {
    constexpr std::string_view source = R"(fn main() { print("abc\"); })";
    EXPECT_TRUE(lexer_rejects_end(source, "\"abc"));
}

TEST(LexerErrors, OddNumberOfQuotesSwallowsTheRestOfTheProgram) {
    // the second string opens at the stray quote and never closes
    constexpr std::string_view source = "fn main() {\n    print(\"a\"\");\n    return;\n}\n";
    EXPECT_TRUE(lexer_rejects_end(source, "\");"));
}

TEST(LexerErrors, UnterminatedCharAtTheEndOfTheInput) {
    EXPECT_TRUE(lexer_rejects_end("fn main() { let c: char = 'a", "'a"));
    EXPECT_TRUE(lexer_rejects_end("fn main() { let c: char = '", "'"));
    EXPECT_TRUE(lexer_rejects_end("fn main() { let c: char = '\\", "'"));
}

TEST(LexerErrors, UnfinishedNumberAtTheEndOfTheInput) {
    EXPECT_TRUE(lexer_rejects_end("define let x: int = 2b", "2b"));
    EXPECT_TRUE(lexer_rejects_end("define let x: int = 16b", "16b"));
}

TEST(ParserErrors, MissingSemicolonIsReportedAtTheNextToken) {
    constexpr std::string_view source = R"(fn main() {
    let x: int = 1
    let y: int = 2;
}
)";
    EXPECT_TRUE(parser_rejects_at(source, "let", {2, 4}));
    EXPECT_TRUE(fails_with_exactly(source, "SYN001: unexpected token 'let' at line 2, column 4"));
}

TEST(ParserErrors, PositionOnTheRightLineOfALongerProgram) {
    constexpr std::string_view source = R"(# shapes
struct Point { x: int; y: int; };

fn area(w: int, h: int): int {
    return w * h;
}

fn main() {
    let p: Point = Point{x: 1, y: 2};
    if (p.x > 0) {
        print(area(p.x p.y));
    }
}
)";
    EXPECT_TRUE(parser_rejects(source, "p", "p.y)"));
    EXPECT_TRUE(parser_rejects_at(source, "p", {10, 23}));
}

TEST(ParserErrors, PositionAfterTabsAndComments) {
    constexpr std::string_view source = "fn main() {\n"
                                        "\t# set up\n"
                                        "\t\tx = 1;\t# fine\n"
                                        "\t\ty = ;\t# not fine\n"
                                        "}\n";
    EXPECT_TRUE(parser_rejects_at(source, ";", {3, 6}));
}

TEST(ParserErrors, PositionAfterAMultiLineString) {
    constexpr std::string_view source = "fn main() {\n"
                                        "    print(\"one\n"
                                        "two\" three);\n"
                                        "}\n";
    EXPECT_TRUE(parser_rejects_at(source, "three", {2, 5}));
}

TEST(ParserErrors, OffendingTokenThatSpansLinesIsReportedWhereItStarts) {
    constexpr std::string_view source = "fn main() {\n"
                                        "    x \"one\n"
                                        "two\";\n"
                                        "}\n";
    EXPECT_TRUE(parser_rejects_at(source, "\"one\ntwo\"", {1, 6}));
}

TEST(ParserErrors, FirstOfSeveralIsReported) {
    constexpr std::string_view source = "fn main() {\n    x = ;\n    y = ;\n}\n";
    EXPECT_TRUE(parser_rejects_at(source, ";", {1, 8}));
}

TEST(ParserErrors, StatementsAndStrayTokensAtTheTopLevel) {
    EXPECT_TRUE(parser_rejects("fn main() { }\nlet x: int = 1;\n", "let"));
    EXPECT_TRUE(parser_rejects("fn main() { }\nx = 1;\n", "x"));
    EXPECT_TRUE(parser_rejects("fn main() { }\nprint(1);\n", "print"));
    EXPECT_TRUE(parser_rejects("fn main() { };\n", ";"));
    EXPECT_TRUE(parser_rejects("fn main() { } }\n", "}", "}\n"));
    EXPECT_TRUE(parser_rejects("struct P { x: int; }\nfn main() { }\n", "fn"));
    EXPECT_TRUE(parser_rejects("define let x: int\nfn main() { }\n", "fn"));
    EXPECT_TRUE(parser_rejects("inv { x }\nfn main() { }\n", "fn"));
    EXPECT_TRUE(parser_rejects("\"a program is not a string\"\n", "\"a program is not a string\""));
    EXPECT_TRUE(parser_rejects("42\n", "42"));
}

TEST(ParserErrors, MisplacedKeywords) {
    EXPECT_TRUE(parser_rejects("fn main() {\n    else { }\n}\n", "else"));
    EXPECT_TRUE(parser_rejects("fn main() {\n    if (x) { } else { } elif (y) { }\n}\n", "elif"));
    EXPECT_TRUE(parser_rejects("fn main() {\n    reject { }\n}\n", "reject"));
    EXPECT_TRUE(parser_rejects("fn main() {\n    define let x: int;\n}\n", "define"));
    EXPECT_TRUE(
        parser_rejects("fn main() {\n    fn inner(): int { return 1; }\n}\n", "fn", "fn i"));
    EXPECT_TRUE(parser_rejects("fn main() {\n    for (;;) { }\n}\n", ";"));
    EXPECT_TRUE(parser_rejects("fn main() {\n    x = let;\n}\n", "let"));
}

TEST(ParserErrors, UnclosedBlockAtTheEndOfTheInput) {
    EXPECT_TRUE(parser_rejects_end("fn main() {\n    let x: int = 1;\n"));
    EXPECT_TRUE(parser_rejects_end("fn main() {\n    let x: int = 1;\n# }\n"));
    EXPECT_TRUE(parser_rejects_end("fn main() {\n    if (x) {\n        return;\n}\n"));
    EXPECT_TRUE(parser_rejects_end("fn main() {"));
    EXPECT_TRUE(parser_rejects_end("struct P {\n    x: int;\n"));
    EXPECT_TRUE(parser_rejects_end("impl P {\n    fn get(): int { return 1; }\n"));
}

TEST(ParserErrors, DeclarationCutOffAtTheEndOfTheInput) {
    EXPECT_TRUE(parser_rejects_end("define"));
    EXPECT_TRUE(parser_rejects_end("fn main() { }\ndefine\n"));
    EXPECT_TRUE(parser_rejects_end("fn main() { }\ndefine   # nothing follows\n"));
    EXPECT_TRUE(parser_rejects_end("define let x: int"));
    EXPECT_TRUE(parser_rejects_end("define let x: int ="));
    EXPECT_TRUE(parser_rejects_end("fn"));
    EXPECT_TRUE(parser_rejects_end("fn main("));
    EXPECT_TRUE(parser_rejects_end("fn main()"));
    EXPECT_TRUE(parser_rejects_end("fn f(a: int):"));
    EXPECT_TRUE(parser_rejects_end("update fn f()"));
    EXPECT_TRUE(parser_rejects_end("struct P { x: int; }"));
    EXPECT_TRUE(parser_rejects_end("inv { x }"));
    EXPECT_TRUE(parser_rejects_end("inv {"));
}

TEST(ParserErrors, StatementCutOffAtTheEndOfTheInput) {
    EXPECT_TRUE(parser_rejects_end("fn main() { x"));
    EXPECT_TRUE(parser_rejects_end("fn main() { x ="));
    EXPECT_TRUE(parser_rejects_end("fn main() { x = 1 +"));
    EXPECT_TRUE(parser_rejects_end("fn main() { print(\"a\""));
    EXPECT_TRUE(parser_rejects_end("fn main() { print(\"a\","));
    EXPECT_TRUE(parser_rejects_end("fn main() { return"));
    EXPECT_TRUE(parser_rejects_end("fn main() { if (x"));
    EXPECT_TRUE(parser_rejects_end("fn main() { a[1"));
    EXPECT_TRUE(parser_rejects_end("fn main() { a."));
    EXPECT_TRUE(parser_rejects_end("fn main() { P{x:"));
    EXPECT_TRUE(parser_rejects_end("fn main() { new(int,"));
    EXPECT_TRUE(parser_rejects_end("fn main() { step { } reject"));
}

// ---------------------------------------------------------------------------------------------
// source spans: token positions from the tokeniser become node spans in the parser
// ---------------------------------------------------------------------------------------------

namespace {

constexpr std::string_view spans_source = "# running sum\n"                  // line 0
                                          "define let total: int = 0;\n"     // line 1
                                          "fn bump(by: int): int {\n"        // line 2
                                          "\t# add, then report\n"           // line 3
                                          "\t\treturn total = total + by;\n" // line 4
                                          "}\n"                              // line 5
                                          "fn main() {\n"                    // line 6
                                          "    let n: ->int = @total;\n"     // line 7
                                          "    print(\"first\n"              // line 8
                                          "second\", bump(2));\n"            // line 9
                                          "\tif (n<-1) { return; }  # never\n"
                                          "}\n";

} // namespace

TEST(SourceSpans, GlobalDeclarationRunsFromDefineToTheSemicolon) {
    const ast_root_ptr root = parse_source(spans_source);
    const variable_decl &total = as<variable_decl>(*root->declarations.at(0));
    EXPECT_TRUE(covers(spans_source, total.source, "define let total: int = 0;"));
    EXPECT_TRUE(covers(spans_source, total.name->source, "total"));
    EXPECT_TRUE(covers(spans_source, total.declared_type->source, "int"));
    EXPECT_TRUE(covers(spans_source, total.initializer->source, "0"));
}

TEST(SourceSpans, FunctionSpanningLines) {
    const ast_root_ptr root = parse_source(spans_source);
    const function_decl &bump = function_at(*root, 1);
    EXPECT_EQ(bump.source.start_line, 2);
    EXPECT_EQ(bump.source.start_col, 0);
    EXPECT_EQ(bump.source.end_line, 5);
    EXPECT_EQ(bump.source.end_col, 1);
    EXPECT_TRUE(covers(spans_source, bump.source,
                       "fn bump(by: int): int {\n"
                       "\t# add, then report\n"
                       "\t\treturn total = total + by;\n"
                       "}"));
    EXPECT_TRUE(covers(spans_source, bump.name->source, "bump"));
    EXPECT_TRUE(
        covers(spans_source, bump.ordinary_parameters.at(0).declared_type->source, "int", 1));
    EXPECT_TRUE(covers(spans_source, bump.return_type->source, "int", 2));
    EXPECT_TRUE(covers(spans_source, bump.body->source,
                       "{\n"
                       "\t# add, then report\n"
                       "\t\treturn total = total + by;\n"
                       "}"));
}

TEST(SourceSpans, ExpressionAfterACommentAndTabs) {
    const ast_root_ptr root = parse_source(spans_source);
    const stmt &statement = *function_at(*root, 1).body->statements.at(0);
    EXPECT_TRUE(covers(spans_source, statement.source, "return total = total + by;"));
    const assignment_expr &assignment = as<assignment_expr>(*as<return_stmt>(statement).value);
    EXPECT_EQ(assignment.source.start_line, 4);
    EXPECT_EQ(assignment.source.start_col, 9);
    EXPECT_TRUE(covers(spans_source, assignment.source, "total = total + by"));
    EXPECT_TRUE(covers(spans_source, assignment.target->source, "total", 1));
    const binary_expr &sum = as<binary_expr>(*assignment.value);
    EXPECT_TRUE(covers(spans_source, sum.source, "total + by"));
    EXPECT_TRUE(covers(spans_source, sum.left->source, "total", 2));
    EXPECT_TRUE(covers(spans_source, sum.right->source, "by", 1));
}

TEST(SourceSpans, MainAndItsVoidReturnType) {
    const ast_root_ptr root = parse_source(spans_source);
    const function_decl &main_function = function_at(*root, 2);
    EXPECT_EQ(main_function.source.start_line, 6);
    EXPECT_EQ(main_function.source.end_line, 11);
    EXPECT_EQ(main_function.source.end_col, 1);
    EXPECT_TRUE(covers(spans_source, main_function.name->source, "main"));
    // void is not written, so it takes the span of the signature
    EXPECT_TRUE(covers(spans_source, main_function.return_type->source, "main()"));
}

TEST(SourceSpans, LocalDeclarationStatementTakesInTheSemicolon) {
    const ast_root_ptr root = parse_source(spans_source);
    const decl_stmt &statement = as<decl_stmt>(*function_at(*root, 2).body->statements.at(0));
    EXPECT_TRUE(covers(spans_source, statement.source, "let n: ->int = @total;"));
    const variable_decl &declaration = as<variable_decl>(*statement.declaration);
    EXPECT_TRUE(covers(spans_source, declaration.source, "let n: ->int = @total"));
    EXPECT_TRUE(covers(spans_source, declaration.declared_type->source, "->int"));
    EXPECT_TRUE(covers(spans_source, declaration.initializer->source, "@total"));
}

TEST(SourceSpans, MultiLineStringAndWhatFollowsIt) {
    const ast_root_ptr root = parse_source(spans_source);
    const expr_stmt &statement = as<expr_stmt>(*function_at(*root, 2).body->statements.at(1));
    EXPECT_TRUE(covers(spans_source, statement.source, "print(\"first\nsecond\", bump(2));"));
    const postfix_expr &call = as<postfix_expr>(*statement.expression);
    EXPECT_TRUE(covers(spans_source, call.source, "print(\"first\nsecond\", bump(2))"));

    const arg_list &arguments = std::get<arg_list>(call.arg);
    const source_span &text = arguments.at(0)->source;
    EXPECT_EQ(text.start_line, 8);
    EXPECT_EQ(text.start_col, 10);
    EXPECT_EQ(text.end_line, 9);
    EXPECT_EQ(text.end_col, 7);
    EXPECT_TRUE(covers(spans_source, text, "\"first\nsecond\""));
    // columns restart on the string's last line
    EXPECT_EQ(arguments.at(1)->source.start_line, 9);
    EXPECT_EQ(arguments.at(1)->source.start_col, 9);
    EXPECT_TRUE(covers(spans_source, arguments.at(1)->source, "bump(2)"));
}

TEST(SourceSpans, SplitDereferenceAfterATab) {
    const ast_root_ptr root = parse_source(spans_source);
    const if_stmt &statement = as<if_stmt>(*function_at(*root, 2).body->statements.at(2));
    EXPECT_TRUE(covers(spans_source, statement.source, "if (n<-1) { return; }"));
    const conditional_branch &branch = statement.branches.at(0);
    EXPECT_TRUE(covers(spans_source, branch.condition->source, "n<-1"));
    EXPECT_TRUE(covers(spans_source, as<binary_expr>(*branch.condition).right->source, "-1"));
    EXPECT_TRUE(covers(spans_source, branch.body->source, "{ return; }"));
    EXPECT_TRUE(covers(spans_source, branch.body->statements.at(0)->source, "return;"));
}

TEST(SourceSpans, WindowsLineEndingsGiveTheSameSpans) {
    std::string windows_source;
    for (const char ch : spans_source) {
        windows_source += ch == '\n' ? "\r\n" : std::string{ch};
    }
    const ast_root_ptr unix_root = parse_source(spans_source);
    const ast_root_ptr windows_root = parse_source(windows_source);
    // only nodes with no line break inside them: a '\r' in a multi-line token is part of it
    const source_span &unix_span = function_at(*unix_root, 1).body->statements.at(0)->source;
    const source_span &windows_span = function_at(*windows_root, 1).body->statements.at(0)->source;
    EXPECT_EQ(unix_span.start_line, windows_span.start_line);
    EXPECT_EQ(unix_span.start_col, windows_span.start_col);
    EXPECT_EQ(unix_span.end_line, windows_span.end_line);
    EXPECT_EQ(unix_span.end_col, windows_span.end_col);
}

// ---------------------------------------------------------------------------------------------
// robustness
// ---------------------------------------------------------------------------------------------

namespace {

constexpr size_t nesting_depth = 200;

} // namespace

TEST(Robustness, DeeplyNestedParentheses) {
    const std::string expression =
        repeated("(", nesting_depth) + "x + 1" + repeated(")", nesting_depth);
    EXPECT_EQ(dump_expression(expression), "(+ x int:1)");
}

TEST(Robustness, DeeplyNestedBlocks) {
    const std::string source =
        "fn main() " + repeated("{ ", nesting_depth) + "x;" + repeated(" }", nesting_depth);
    const std::string expected = "(program (fn main () void " + repeated("(block ", nesting_depth) +
                                 "(expr x)" + repeated(")", nesting_depth) + "))";
    EXPECT_EQ(parse_and_dump(source), expected);
}

TEST(Robustness, DeeplyNestedControlFlow) {
    const std::string source = "fn main() { " +
                               repeated("if (a) { while (b) { ", nesting_depth / 2) + "x;" +
                               repeated(" } }", nesting_depth / 2) + " }";
    const std::string expected = "(program (fn main () void (block " +
                                 repeated("(if (a (block (while b (block ", nesting_depth / 2) +
                                 "(expr x)" + repeated(")))))", nesting_depth / 2) + ")))";
    EXPECT_EQ(parse_and_dump(source), expected);
}

TEST(Robustness, DeeplyNestedArraysCallsAndIndexes) {
    EXPECT_EQ(dump_expression(repeated("[", nesting_depth) + "1" + repeated("]", nesting_depth)),
              repeated("(array ", nesting_depth) + "int:1" + repeated(")", nesting_depth));
    EXPECT_EQ(dump_expression(repeated("f(", nesting_depth) + "x" + repeated(")", nesting_depth)),
              repeated("(call f ", nesting_depth) + "x" + repeated(")", nesting_depth));
    EXPECT_EQ(dump_expression(repeated("a[", nesting_depth) + "0" + repeated("]", nesting_depth)),
              repeated("(index a ", nesting_depth) + "int:0" + repeated(")", nesting_depth));
}

TEST(Robustness, LongPrefixOperatorChains) {
    EXPECT_EQ(dump_expression(repeated("-", nesting_depth) + "x"),
              repeated("(- ", nesting_depth) + "x" + repeated(")", nesting_depth));
    EXPECT_EQ(dump_expression(repeated("!", nesting_depth) + "x"),
              repeated("(! ", nesting_depth) + "x" + repeated(")", nesting_depth));
    // every arrow here is a dereference: none follows an operand
    EXPECT_EQ(dump_expression(repeated("<-", nesting_depth) + "p"),
              repeated("(<- ", nesting_depth) + "p" + repeated(")", nesting_depth));
    EXPECT_EQ(dump_statements("let p: " + repeated("->", nesting_depth) + "int;"),
              "(let p " + repeated("(-> ", nesting_depth) + "int" + repeated(")", nesting_depth) +
                  ")");
}

TEST(Robustness, LongOperatorChains) {
    constexpr size_t terms = 1000;
    EXPECT_EQ(dump_expression("x" + repeated(" + 1", terms)),
              repeated("(+ ", terms) + "x" + repeated(" int:1)", terms));
    // right associative, so the tree leans the other way
    EXPECT_EQ(dump_expression(repeated("x = ", terms) + "1"),
              repeated("(= x ", terms) + "int:1" + repeated(")", terms));
    EXPECT_EQ(dump_expression("x" + repeated("<-1", terms)),
              repeated("(< ", terms) + "x" + repeated(" (- int:1))", terms));
    EXPECT_EQ(dump_expression("a" + repeated(".b", terms)),
              repeated("(. ", terms) + "a" + repeated(" b)", terms));
}

TEST(Robustness, LongProgram) {
    constexpr size_t count = 5000;
    std::string source;
    for (size_t i = 0; i < count; i++) {
        source += std::format("define let v{}: int = {}; # declaration {}\n", i, i, i);
        source += std::format("fn f{}(a: int): int {{ return a + v{}; }}\n", i, i);
    }
    source += "fn main() { }\n";

    const ast_root_ptr root = parse_source(source);
    ASSERT_EQ(root->declarations.size(), 2 * count + 1);
    EXPECT_EQ(dump(*root->declarations.front()), "(let v0 int int:0)");
    EXPECT_EQ(dump(*root->declarations.at(2 * count - 2)), "(let v4999 int int:4999)");
    EXPECT_EQ(dump(*root->declarations.at(2 * count - 1)),
              "(fn f4999 ((a int)) int (block (return (+ a v4999))))");
    EXPECT_EQ(dump(*root->declarations.back()), "(fn main () void (block))");
    // one declaration pair per line, so the last function sits on the second last line
    EXPECT_EQ(root->declarations.at(2 * count - 1)->source.start_line,
              static_cast<int>(2 * count - 1));
    EXPECT_EQ(root->declarations.back()->source.start_line, static_cast<int>(2 * count));
}

TEST(Robustness, LongFunctionBody) {
    constexpr size_t count = 5000;
    const ast_root_ptr root = parse_source(in_main(repeated("x = x + 1; ", count)));
    EXPECT_EQ(function_at(*root, 0).body->statements.size(), count);
}

TEST(Robustness, LongProgramOnOneLine) {
    constexpr size_t count = 2000;
    const ast_root_ptr root = parse_source(repeated("inv{x<-1};", count));
    ASSERT_EQ(root->declarations.size(), count);
    EXPECT_EQ(dump(*root->declarations.back()), "(inv (< x (- int:1)))");
    EXPECT_EQ(root->declarations.back()->source.start_line, 0);
    EXPECT_EQ(root->declarations.back()->source.start_col, static_cast<int>(10 * (count - 1)));
}

TEST(Robustness, LongTokens) {
    constexpr size_t length = 100000;
    const std::string name = repeated("n", length);
    EXPECT_EQ(dump_expression(name), name);
    const std::string text = repeated("ab\n", length);
    const std::string dumped = dump_expression('"' + text + '"');
    EXPECT_EQ(dumped, "str:\"" + repeated("ab\\n", length) + '"');
    EXPECT_EQ(parse_and_dump("#" + repeated(" fn", length) + "\nfn main() { }"),
              "(program (fn main () void (block)))");
    EXPECT_EQ(parse_and_dump(repeated(" \t\n", length) + "fn main() { }"),
              "(program (fn main () void (block)))");
}

TEST(Robustness, ErrorAtTheEndOfALongProgram) {
    constexpr size_t count = 2000;
    const std::string valid = repeated("define let x: int;\n", count);
    EXPECT_TRUE(
        lexer_rejects(valid + "define let y: int = $;\n", "$", {static_cast<int>(count), 20}));
    EXPECT_TRUE(
        parser_rejects_at(valid + "define let y: int = ;\n", ";", {static_cast<int>(count), 20}));
    EXPECT_TRUE(parser_rejects_end(valid + "define let y: int ="));
}
