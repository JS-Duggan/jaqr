#include "parser.h"
#include "../tokens/token.h"
#include <cassert>
#include <format>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <vector>

class Parser {
  public:
    explicit Parser(const std::vector<token> &tokens) : tokens(tokens), cur_index(0) {
        skip_trivia(); // the first token could be whitespace or a comment
    }
    // TODO: this will return a constructed AST once the AST is done. for now it just checks if
    // correct
    void parse() {
        parse_program();
    }

  private:
    const std::vector<token> &tokens;
    size_t cur_index;

    // when the parser splits one token into two (see split_dereference), this holds the second
    // half. while set, it is the current token, and consuming it does not move cur_index
    std::optional<token> split_remainder;

    // ---------------------------------------------------------------------------------------
    // error reporting
    // ---------------------------------------------------------------------------------------

    [[noreturn]] static void report_unexpected_token(const token &errored_token) {
        throw std::runtime_error(
            std::format("Parser error: unexpected token '{}' of type {} at line {}, column {}",
                        errored_token.lexeme, static_cast<int>(errored_token.type),
                        errored_token.line, errored_token.column));
    }

    [[noreturn]] static void report_unexpected_end() {
        throw std::runtime_error("Parser error: unexpected end of input");
    }

    // errors on whatever is at the current position (a token, or the end of input)
    [[noreturn]] void report_error_at_current() const {
        if (at_end())
            report_unexpected_end();
        report_unexpected_token(current());
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

    // true if the current token is of the given type (never consumes)
    [[nodiscard]] bool check(const token_type &type) const {
        return !at_end() && current().type == type;
    }

    //  true if the current token is any of the given types (never consumes)
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

    // consumes the current token if it is any of the given types
    bool match_any(const std::initializer_list<token_type> &types) {
        if (!check_any(types))
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
    void expect_identifier() {
        if (!check(token_type::IDENTIFIER) || check_main())
            report_error_at_current();
        advance();
    }

    // x<-1 tokenises as IDENTIFIER DEREFERENCE INT, instead of x < -1
    // called when the current token is <- directly after a operand. Meaning its supposed to be <
    // then -
    void split_dereference() {
        const token &dereference = current();
        token minus{token_type::MINUS, "-", dereference.line, dereference.column + 1};
        advance();
        split_remainder = minus;
    }

    // ---------------------------------------------------------------------------------------
    // grammar rules (program.txt)
    // ---------------------------------------------------------------------------------------

    // program = { topLevelDeclaration } EOF ;
    void parse_program() {
        while (!at_end())
            parse_top_level_declaration();
    }

    // topLevelDeclaration
    //     = globalVariableDeclaration
    //     | functionDeclaration
    //     | updateFunctionDeclaration
    //     | structDeclaration
    //     | implDeclaration
    //     | invariantDeclaration ;
    void parse_top_level_declaration() {
        switch (peek().type) {
        case token_type::DEFINE:
            parse_global_variable_declaration();
            break;
        case token_type::FN:
            parse_function_declaration();
            break;
        case token_type::UPDATE:
            parse_update_function_declaration();
            break;
        case token_type::STRUCT:
            parse_struct_declaration();
            break;
        case token_type::IMPL:
            parse_impl_declaration();
            break;
        case token_type::INV:
            parse_invariant_declaration();
            break;
        default:
            report_error_at_current();
        }
    }

    // globalVariableDeclaration = "define" variableDeclaration ";" ;
    void parse_global_variable_declaration() {
        expect(token_type::DEFINE);
        parse_variable_declaration();
        expect(token_type::SEMI_COLON);
    }

    // variableDeclaration
    //     = "let" binding [ "=" expression ]
    //     | "state" binding "=" expression
    //     | "imut" [ "state" ] binding "=" expression ;
    void parse_variable_declaration() {
        switch (peek().type) {
        case token_type::LET:
            advance();
            parse_binding();
            if (match(token_type::ASSIGN))
                parse_expression();
            break;
        case token_type::STATE:
            advance();
            parse_binding();
            expect(token_type::ASSIGN);
            parse_expression();
            break;
        case token_type::IMUT:
            advance();
            match(token_type::STATE);
            parse_binding();
            expect(token_type::ASSIGN);
            parse_expression();
            break;
        default:
            report_error_at_current();
        }
    }

    // binding = IDENT ":" type ;
    void parse_binding() {
        expect_identifier();
        expect(token_type::COLON);
        parse_type();
    }

    // type
    //     = "->" type
    //     | arrayDimension type
    //     | typeName ;
    // types read left to right: [3]->int = array of 3 pointers to int
    void parse_type() {
        switch (peek().type) {
        case token_type::POINTER:
            advance();
            parse_type();
            break;
        case token_type::SQUARE_OPEN:
            parse_array_dimension();
            parse_type();
            break;
        default:
            parse_type_name();
        }
    }

    // typeName = primitiveType | IDENT ;
    void parse_type_name() {
        switch (peek().type) {
        case token_type::INT:
        case token_type::FLOAT:
        case token_type::CHAR:
        case token_type::BOOL:
        case token_type::BYTE:
            parse_primitive_type();
            break;
        case token_type::IDENTIFIER:
            expect_identifier();
            break;
        default:
            report_error_at_current();
        }
    }

    // primitiveType = "int" | "float" | "char" | "bool" | "byte" ;
    void parse_primitive_type() {
        switch (peek().type) {
        case token_type::INT:
        case token_type::FLOAT:
        case token_type::CHAR:
        case token_type::BOOL:
        case token_type::BYTE:
            advance();
            break;
        default:
            report_error_at_current();
        }
    }

    // arrayDimension = "[" expression "]" ;
    void parse_array_dimension() {
        expect(token_type::SQUARE_OPEN);
        parse_expression();
        expect(token_type::SQUARE_CLOSE);
    }

    // functionDeclaration = "fn" ( mainSignature | functionSignature ) block ;
    // "main" is an IDENTIFIER token, so the branch is picked by its lexeme
    void parse_function_declaration() {
        expect(token_type::FN);
        if (check_main())
            parse_main_signature();
        else
            parse_function_signature();
        parse_block();
    }

    // mainSignature = "main" "(" ")" ;
    void parse_main_signature() {
        assert(check_main());
        advance(); // already checked to be "main"
        expect(token_type::PARENTHESES_OPEN);
        expect(token_type::PARENTHESES_CLOSE);
    }

    // functionSignature = IDENT parameterClause ":" type ;
    void parse_function_signature() {
        expect_identifier();
        parse_parameter_clause();
        expect(token_type::COLON);
        parse_type();
    }

    // updateFunctionDeclaration = "update" "fn" IDENT parameterClause parameterClause block ;
    void parse_update_function_declaration() {
        expect(token_type::UPDATE);
        expect(token_type::FN);
        expect_identifier();
        parse_parameter_clause();
        parse_parameter_clause();
        parse_block();
    }

    // parameterClause = "(" [ parameterList ] ")" ;
    void parse_parameter_clause() {
        expect(token_type::PARENTHESES_OPEN);
        if (!check(token_type::PARENTHESES_CLOSE))
            parse_parameter_list();
        expect(token_type::PARENTHESES_CLOSE);
    }

    // parameterList = binding { "," binding } ;
    void parse_parameter_list() {
        parse_binding();
        while (match(token_type::COMMA))
            parse_binding();
    }

    // structDeclaration = "struct" IDENT "{" { fieldDeclaration } "}" ";" ;
    void parse_struct_declaration() {
        expect(token_type::STRUCT);
        expect_identifier();
        expect(token_type::CURLY_OPEN);
        while (!check(token_type::CURLY_CLOSE))
            parse_field_declaration();
        expect(token_type::CURLY_CLOSE);
        expect(token_type::SEMI_COLON);
    }

    // fieldDeclaration = binding ";" ;
    void parse_field_declaration() {
        parse_binding();
        expect(token_type::SEMI_COLON);
    }

    // implDeclaration = "impl" IDENT "{" { methodDeclaration } "}" ";" ;
    void parse_impl_declaration() {
        expect(token_type::IMPL);
        expect_identifier();
        expect(token_type::CURLY_OPEN);
        while (!check(token_type::CURLY_CLOSE))
            parse_method_declaration();
        expect(token_type::CURLY_CLOSE);
        expect(token_type::SEMI_COLON);
    }

    // methodDeclaration = methodFunctionDeclaration | updateMethodDeclaration ;
    void parse_method_declaration() {
        switch (peek().type) {
        case token_type::FN:
            parse_method_function_declaration();
            break;
        case token_type::UPDATE:
            parse_update_method_declaration();
            break;
        default:
            report_error_at_current();
        }
    }

    // methodFunctionDeclaration = "fn" functionSignature block ;
    // same as functionDeclaration, but main is not allowed
    void parse_method_function_declaration() {
        expect(token_type::FN);
        parse_function_signature();
        parse_block();
    }

    // updateMethodDeclaration = "update" "fn" IDENT parameterClause block ;
    void parse_update_method_declaration() {
        expect(token_type::UPDATE);
        expect(token_type::FN);
        expect_identifier();
        parse_parameter_clause();
        parse_block();
    }

    // ---------------------------------------------------------------------------------------
    // grammar rules (statement.txt)
    // ---------------------------------------------------------------------------------------

    // block = "{" { statement } "}" ;
    void parse_block() {
        expect(token_type::CURLY_OPEN);
        while (!check(token_type::CURLY_CLOSE))
            parse_statement();
        expect(token_type::CURLY_CLOSE);
    }

    // statement
    //     = variableDeclaration ";"
    //     | expressionStatement
    //     | returnStatement
    //     | printStatement
    //     | scanStatement
    //     | ifStatement
    //     | whileStatement
    //     | forStatement
    //     | stepStatement
    //     | invariantDeclaration
    //     | block ;
    void parse_statement() {
        switch (peek().type) {
        case token_type::LET:
        case token_type::STATE:
        case token_type::IMUT:
            parse_variable_declaration();
            expect(token_type::SEMI_COLON);
            break;
        case token_type::RETURN:
            parse_return_statement();
            break;
        case token_type::PRINT:
            parse_print_statement();
            break;
        case token_type::SCAN:
            parse_scan_statement();
            break;
        case token_type::IF:
            parse_if_statement();
            break;
        case token_type::WHILE:
            parse_while_statement();
            break;
        case token_type::FOR:
            parse_for_statement();
            break;
        case token_type::STEP:
            parse_step_statement();
            break;
        case token_type::INV:
            parse_invariant_declaration();
            break;
        case token_type::CURLY_OPEN:
            parse_block();
            break;
        default:
            parse_expression_statement();
        }
    }

    // expressionStatement = expression ";" ;
    void parse_expression_statement() {
        parse_expression();
        expect(token_type::SEMI_COLON);
    }

    // returnStatement = "return" expression ";" ;
    void parse_return_statement() {
        expect(token_type::RETURN);
        parse_expression();
        expect(token_type::SEMI_COLON);
    }

    // printStatement = "print" "(" [ argumentList ] ")" ";" ;
    void parse_print_statement() {
        expect(token_type::PRINT);
        expect(token_type::PARENTHESES_OPEN);
        if (!check(token_type::PARENTHESES_CLOSE))
            parse_argument_list();
        expect(token_type::PARENTHESES_CLOSE);
        expect(token_type::SEMI_COLON);
    }

    // scanStatement = "scan" ">>" prefixExpression { ">>" prefixExpression } ";" ;
    // targets are prefixExpressions so ">>" is not read as a shift: scan >> x >> y
    void parse_scan_statement() {
        expect(token_type::SCAN);
        expect(token_type::RIGHT_SHIFT);
        parse_prefix_expression();
        while (match(token_type::RIGHT_SHIFT))
            parse_prefix_expression();
        expect(token_type::SEMI_COLON);
    }

    // ifStatement
    //     = "if" "(" expression ")" block
    //       { "elif" "(" expression ")" block }
    //       [ "else" block ] ;
    void parse_if_statement() {
        expect(token_type::IF);
        expect(token_type::PARENTHESES_OPEN);
        parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        parse_block();

        while (match(token_type::ELIF)) {
            expect(token_type::PARENTHESES_OPEN);
            parse_expression();
            expect(token_type::PARENTHESES_CLOSE);
            parse_block();
        }

        if (match(token_type::ELSE))
            parse_block();
    }

    // whileStatement = "while" "(" expression ")" block ;
    void parse_while_statement() {
        expect(token_type::WHILE);
        expect(token_type::PARENTHESES_OPEN);
        parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        parse_block();
    }

    // forStatement = "for" "(" forInitializer ";" expression ";" expression ")" block ;
    void parse_for_statement() {
        expect(token_type::FOR);
        expect(token_type::PARENTHESES_OPEN);
        parse_for_initializer();
        expect(token_type::SEMI_COLON);
        parse_expression();
        expect(token_type::SEMI_COLON);
        parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
        parse_block();
    }

    // forInitializer = "let" binding "=" expression ;
    void parse_for_initializer() {
        expect(token_type::LET);
        parse_binding();
        expect(token_type::ASSIGN);
        parse_expression();
    }

    // stepStatement = "step" block [ "reject" block ] ;
    void parse_step_statement() {
        expect(token_type::STEP);
        parse_block();
        if (match(token_type::REJECT))
            parse_block();
    }

    // invariantDeclaration = "inv" "{" expression "}" ";" ;
    void parse_invariant_declaration() {
        expect(token_type::INV);
        expect(token_type::CURLY_OPEN);
        parse_expression();
        expect(token_type::CURLY_CLOSE);
        expect(token_type::SEMI_COLON);
    }

    // ---------------------------------------------------------------------------------------
    // grammar rules (expression.txt)
    // ---------------------------------------------------------------------------------------

    // expression = assignmentExpression ;
    void parse_expression() {
        parse_assignment_expression();
    }

    // assignmentExpression = logicalOrExpression [ "=" assignmentExpression ] ;
    void parse_assignment_expression() {
        parse_logical_or_expression();
        if (match(token_type::ASSIGN))
            parse_assignment_expression(); // recursion makes it right associative
    }

    // logicalOrExpression = logicalAndExpression { "||" logicalAndExpression } ;
    void parse_logical_or_expression() {
        parse_logical_and_expression();
        while (match(token_type::LOGICAL_OR))
            parse_logical_and_expression();
    }

    // logicalAndExpression = equalityExpression { "&&" equalityExpression } ;
    void parse_logical_and_expression() {
        parse_equality_expression();
        while (match(token_type::LOGICAL_AND))
            parse_equality_expression();
    }

    // equalityExpression = relationalExpression { ( "==" | "!=" ) relationalExpression } ;
    void parse_equality_expression() {
        parse_relational_expression();
        while (match_any({token_type::EQUAL, token_type::NOT_EQUAL}))
            parse_relational_expression();
    }

    // relationalExpression
    //     = bitwiseOrExpression { ( "<" | ">" | "<=" | ">=" ) bitwiseOrExpression } ;
    // "<-" directly after an operand is split into "<" "-", so x<-1 = x < -1
    void parse_relational_expression() {
        parse_bitwise_or_expression();
        while (!at_end()) {
            switch (current().type) {
            case token_type::DEREFERENCE:
                split_dereference();
                break;
            case token_type::LESSER:
            case token_type::GREATER:
            case token_type::LESS_OR_EQUAL:
            case token_type::GREATER_OR_EQUAL:
                advance();
                break;
            default:
                return;
            }
            parse_bitwise_or_expression();
        }
    }

    // bitwiseOrExpression = bitwiseXorExpression { "|" bitwiseXorExpression } ;
    // bitwise operators bind tighter than comparisons, so x & 1 == 0 = (x & 1) == 0
    void parse_bitwise_or_expression() {
        parse_bitwise_xor_expression();
        while (match(token_type::BIT_OR))
            parse_bitwise_xor_expression();
    }

    // bitwiseXorExpression = bitwiseAndExpression { "^" bitwiseAndExpression } ;
    void parse_bitwise_xor_expression() {
        parse_bitwise_and_expression();
        while (match(token_type::XOR))
            parse_bitwise_and_expression();
    }

    // bitwiseAndExpression = shiftExpression { "&" shiftExpression } ;
    void parse_bitwise_and_expression() {
        parse_shift_expression();
        while (match(token_type::BIT_AND))
            parse_shift_expression();
    }

    // shiftExpression = additiveExpression { ( "<<" | ">>" ) additiveExpression } ;
    void parse_shift_expression() {
        parse_additive_expression();
        while (match_any({token_type::LEFT_SHIFT, token_type::RIGHT_SHIFT}))
            parse_additive_expression();
    }

    // additiveExpression = multiplicativeExpression { ( "+" | "-" ) multiplicativeExpression } ;
    void parse_additive_expression() {
        parse_multiplicative_expression();
        while (match_any({token_type::PLUS, token_type::MINUS}))
            parse_multiplicative_expression();
    }

    // multiplicativeExpression = unaryExpression { ( "*" | "/" | "\" ) unaryExpression } ;
    void parse_multiplicative_expression() {
        parse_unary_expression();
        while (match_any({token_type::MULTIPLY, token_type::DIVIDE, token_type::DIVIDE_FLOOR}))
            parse_unary_expression();
    }

    // unaryExpression = ( "+" | "-" | "!" ) unaryExpression | powerExpression ;
    void parse_unary_expression() {
        switch (peek().type) {
        case token_type::PLUS:
        case token_type::MINUS:
        case token_type::NOT:
            advance();
            parse_unary_expression();
            break;
        default:
            parse_power_expression();
        }
    }

    // powerExpression = prefixExpression [ "~" unaryExpression ] ;
    void parse_power_expression() {
        parse_prefix_expression();
        if (match(token_type::EXPONENT))
            parse_unary_expression(); // recursion makes it right associative
    }

    // prefixExpression = ( "@" | "<-" ) prefixExpression | postfixExpression ;
    void parse_prefix_expression() {
        switch (peek().type) {
        case token_type::ADDRESS:
        case token_type::DEREFERENCE:
            advance();
            parse_prefix_expression();
            break;
        default:
            parse_postfix_expression();
        }
    }

    // postfixExpression = primaryExpression { postfixSuffix } ;
    void parse_postfix_expression() {
        parse_primary_expression();
        while (
            check_any({token_type::PARENTHESES_OPEN, token_type::SQUARE_OPEN, token_type::ACCESS}))
            parse_postfix_suffix();
    }

    // postfixSuffix = argumentClause | "[" expression "]" | "." IDENT ;
    void parse_postfix_suffix() {
        switch (peek().type) {
        case token_type::PARENTHESES_OPEN:
            parse_argument_clause();
            break;
        case token_type::SQUARE_OPEN:
            advance();
            parse_expression();
            expect(token_type::SQUARE_CLOSE);
            break;
        case token_type::ACCESS:
            advance();
            expect_identifier();
            break;
        default:
            report_error_at_current();
        }
    }

    // argumentClause = "(" [ argumentList ] ")" ;
    void parse_argument_clause() {
        expect(token_type::PARENTHESES_OPEN);
        if (!check(token_type::PARENTHESES_CLOSE))
            parse_argument_list();
        expect(token_type::PARENTHESES_CLOSE);
    }

    // argumentList = expression { "," expression } ;
    void parse_argument_list() {
        parse_expression();
        while (match(token_type::COMMA))
            parse_expression();
    }

    // primaryExpression
    //     = literal
    //     | IDENT [ structInitializer ]
    //     | "this"
    //     | "(" expression ")"
    //     | arrayLiteral
    //     | conversionExpression
    //     | allocationExpression ;
    void parse_primary_expression() {
        switch (peek().type) {
        case token_type::INT_LITERAL:
        case token_type::BASE_2_LITERAL:
        case token_type::BASE_16_LITERAL:
        case token_type::FLOAT_LITERAL:
        case token_type::CHAR_LITERAL:
        case token_type::STRING_LITERAL:
        case token_type::BOOL_LITERAL:
            parse_literal();
            break;
        case token_type::IDENTIFIER:
            expect_identifier();
            if (check(token_type::CURLY_OPEN))
                parse_struct_initializer();
            break;
        case token_type::THIS:
            advance();
            break;
        case token_type::PARENTHESES_OPEN:
            advance();
            parse_expression();
            expect(token_type::PARENTHESES_CLOSE);
            break;
        case token_type::SQUARE_OPEN:
            parse_array_literal();
            break;
        case token_type::INT:
        case token_type::FLOAT:
        case token_type::CHAR:
        case token_type::BOOL:
        case token_type::BYTE:
            parse_conversion_expression();
            break;
        case token_type::NEW:
            parse_allocation_expression();
            break;
        default:
            report_error_at_current();
        }
    }

    // literal
    //     = INT_LITERAL | BASE_2_LITERAL | BASE_16_LITERAL | FLOAT_LITERAL
    //     | CHAR_LITERAL | STRING_LITERAL | BOOL_LITERAL ;
    void parse_literal() {
        switch (peek().type) {
        case token_type::INT_LITERAL:
        case token_type::BASE_2_LITERAL:
        case token_type::BASE_16_LITERAL:
        case token_type::FLOAT_LITERAL:
        case token_type::CHAR_LITERAL:
        case token_type::STRING_LITERAL:
        case token_type::BOOL_LITERAL:
            advance();
            break;
        default:
            report_error_at_current();
        }
    }

    // arrayLiteral = "[" [ argumentList ] "]" ;
    void parse_array_literal() {
        expect(token_type::SQUARE_OPEN);
        if (!check(token_type::SQUARE_CLOSE))
            parse_argument_list();
        expect(token_type::SQUARE_CLOSE);
    }

    // structInitializer = "{" [ fieldInitializerList ] "}" ;
    void parse_struct_initializer() {
        expect(token_type::CURLY_OPEN);
        if (!check(token_type::CURLY_CLOSE))
            parse_field_initializer_list();
        expect(token_type::CURLY_CLOSE);
    }

    // fieldInitializerList = fieldInitializer { "," fieldInitializer } ;
    void parse_field_initializer_list() {
        parse_field_initializer();
        while (match(token_type::COMMA))
            parse_field_initializer();
    }

    // fieldInitializer = IDENT ":" expression ;
    void parse_field_initializer() {
        expect_identifier();
        expect(token_type::COLON);
        parse_expression();
    }

    // conversionExpression = primitiveType "(" expression ")" ;
    void parse_conversion_expression() {
        parse_primitive_type();
        expect(token_type::PARENTHESES_OPEN);
        parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
    }

    // allocationExpression = "new" "(" type "," expression ")" ;
    void parse_allocation_expression() {
        expect(token_type::NEW);
        expect(token_type::PARENTHESES_OPEN);
        parse_type();
        expect(token_type::COMMA);
        parse_expression();
        expect(token_type::PARENTHESES_CLOSE);
    }
};

void parse(const std::vector<token> &tokens) {
    Parser p(tokens);
    p.parse();
}
