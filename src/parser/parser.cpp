#include "parser.h"
#include "../ast/ast_builder.h"
#include "../ast/declaration.h"
#include "../ast/expression.h"
#include "../ast/expression_base.h"
#include "../ast/source.h"
#include "../ast/statement.h"
#include "../ast/statement_base.h"
#include "../ast/type.h"

#include "../tokens/token.h"
#include <cassert>
#include <charconv>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

class Parser {
  public:
    explicit Parser(const std::vector<token> &tokens) : tokens(tokens), cur_index(0) {
        skip_trivia(); // the first token could be whitespace or a comment
    }

    // builds the AST of the whole program. throws std::runtime_error on a syntax error
    program_ptr parse() {
        return parse_program();
    }

  private:
    // a point in the source text, 0-indexed like token line and column
    struct source_position {
        int line;
        int col;
    };

    const std::vector<token> &tokens;
    size_t cur_index;

    // when the parser splits one token into two (see split_dereference), this holds the second
    // half. while set, it is the current token, and consuming it does not move cur_index
    std::optional<token> split_remainder;

    // just past the last consumed token, so a rule's span can end there
    source_position prev_end{0, 0};

    // ---------------------------------------------------------------------------------------
    // error reporting
    // ---------------------------------------------------------------------------------------

    [[noreturn]] static void report_unexpected_token(const token &errored_token) {
        throw std::runtime_error(
            std::format("SYN001: unexpected token '{}' of type {} at line {}, column {}",
                        errored_token.lexeme, static_cast<int>(errored_token.type),
                        errored_token.line, errored_token.column));
    }

    [[noreturn]] void report_unexpected_end() const {
        throw std::runtime_error(std::format("SYN001: unexpected end of input at line {}, column {}",
                                             prev_end.line, prev_end.col));
    }

    // errors on whatever is at the current position (a token, or the end of input)
    [[noreturn]] void report_error_at_current() const {
        if (at_end())
            report_unexpected_end();
        report_unexpected_token(current());
    }

    // a literal whose value does not fit its type, e.g. an int literal above 2147483647
    [[noreturn]] static void report_literal_out_of_range(const token &literal_token) {
        throw std::runtime_error(
            std::format("SYN002: literal '{}' is out of range at line {}, column {}",
                        literal_token.lexeme, literal_token.line, literal_token.column));
    }

    // ---------------------------------------------------------------------------------------
    // token helpers
    // ---------------------------------------------------------------------------------------

    void skip_trivia() {
        while (cur_index < tokens.size() && (tokens[cur_index].type == token_type::WHITESPACE ||
                                             tokens[cur_index].type == token_type::COMMENT))
            cur_index++;
    }

    void advance() {
        assert(!at_end());
        prev_end = token_end(current());
        if (split_remainder) {
            split_remainder.reset();
            return;
        }
        cur_index++;
        skip_trivia();
    }

    [[nodiscard]] bool at_end() const {
        return !split_remainder && cur_index >= tokens.size();
    }

    // the current token. only valid when !at_end()
    [[nodiscard]] const token &current() const {
        return split_remainder ? *split_remainder : tokens[cur_index];
    }

    // the current token, errors at the end of input. for choosing a branch with a switch
    [[nodiscard]] const token &peek() const {
        if (at_end())
            report_unexpected_end();
        return current();
    }

    // ---------------------------------------------------------------------------------------
    // source spans
    // a node's span runs from its first token to just past its last token
    // ---------------------------------------------------------------------------------------

    // just past the last char of a token. a lexeme can span lines (e.g. a string)
    static source_position token_end(const token &tok) {
        source_position end{tok.line, tok.column};
        for (const char ch : tok.lexeme) {
            if (ch == '\n') {
                end.line++;
                end.col = 0;
            } else {
                end.col++;
            }
        }
        return end;
    }

    // where the current token starts. errors at the end of input
    [[nodiscard]] source_position start_position() const {
        const token &tok = peek();
        return {tok.line, tok.column};
    }

    // where an already built node starts
    static source_position start_of(const source_span &span) {
        return {span.start_line, span.start_col};
    }

    // from start to the end of the last consumed token.
    // call it after the rule's last token is consumed, and never in the same argument list as
    // a parse_* call or a std::move of the node it reads from, as argument order is unspecified
    [[nodiscard]] source_span span_from(const source_position &start) const {
        return {start.line, start.col, prev_end.line, prev_end.col};
    }

    // true if the current token is of the given type (never consumes)
    [[nodiscard]] bool check(const token_type &type) const {
        return !at_end() && current().type == type;
    }

    // true if the current token is any of the given types (never consumes)
    [[nodiscard]] bool check_any(const std::initializer_list<token_type> &types) const {
        for (const token_type &type : types) {
            if (check(type))
                return true;
        }
        return false;
    }

    // consumes the current token if it is of the given type, for optional parts
    bool match(const token_type &type) {
        if (!check(type))
            return false;
        advance();
        return true;
    }

    // consumes the current token, errors if it is not of the given type
    void expect(const token_type &expected_type) {
        if (!check(expected_type))
            report_error_at_current();
        advance();
    }

    // true if the current token is the identifier "main" (never consumes)
    [[nodiscard]] bool check_main() const {
        return check(token_type::IDENTIFIER) && current().lexeme == "main";
    }

    // IDENT: consumes an identifier, errors if it is not one or is the reserved name "main"
    identifier_ptr expect_identifier() {
        if (!check(token_type::IDENTIFIER) || check_main())
            report_error_at_current();
        const source_position start = start_position();
        std::string lexeme = current().lexeme;
        advance();
        return make_identifier(span_from(start), std::move(lexeme));
    }

    // consumes the current token, errors if it is not a binary operator. returns the operator
    binary_op expect_binary_operator() {
        binary_op op{};
        switch (peek().type) {
        case token_type::LOGICAL_OR:
            op = binary_op::LOGICAL_OR;
            break;
        case token_type::LOGICAL_AND:
            op = binary_op::LOGICAL_AND;
            break;
        case token_type::EQUAL:
            op = binary_op::EQUALS;
            break;
        case token_type::NOT_EQUAL:
            op = binary_op::NOT_EQUALS;
            break;
        case token_type::LESSER:
            op = binary_op::LT;
            break;
        case token_type::GREATER:
            op = binary_op::GT;
            break;
        case token_type::LESS_OR_EQUAL:
            op = binary_op::LTE;
            break;
        case token_type::GREATER_OR_EQUAL:
            op = binary_op::GTE;
            break;
        case token_type::BIT_OR:
            op = binary_op::BITWISE_OR;
            break;
        case token_type::XOR:
            op = binary_op::BITWISE_XOR;
            break;
        case token_type::BIT_AND:
            op = binary_op::BITWISE_AND;
            break;
        case token_type::LEFT_SHIFT:
            op = binary_op::LEFT_SHIFT;
            break;
        case token_type::RIGHT_SHIFT:
            op = binary_op::RIGHT_SHIFT;
            break;
        case token_type::PLUS:
            op = binary_op::ADD;
            break;
        case token_type::MINUS:
            op = binary_op::SUBTRACT;
            break;
        case token_type::MULTIPLY:
            op = binary_op::MULTIPLY;
            break;
        case token_type::DIVIDE:
            op = binary_op::DIVIDE;
            break;
        case token_type::DIVIDE_FLOOR:
            op = binary_op::FLOOR_DIVIDE;
            break;
        case token_type::EXPONENT:
            op = binary_op::EXPONENT;
            break;
        default:
            report_error_at_current();
        }
        advance();
        return op;
    }

    // x<-1 tokenises as IDENTIFIER DEREFERENCE INT_LITERAL, but means x < -1.
    // called when the current token is <- directly after an operand, where it must mean < then -
    void split_dereference() {
        const token &dereference = current();
        token minus{token_type::MINUS, "-", dereference.line, dereference.column + 1};
        advance();
        split_remainder = std::move(minus);
    }

    // ---------------------------------------------------------------------------------------
    // literal values
    // ---------------------------------------------------------------------------------------

    // the value of an integer literal, written in the given base after its prefix: 16bff = 255.
    // errors if it does not fit in an int
    static std::int32_t integer_value(const token &literal_token, const std::string_view &prefix,
                                      int base) {
        const std::string &lexeme = literal_token.lexeme;
        assert(lexeme.starts_with(prefix));
        std::int32_t value = 0;
        const std::from_chars_result result = std::from_chars(
            lexeme.data() + prefix.size(), lexeme.data() + lexeme.size(), value, base);
        if (result.ec != std::errc())
            report_literal_out_of_range(literal_token);
        return value;
    }

    // the value of a float literal: 1.5, .5 or 1.
    static double float_value(const token &literal_token) {
        double value = 0;

        try {
            value = std::stod(literal_token.lexeme);
        } catch (const std::out_of_range &) {
            report_literal_out_of_range(literal_token);
        }

        return value;
    }

    // the char an escape stands for, given the char after the backslash: n = newline
    static char escaped_char(char escape) {
        switch (escape) {
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case '0':
            return '\0';
        default: // \\ \' \" stand for the char itself
            return escape;
        }
    }

    // the text between the quotes of a char or string literal, with escapes replaced
    static std::string unquote(const std::string &lexeme) {
        std::string text;
        for (size_t i = 1; i + 1 < lexeme.size(); i++) {
            char ch = lexeme[i];
            if (ch == '\\')
                ch = escaped_char(lexeme[++i]); // the tokeniser ensures an escape char follows
            text += ch;
        }
        return text;
    }

    // ---------------------------------------------------------------------------------------
    // grammar rules (program.txt)
    // ---------------------------------------------------------------------------------------

    // program = { topLevelDeclaration } EOF ;
    program_ptr parse_program() {
        program_ptr root = make_program();
        while (!at_end()) {
            root->declarations.push_back(parse_top_level_declaration());
        }
        return root;
    }

    // topLevelDeclaration
    //     = globalVariableDeclaration
    //     | functionDeclaration
    //     | updateFunctionDeclaration
    //     | structDeclaration
    //     | implDeclaration
    //     | invariantDeclaration ;
    decl_ptr parse_top_level_declaration() {

        switch (peek().type) {
        case token_type::DEFINE:
            return parse_global_variable_declaration();
        case token_type::FN:
            return parse_function_declaration();
        case token_type::UPDATE:
            return parse_update_function_declaration();
        case token_type::STRUCT:
            return parse_struct_declaration();
        case token_type::IMPL:
            return parse_impl_declaration();
        case token_type::INV:
            return parse_invariant_declaration();
        default:
            report_error_at_current();
        }
    }

    // globalVariableDeclaration = "define" variableDeclaration ";" ;
    // the span covers "define" through ";"
    variable_decl_ptr parse_global_variable_declaration() {
        const source_position start = start_position();
        expect(token_type::DEFINE);
        variable_decl_ptr var_declaration = parse_variable_declaration(start);
        expect(token_type::SEMI_COLON);
        var_declaration->source = span_from(start); // widen to take in the ";"
        return var_declaration;
    }

    // variableDeclaration
    //     = "let" binding [ "=" expression ]
    //     | "state" binding "=" expression
    //     | "imut" [ "state" ] binding "=" expression ;
    // start is where the declaration's span begins, which is before "define" for a global
    variable_decl_ptr parse_variable_declaration(const source_position &start) {
        identifier_ptr identifier;
        type_ptr type;
        expr_ptr expression;

        switch (peek().type) {
        case token_type::LET:
            advance();
            std::tie(identifier, type) = parse_binding();
            if (match(token_type::ASSIGN))
                expression = parse_expression();
            return make_variable_decl(span_from(start), std::move(identifier), std::move(type),
                                      std::move(expression), variable_storage::ORDINARY, false);
        case token_type::STATE:
            advance();
            std::tie(identifier, type) = parse_binding();
            expect(token_type::ASSIGN);
            expression = parse_expression();
            return make_variable_decl(span_from(start), std::move(identifier), std::move(type),
                                      std::move(expression), variable_storage::STATE, false);
        case token_type::IMUT: {
            advance();
            variable_storage storage =
                match(token_type::STATE) ? variable_storage::STATE : variable_storage::ORDINARY;
            std::tie(identifier, type) = parse_binding();
            expect(token_type::ASSIGN);
            expression = parse_expression();

            return make_variable_decl(span_from(start), std::move(identifier), std::move(type),
                                      std::move(expression), storage, true);
        }
        default:
            report_error_at_current();
        }
    }
    //  binding = IDENT ":" type ;
    [[nodiscard]] std::pair<identifier_ptr, type_ptr> parse_binding() {
        identifier_ptr identifier = expect_identifier();
        expect(token_type::COLON);
        type_ptr identifier_type_ptr = parse_type();
        return {std::move(identifier), std::move(identifier_type_ptr)};
    }

    // type
    //     = "->" type
    //     | arrayDimension type
    //     | typeName ;
    // types read left to right: [3]->int = array of 3 pointers to int
    type_ptr parse_type() {
        const source_position start = start_position();
        switch (peek().type) {
        case token_type::POINTER: {
            advance();
            type_ptr pointee = parse_type();
            return make_pointer_type(span_from(start), std::move(pointee));
        }
        case token_type::SQUARE_OPEN: {
            expr_ptr size = parse_array_dimension();
            type_ptr element = parse_type();
            return make_array_type(span_from(start), std::move(element), std::move(size));
        }
        default:
            return parse_type_name();
        }
    }

    // typeName = primitiveType | IDENT ;
    type_ptr parse_type_name() {
        switch (peek().type) {
        case token_type::INT:
        case token_type::FLOAT:
        case token_type::CHAR:
        case token_type::BOOL:
        case token_type::BYTE:
            return parse_primitive_type();
        case token_type::IDENTIFIER: {
            identifier_ptr name = expect_identifier();
            return make_named_type(name->source, std::move(name->lexeme));
        }
        default:
            report_error_at_current();
        }
    }

    // primitiveType = "int" | "float" | "char" | "bool" | "byte" ;
    type_ptr parse_primitive_type() {
        primitive_type type{};
        switch (peek().type) {
        case token_type::INT:
            type = primitive_type::INT;
            break;
        case token_type::FLOAT:
            type = primitive_type::FLOAT;
            break;
        case token_type::CHAR:
            type = primitive_type::CHAR;
            break;
        case token_type::BOOL:
            type = primitive_type::BOOL;
            break;
        case token_type::BYTE:
            type = primitive_type::BYTE;
            break;
        default:
            report_error_at_current();
        }
        const source_position start = start_position();
        advance();
        return make_primitive_type(span_from(start), type);
    }

    // arrayDimension = "[" expression "]" ;
    expr_ptr parse_array_dimension() {
        expect(token_type::SQUARE_OPEN);
        expr_ptr size = parse_expression();
        expect(token_type::SQUARE_CLOSE);
        return size;
    }

    // a function declaration without its body
    struct function_signature {
        identifier_ptr name;
        std::vector<parameter> parameters;
        type_ptr return_type;
    };

    // functionDeclaration = "fn" ( mainSignature | functionSignature ) block ;
    // "main" is an IDENTIFIER token, so the branch is picked by its lexeme
    function_decl_ptr parse_function_declaration() {
        const source_position start = start_position();
        expect(token_type::FN);
        function_signature signature =
            check_main() ? parse_main_signature() : parse_function_signature();
        block_ptr body = parse_block();
        return make_function_decl(span_from(start), std::move(signature.name),
                                  std::move(signature.parameters), std::move(signature.return_type),
                                  std::move(body));
    }

    // mainSignature = "main" "(" ")" ;
    // main has no parameters and returns void.
    // void is not written in the source, so its span is the whole "main()" signature
    function_signature parse_main_signature() {
        assert(check_main());
        const source_position start = start_position();
        std::string lexeme = current().lexeme;
        advance(); // already checked to be "main"
        identifier_ptr name = make_identifier(span_from(start), std::move(lexeme));
        expect(token_type::PARENTHESES_OPEN);
        expect(token_type::PARENTHESES_CLOSE);
        return {std::move(name), {}, make_primitive_type(span_from(start), primitive_type::VOID)};
    }

    // functionSignature = IDENT parameterClause ":" type ;
    function_signature parse_function_signature() {
        identifier_ptr name = expect_identifier();
        std::vector<parameter> parameters = parse_parameter_clause();
        expect(token_type::COLON);
        type_ptr return_type = parse_type();
        return {std::move(name), std::move(parameters), std::move(return_type)};
    }

    // updateFunctionDeclaration = "update" "fn" IDENT parameterClause parameterClause block ;
    // first clause is the state parameters, second is the non-state parameters
    function_decl_ptr parse_update_function_declaration() {
        const source_position start = start_position();
        expect(token_type::UPDATE);
        expect(token_type::FN);
        identifier_ptr name = expect_identifier();
        std::vector<parameter> state_parameters = parse_parameter_clause();
        std::vector<parameter> parameters = parse_parameter_clause();
        block_ptr body = parse_block();
        return make_update_function_decl(span_from(start), std::move(name), std::move(parameters),
                                         std::move(state_parameters), std::move(body));
    }

    // parameterClause = "(" [ parameterList ] ")" ;
    std::vector<parameter> parse_parameter_clause() {
        expect(token_type::PARENTHESES_OPEN);
        std::vector<parameter> parameters;
        if (!check(token_type::PARENTHESES_CLOSE))
            parameters = parse_parameter_list();
        expect(token_type::PARENTHESES_CLOSE);
        return parameters;
    }

    // parameterList = binding { "," binding } ;
    std::vector<parameter> parse_parameter_list() {
        std::vector<parameter> parameters;
        do {
            auto [name, type] = parse_binding();
            parameters.emplace_back(std::move(name->lexeme), std::move(type));
        } while (match(token_type::COMMA));
        return parameters;
    }

    // structDeclaration = "struct" IDENT "{" { fieldDeclaration } "}" ";" ;
    decl_ptr parse_struct_declaration() {
        const source_position start = start_position();
        expect(token_type::STRUCT);
        identifier_ptr name = expect_identifier();
        expect(token_type::CURLY_OPEN);
        std::vector<field_decl> fields;
        while (!check(token_type::CURLY_CLOSE))
            fields.push_back(parse_field_declaration());
        expect(token_type::CURLY_CLOSE);
        expect(token_type::SEMI_COLON);
        return make_struct_decl(span_from(start), std::move(name), std::move(fields));
    }

    // fieldDeclaration = binding ";" ;
    field_decl parse_field_declaration() {
        auto [name, type] = parse_binding();
        expect(token_type::SEMI_COLON);
        return {std::move(name->lexeme), std::move(type)};
    }

    // implDeclaration = "impl" IDENT "{" { methodDeclaration } "}" ";" ;
    decl_ptr parse_impl_declaration() {
        const source_position start = start_position();
        expect(token_type::IMPL);
        identifier_ptr type_name = expect_identifier();
        expect(token_type::CURLY_OPEN);
        std::vector<function_decl_ptr> methods;
        while (!check(token_type::CURLY_CLOSE))
            methods.push_back(parse_method_declaration());
        expect(token_type::CURLY_CLOSE);
        expect(token_type::SEMI_COLON);
        return make_impl_decl(span_from(start), std::move(type_name), std::move(methods));
    }

    // methodDeclaration = methodFunctionDeclaration | updateMethodDeclaration ;
    function_decl_ptr parse_method_declaration() {
        switch (peek().type) {
        case token_type::FN:
            return parse_method_function_declaration();
        case token_type::UPDATE:
            return parse_update_method_declaration();
        default:
            report_error_at_current();
        }
    }

    // methodFunctionDeclaration = "fn" functionSignature block ;
    // same as functionDeclaration, but main is not allowed
    function_decl_ptr parse_method_function_declaration() {
        const source_position start = start_position();
        expect(token_type::FN);
        function_signature signature = parse_function_signature();
        block_ptr body = parse_block();
        return make_function_decl(span_from(start), std::move(signature.name),
                                  std::move(signature.parameters), std::move(signature.return_type),
                                  std::move(body));
    }

    // updateMethodDeclaration = "update" "fn" IDENT parameterClause block ;
    // the clause is the non-state parameters. the enclosing impl supplies the state
    function_decl_ptr parse_update_method_declaration() {
        const source_position start = start_position();
        expect(token_type::UPDATE);
        expect(token_type::FN);
        identifier_ptr name = expect_identifier();
        std::vector<parameter> parameters = parse_parameter_clause();
        block_ptr body = parse_block();
        return make_update_method_decl(span_from(start), std::move(name), std::move(parameters),
                                       std::move(body));
    }

    // ---------------------------------------------------------------------------------------
    // grammar rules (statement.txt)
    // ---------------------------------------------------------------------------------------

    // block = "{" { statement } "}" ;
    block_ptr parse_block() {
        const source_position start = start_position();
        expect(token_type::CURLY_OPEN);
        std::vector<stmt_ptr> statements;
        while (!check(token_type::CURLY_CLOSE))
            statements.push_back(parse_statement());
        expect(token_type::CURLY_CLOSE);
        return make_block_stmt(span_from(start), std::move(statements));
    }

    // statement
    //     = variableDeclaration ";"
    //     | expressionStatement
    //     | returnStatement
    //     | ifStatement
    //     | whileStatement
    //     | forStatement
    //     | stepStatement
    //     | invariantDeclaration
    //     | block ;
    // declarations are wrapped in a decl_stmt
    stmt_ptr parse_statement() {
        const source_position start = start_position();
        switch (peek().type) {
        case token_type::LET:
        case token_type::STATE:
        case token_type::IMUT: {
            variable_decl_ptr declaration = parse_variable_declaration(start);
            expect(token_type::SEMI_COLON); // in the statement's span, not the declaration's
            return make_decl_stmt(span_from(start), std::move(declaration));
        }
        case token_type::RETURN:
            return parse_return_statement();
        case token_type::IF:
            return parse_if_statement();
        case token_type::WHILE:
            return parse_while_statement();
        case token_type::FOR:
            return parse_for_statement();
        case token_type::STEP:
            return parse_step_statement();
        case token_type::INV: {
            decl_ptr invariant = parse_invariant_declaration();
            return make_decl_stmt(span_from(start), std::move(invariant));
        }
        case token_type::CURLY_OPEN:
            return parse_block();
        default:
            return parse_expression_statement();
        }
    }

    // expressionStatement = expression ";" ;
    stmt_ptr parse_expression_statement() {
        const source_position start = start_position();
        expr_ptr expression = parse_expression();
        expect(token_type::SEMI_COLON);
        return make_expr_stmt(span_from(start), std::move(expression));
    }

    // returnStatement = "return" [ expression ] ";" ;
    stmt_ptr parse_return_statement() {
        const source_position start = start_position();
        expect(token_type::RETURN);
        expr_ptr value;
        if (!check(token_type::SEMI_COLON))
            value = parse_expression();
        expect(token_type::SEMI_COLON);
        return make_return_stmt(span_from(start), std::move(value));
    }

    // ifStatement
    //     = "if" "(" expression ")" block
    //       { "elif" "(" expression ")" block }
    //       [ "else" block ] ;
    stmt_ptr parse_if_statement() {
        const source_position start = start_position();
        std::vector<conditional_branch> branches;

        // the if branch, then one branch per elif
        expect(token_type::IF);
        do {
            expect(token_type::PARENTHESES_OPEN);
            expr_ptr condition = parse_expression();
            expect(token_type::PARENTHESES_CLOSE);
            branches.emplace_back(std::move(condition), parse_block());
        } while (match(token_type::ELIF));

        block_ptr else_body;
        if (match(token_type::ELSE))
            else_body = parse_block();

        return make_if_stmt(span_from(start), std::move(branches), std::move(else_body));
    }

    // whileStatement = "while" "(" expression ")" block ;
    stmt_ptr parse_while_statement() {
        const source_position start = start_position();
        expect(token_type::WHILE);
        expect(token_type::PARENTHESES_OPEN);
        expr_ptr condition = parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        block_ptr body = parse_block();
        return make_while_stmt(span_from(start), std::move(condition), std::move(body));
    }

    // forStatement = "for" "(" forInitializer ";" expression ";" expression ")" block ;
    stmt_ptr parse_for_statement() {
        const source_position start = start_position();
        expect(token_type::FOR);
        expect(token_type::PARENTHESES_OPEN);
        variable_decl_ptr initializer = parse_for_initializer();
        expect(token_type::SEMI_COLON);
        expr_ptr condition = parse_expression();
        expect(token_type::SEMI_COLON);
        expr_ptr increment = parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        block_ptr body = parse_block();
        return make_for_stmt(span_from(start), std::move(initializer), std::move(condition),
                             std::move(increment), std::move(body));
    }

    // forInitializer = "let" binding "=" expression ;
    variable_decl_ptr parse_for_initializer() {
        const source_position start = start_position();
        expect(token_type::LET);
        auto [name, type] = parse_binding();
        expect(token_type::ASSIGN);
        expr_ptr value = parse_expression();
        return make_variable_decl(span_from(start), std::move(name), std::move(type),
                                  std::move(value), variable_storage::ORDINARY, false);
    }

    // stepStatement = "step" block [ "reject" block ] ;
    stmt_ptr parse_step_statement() {
        const source_position start = start_position();
        expect(token_type::STEP);
        block_ptr body = parse_block();
        block_ptr reject_body;
        if (match(token_type::REJECT))
            reject_body = parse_block();
        return make_step_stmt(span_from(start), std::move(body), std::move(reject_body));
    }

    // invariantDeclaration = "inv" "{" expression "}" ";" ;
    decl_ptr parse_invariant_declaration() {
        const source_position start = start_position();
        expect(token_type::INV);
        expect(token_type::CURLY_OPEN);
        expr_ptr condition = parse_expression();
        expect(token_type::CURLY_CLOSE);
        expect(token_type::SEMI_COLON);
        return make_invariant_decl(span_from(start), std::move(condition));
    }

    // ---------------------------------------------------------------------------------------
    // grammar rules (expression.txt)
    // ---------------------------------------------------------------------------------------

    // expression = assignmentExpression ;

    // assignmentExpression = logicalOrExpression [ "=" assignmentExpression ] ;
    expr_ptr parse_expression() {
        expr_ptr target = parse_logical_or_expression();
        if (match(token_type::ASSIGN)) {
            expr_ptr value = parse_expression(); // recursion makes it right associative
            const source_span span = span_from(start_of(target->source));
            return make_assignment_expr(span, std::move(target), std::move(value));
        }
        return target;
    }

    // the binary operator loops below build left associative trees: a - b - c = (a - b) - c

    // logicalOrExpression = logicalAndExpression { "||" logicalAndExpression } ;
    expr_ptr parse_logical_or_expression() {
        expr_ptr left = parse_logical_and_expression();
        while (check(token_type::LOGICAL_OR)) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_logical_and_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // logicalAndExpression = equalityExpression { "&&" equalityExpression } ;
    expr_ptr parse_logical_and_expression() {
        expr_ptr left = parse_equality_expression();
        while (check(token_type::LOGICAL_AND)) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_equality_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // equalityExpression = relationalExpression { ( "==" | "!=" ) relationalExpression } ;
    expr_ptr parse_equality_expression() {
        expr_ptr left = parse_relational_expression();
        while (check_any({token_type::EQUAL, token_type::NOT_EQUAL})) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_relational_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // relationalExpression
    //     = bitwiseOrExpression { ( "<" | ">" | "<=" | ">=" ) bitwiseOrExpression } ;
    // "<-" directly after an operand is split into "<" "-", so x<-1 = x < -1
    expr_ptr parse_relational_expression() {
        expr_ptr left = parse_bitwise_or_expression();
        while (!at_end()) {
            binary_op op{};
            switch (current().type) {
            case token_type::DEREFERENCE:
                split_dereference();
                op = binary_op::LT;
                break;
            case token_type::LESSER:
            case token_type::GREATER:
            case token_type::LESS_OR_EQUAL:
            case token_type::GREATER_OR_EQUAL:
                op = expect_binary_operator();
                break;
            default:
                return left;
            }
            expr_ptr right = parse_bitwise_or_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // bitwiseOrExpression = bitwiseXorExpression { "|" bitwiseXorExpression } ;
    // bitwise operators bind tighter than comparisons, so x & 1 == 0 = (x & 1) == 0
    expr_ptr parse_bitwise_or_expression() {
        expr_ptr left = parse_bitwise_xor_expression();
        while (check(token_type::BIT_OR)) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_bitwise_xor_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // bitwiseXorExpression = bitwiseAndExpression { "^" bitwiseAndExpression } ;
    expr_ptr parse_bitwise_xor_expression() {
        expr_ptr left = parse_bitwise_and_expression();
        while (check(token_type::XOR)) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_bitwise_and_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // bitwiseAndExpression = shiftExpression { "&" shiftExpression } ;
    expr_ptr parse_bitwise_and_expression() {
        expr_ptr left = parse_shift_expression();
        while (check(token_type::BIT_AND)) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_shift_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // shiftExpression = additiveExpression { ( "<<" | ">>" ) additiveExpression } ;
    expr_ptr parse_shift_expression() {
        expr_ptr left = parse_additive_expression();
        while (check_any({token_type::LEFT_SHIFT, token_type::RIGHT_SHIFT})) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_additive_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // additiveExpression = multiplicativeExpression { ( "+" | "-" ) multiplicativeExpression } ;
    expr_ptr parse_additive_expression() {
        expr_ptr left = parse_multiplicative_expression();
        while (check_any({token_type::PLUS, token_type::MINUS})) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_multiplicative_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // multiplicativeExpression = unaryExpression { ( "*" | "/" | "\" ) unaryExpression } ;
    expr_ptr parse_multiplicative_expression() {
        expr_ptr left = parse_unary_expression();
        while (check_any({token_type::MULTIPLY, token_type::DIVIDE, token_type::DIVIDE_FLOOR})) {
            const binary_op op = expect_binary_operator();
            expr_ptr right = parse_unary_expression();
            const source_span span = span_from(start_of(left->source));
            left = make_binary_expr(span, op, std::move(left), std::move(right));
        }
        return left;
    }

    // unaryExpression = ( "-" | "!" ) unaryExpression | powerExpression ;
    expr_ptr parse_unary_expression() {
        unary_op op{};
        switch (peek().type) {
        case token_type::MINUS:
            op = unary_op::NEGATE;
            break;
        case token_type::NOT:
            op = unary_op::LOGICAL_NOT;
            break;
        default:
            return parse_power_expression();
        }
        const source_position start = start_position();
        advance();
        expr_ptr operand = parse_unary_expression();
        return make_unary_expr(span_from(start), op, std::move(operand));
    }

    // powerExpression = prefixExpression [ "~" unaryExpression ] ;
    expr_ptr parse_power_expression() {
        expr_ptr base = parse_prefix_expression();
        if (check(token_type::EXPONENT)) {
            const binary_op op = expect_binary_operator();
            expr_ptr exponent = parse_unary_expression(); // recursion makes it right associative
            const source_span span = span_from(start_of(base->source));
            return make_binary_expr(span, op, std::move(base), std::move(exponent));
        }
        return base;
    }

    // prefixExpression = ( "@" | "<-" ) prefixExpression | postfixExpression ;
    expr_ptr parse_prefix_expression() {
        unary_op op{};
        switch (peek().type) {
        case token_type::ADDRESS:
            op = unary_op::ADDRESS;
            break;
        case token_type::DEREFERENCE:
            op = unary_op::DEREFERENCE;
            break;
        default:
            return parse_postfix_expression();
        }
        const source_position start = start_position();
        advance();
        expr_ptr operand = parse_prefix_expression();
        return make_unary_expr(span_from(start), op, std::move(operand));
    }

    // postfixExpression = primaryExpression { postfixSuffix } ;
    expr_ptr parse_postfix_expression() {
        expr_ptr expression = parse_primary_expression();
        while (
            check_any({token_type::PARENTHESES_OPEN, token_type::SQUARE_OPEN, token_type::ACCESS}))
            expression = parse_postfix_suffix(std::move(expression));
        return expression;
    }

    // postfixSuffix = argumentClause | "[" expression "]" | "." IDENT ;
    // applies the suffix to object, the expression before it.
    // two argument clauses in a row are one update call: f(state arguments)(arguments)
    // the span runs from the start of object to the end of the suffix
    expr_ptr parse_postfix_suffix(expr_ptr object) {
        const source_position start = start_of(object->source);
        switch (peek().type) {
        case token_type::PARENTHESES_OPEN: {
            arg_list arguments = parse_argument_clause();
            if (!check(token_type::PARENTHESES_OPEN))
                return make_call_expr(span_from(start), std::move(object), std::move(arguments));

            update_arguments update_call_arguments;
            update_call_arguments.state = std::move(arguments);
            update_call_arguments.regular = parse_argument_clause();
            return make_update_call_expr(span_from(start), std::move(object),
                                         std::move(update_call_arguments));
        }
        case token_type::SQUARE_OPEN: {
            advance();
            expr_ptr index = parse_expression();
            expect(token_type::SQUARE_CLOSE);
            return make_subscript_expr(span_from(start), std::move(object), std::move(index));
        }
        case token_type::ACCESS: {
            advance();
            identifier_ptr member = expect_identifier();
            return make_member_access_expr(span_from(start), std::move(object),
                                           std::move(member->lexeme));
        }
        default:
            report_error_at_current();
        }
    }

    //  argumentClause = "(" [ argumentList ] ")" ;
    arg_list parse_argument_clause() {
        expect(token_type::PARENTHESES_OPEN);
        arg_list arguments;
        if (!check(token_type::PARENTHESES_CLOSE))
            arguments = parse_argument_list();
        expect(token_type::PARENTHESES_CLOSE);
        return arguments;
    }

    // argumentList = expression { "," expression } ;
    arg_list parse_argument_list() {
        arg_list arguments;
        arguments.push_back(parse_expression());
        while (match(token_type::COMMA))
            arguments.push_back(parse_expression());
        return arguments;
    }

    // primaryExpression
    //     = literal
    //     | IDENT [ structInitializer ]
    //     | "this"
    //     | "(" expression ")"
    //     | arrayLiteral
    //     | conversionExpression
    //     | allocationExpression ;
    // a parenthesised expression keeps the span of what is inside the parentheses
    expr_ptr parse_primary_expression() {
        const source_position start = start_position();
        switch (peek().type) {
        case token_type::INT_LITERAL:
        case token_type::BASE_2_LITERAL:
        case token_type::BASE_16_LITERAL:
        case token_type::FLOAT_LITERAL:
        case token_type::CHAR_LITERAL:
        case token_type::STRING_LITERAL:
        case token_type::BOOL_LITERAL:
            return parse_literal();
        case token_type::IDENTIFIER: {
            identifier_ptr name = expect_identifier();
            if (!check(token_type::CURLY_OPEN))
                return name;
            std::vector<field_init> fields = parse_struct_initializer();
            return make_struct_literal(span_from(start), std::move(name->lexeme),
                                       std::move(fields));
        }
        case token_type::THIS:
            advance();
            return make_this_expr(span_from(start));
        case token_type::PARENTHESES_OPEN: {
            advance();
            expr_ptr expression = parse_expression();
            expect(token_type::PARENTHESES_CLOSE);
            return expression;
        }
        case token_type::SQUARE_OPEN:
            return parse_array_literal();
        case token_type::INT:
        case token_type::FLOAT:
        case token_type::CHAR:
        case token_type::BOOL:
        case token_type::BYTE:
            return parse_conversion_expression();
        case token_type::NEW:
            return parse_allocation_expression();
        default:
            report_error_at_current();
        }
    }

    // literal
    //     = INT_LITERAL | BASE_2_LITERAL | BASE_16_LITERAL | FLOAT_LITERAL
    //     | CHAR_LITERAL | STRING_LITERAL | BOOL_LITERAL ;
    // the lexeme is turned into the value it stands for: 16bff = 255, '\n' = newline
    expr_ptr parse_literal() {
        const source_position start = start_position();
        const token &literal_token = peek();
        literal_val value;
        switch (literal_token.type) {
        case token_type::INT_LITERAL:
            value = integer_value(literal_token, "", 10);
            break;
        case token_type::BASE_2_LITERAL:
            value = integer_value(literal_token, "2b", 2);
            break;
        case token_type::BASE_16_LITERAL:
            value = integer_value(literal_token, "16b", 16);
            break;
        case token_type::FLOAT_LITERAL:
            value = float_value(literal_token);
            break;
        case token_type::CHAR_LITERAL:
            value = unquote(literal_token.lexeme).front();
            break;
        case token_type::STRING_LITERAL:
            value = unquote(literal_token.lexeme);
            break;
        case token_type::BOOL_LITERAL:
            value = literal_token.lexeme == "true";
            break;
        default:
            report_error_at_current();
        }
        advance();
        return make_literal(span_from(start), std::move(value));
    }

    // arrayLiteral = "[" [ argumentList ] "]" ;
    expr_ptr parse_array_literal() {
        const source_position start = start_position();
        expect(token_type::SQUARE_OPEN);
        arg_list elements;
        if (!check(token_type::SQUARE_CLOSE))
            elements = parse_argument_list();
        expect(token_type::SQUARE_CLOSE);
        return make_array_literal(span_from(start), std::move(elements));
    }

    // structInitializer = "{" [ fieldInitializerList ] "}" ;
    std::vector<field_init> parse_struct_initializer() {
        expect(token_type::CURLY_OPEN);
        std::vector<field_init> fields;
        if (!check(token_type::CURLY_CLOSE))
            fields = parse_field_initializer_list();
        expect(token_type::CURLY_CLOSE);
        return fields;
    }

    // fieldInitializerList = fieldInitializer { "," fieldInitializer } ;
    std::vector<field_init> parse_field_initializer_list() {
        std::vector<field_init> fields;
        fields.push_back(parse_field_initializer());
        while (match(token_type::COMMA))
            fields.push_back(parse_field_initializer());
        return fields;
    }

    // fieldInitializer = IDENT ":" expression ;
    field_init parse_field_initializer() {
        identifier_ptr name = expect_identifier();
        expect(token_type::COLON);
        expr_ptr value = parse_expression();
        return {std::move(name->lexeme), std::move(value)};
    }

    // conversionExpression = primitiveType "(" expression ")" ;
    expr_ptr parse_conversion_expression() {
        const source_position start = start_position();
        type_ptr target_type = parse_primitive_type();
        expect(token_type::PARENTHESES_OPEN);
        expr_ptr value = parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        return make_conversion_expr(span_from(start), std::move(target_type), std::move(value));
    }

    // allocationExpression = "new" "(" type "," expression ")" ;
    expr_ptr parse_allocation_expression() {
        const source_position start = start_position();
        expect(token_type::NEW);
        expect(token_type::PARENTHESES_OPEN);
        type_ptr element_type = parse_type();
        expect(token_type::COMMA);
        expr_ptr count = parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        return make_dynamic_alloc(span_from(start), std::move(element_type), std::move(count));
    }
};

program_ptr parse(const std::vector<token> &tokens) {
    Parser p(tokens);
    return p.parse();
}
