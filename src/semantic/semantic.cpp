#include "semantic.h"

#include "../symbol_table/symbol_table.h"
#include "../visitor/visitor.h"
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {
[[noreturn]] void fail(const char *code, const source_span &span, const std::string &message) {
    throw std::runtime_error(std::format("{} at line {}, column {}: {}", code, span.start_line,
                                         span.start_col, message));
}

struct value_type {
    enum class form { PRIMITIVE, STRUCT, POINTER, ARRAY } shape = form::PRIMITIVE;
    primitive_type primitive = primitive_type::VOID;
    std::string name;
    std::shared_ptr<value_type> element;
    std::int32_t length = 0;

    explicit value_type(primitive_type type = primitive_type::VOID) : primitive(type) {
    }
    static value_type named(std::string name) {
        value_type type;
        type.shape = form::STRUCT;
        type.name = std::move(name);
        return type;
    }
    static value_type pointer(value_type pointee) {
        value_type type;
        type.shape = form::POINTER;
        type.element = std::make_shared<value_type>(std::move(pointee));
        return type;
    }
    static value_type array(value_type element, std::int32_t length) {
        value_type type;
        type.shape = form::ARRAY;
        type.element = std::make_shared<value_type>(std::move(element));
        type.length = length;
        return type;
    }
};

bool same(const value_type &a, const value_type &b) {
    if (a.shape != b.shape)
        return false;
    switch (a.shape) {
    case value_type::form::PRIMITIVE:
        return a.primitive == b.primitive;
    case value_type::form::STRUCT:
        return a.name == b.name;
    case value_type::form::POINTER:
        return same(*a.element, *b.element);
    case value_type::form::ARRAY:
        return a.length == b.length && same(*a.element, *b.element);
    }
    return false;
}

std::string describe(const value_type &type) {
    switch (type.shape) {
    case value_type::form::PRIMITIVE:
        return std::string(spelling(type.primitive));
    case value_type::form::STRUCT:
        return type.name;
    case value_type::form::POINTER:
        return "->" + describe(*type.element);
    case value_type::form::ARRAY:
        return std::format("[{}]{}", type.length, describe(*type.element));
    }
    return "<invalid>";
}

bool numeric(const value_type &type) {
    return type.shape == value_type::form::PRIMITIVE &&
           (type.primitive == primitive_type::INT || type.primitive == primitive_type::FLOAT);
}
bool truthy(const value_type &type) {
    return numeric(type) ||
           (type.shape == value_type::form::PRIMITIVE && type.primitive == primitive_type::BOOL);
}
bool compatible(const value_type &expected, const value_type &actual) {
    return same(expected, actual) || (numeric(expected) && numeric(actual));
}

class checker final : public visitor {
    struct binding {
        value_type type;
        bool state = false;
        bool immutable = false;
    };
    struct expr_result {
        value_type type;
        bool location = false;
        bool state = false;
        bool immutable = false;
        const function_decl *callable = nullptr;
        bool statement = false;
        bool receiver = false;
        bool receiver_storage = false;
    };
    struct method_call {
        const function_decl *method;
        const function_decl *caller;
        source_span source;
        bool location;
        bool state;
        bool immutable;
        bool through_receiver;
    };

    symbol_table<binding, const struct_decl *, const function_decl *> symbols;
    std::unordered_map<const function_decl *, bool> writes_receiver;
    std::vector<method_call> method_calls;
    std::optional<expr_result> result;
    std::optional<value_type> resolved_type;
    std::optional<value_type> expected_array;
    bool initializer_context = false;
    const function_decl *current_function = nullptr;
    const struct_decl *current_struct = nullptr;
    int main_count = 0;
    bool global_mode = false;
    bool in_step = false;
    bool in_reject = false;
    bool in_invariant = false;

    expr_result evaluate(const expr &node, std::optional<value_type> expected = std::nullopt,
                         bool initializer = false) {
        const auto prior = expected_array;
        const bool prior_initializer = initializer_context;
        expected_array = std::move(expected);
        initializer_context = initializer;
        result.reset();
        node.accept(*this);
        expected_array = prior;
        initializer_context = prior_initializer;
        if (!result)
            fail("SEM000", node.source, "expression is not supported yet");
        return *result;
    }
    value_type resolve(const type_syntax &node) {
        resolved_type.reset();
        node.accept(*this);
        if (!resolved_type)
            fail("SEM000", node.source, "type is not supported yet");
        return *resolved_type;
    }
    void require(const value_type &expected, const value_type &actual, const source_span &span,
                 bool exact = false) {
        if (!(exact ? same(expected, actual) : compatible(expected, actual)))
            fail("SEM003", span,
                 std::format("expected {}, got {}", describe(expected), describe(actual)));
    }
    void declare(const std::string &name, binding symbol, const source_span &span) {
        if (!symbols.declare_variable(name, std::move(symbol)))
            fail("SEM002", span, "duplicate name '" + name + "'");
    }
    void arguments(const std::vector<parameter> &params, const arg_list &args, bool state_args) {
        if (params.size() != args.size())
            fail("SEM006", current_call_span, "wrong number of arguments");
        for (size_t i = 0; i < args.size(); ++i) {
            const value_type type = resolve(*params[i].declared_type);
            const expr_result actual = evaluate(*args[i], type);
            require(type, actual.type, args[i]->source, true);
            if (state_args && (!actual.location || !actual.state || actual.immutable))
                fail("SEM018", args[i]->source, "state argument must be mutable state storage");
        }
    }
    source_span current_call_span{0, 0, 0, 0};

    std::optional<std::int32_t> constant(const expr &node) {
        if (const auto *number = dynamic_cast<const literal *>(&node)) {
            if (const auto *integer = std::get_if<std::int32_t>(&number->value))
                return *integer;
        }
        if (const auto *unary = dynamic_cast<const unary_expr *>(&node)) {
            if (unary->operation == unary_op::NEGATE) {
                const auto value = constant(*unary->right);
                if (value && *value != std::numeric_limits<std::int32_t>::min())
                    return -*value;
            }
        }
        if (const auto *binary = dynamic_cast<const binary_expr *>(&node)) {
            const auto left = constant(*binary->left);
            const auto right = constant(*binary->right);
            if (!left || !right)
                return std::nullopt;
            std::int64_t value = 0;
            switch (binary->operation) {
            case binary_op::ADD:
                value = std::int64_t(*left) + *right;
                break;
            case binary_op::SUBTRACT:
                value = std::int64_t(*left) - *right;
                break;
            case binary_op::MULTIPLY:
                value = std::int64_t(*left) * *right;
                break;
            case binary_op::DIVIDE:
                if (*right == 0)
                    return std::nullopt;
                value = *left / std::int64_t(*right);
                break;
            default:
                return std::nullopt;
            }
            if (value >= std::numeric_limits<std::int32_t>::min() &&
                value <= std::numeric_limits<std::int32_t>::max())
                return static_cast<std::int32_t>(value);
        }
        return std::nullopt;
    }

    std::optional<double> numeric_constant(const expr &node) {
        // Preserve integer division when a constant expression contains only integers.
        if (const auto integer = constant(node))
            return static_cast<double>(*integer);
        if (const auto *number = dynamic_cast<const literal *>(&node)) {
            if (const auto *integer = std::get_if<std::int32_t>(&number->value))
                return static_cast<double>(*integer);
            if (const auto *floating = std::get_if<double>(&number->value))
                return *floating;
        }
        if (const auto *unary = dynamic_cast<const unary_expr *>(&node)) {
            if (unary->operation == unary_op::NEGATE) {
                if (const auto operand = numeric_constant(*unary->right))
                    return -*operand;
            }
        }
        if (const auto *cast = dynamic_cast<const conversion_expr *>(&node)) {
            const auto *type = dynamic_cast<const primitive_type_syntax *>(cast->target_type.get());
            const auto operand = numeric_constant(*cast->value);
            if (type && operand && std::isfinite(*operand)) {
                if (type->type == primitive_type::FLOAT)
                    return *operand;
                if (type->type == primitive_type::INT &&
                    *operand >= std::numeric_limits<std::int32_t>::min() &&
                    *operand <= std::numeric_limits<std::int32_t>::max())
                    return std::trunc(*operand);
            }
        }
        if (const auto *binary = dynamic_cast<const binary_expr *>(&node)) {
            const auto left = numeric_constant(*binary->left);
            const auto right = numeric_constant(*binary->right);
            if (!left || !right)
                return std::nullopt;
            switch (binary->operation) {
            case binary_op::ADD:
                return *left + *right;
            case binary_op::SUBTRACT:
                return *left - *right;
            case binary_op::MULTIPLY:
                return *left * *right;
            case binary_op::DIVIDE:
            case binary_op::FLOOR_DIVIDE:
                if (*right != 0)
                    return *left / *right;
                break;
            default:
                break;
            }
        }
        return std::nullopt;
    }

    static bool guarantees_return(const block_stmt &body) {
        for (const stmt_ptr &statement : body.statements) {
            if (dynamic_cast<const return_stmt *>(statement.get()))
                return true;
            if (const auto *nested = dynamic_cast<const block_stmt *>(statement.get())) {
                if (guarantees_return(*nested))
                    return true;
            }
            if (const auto *branch = dynamic_cast<const if_stmt *>(statement.get())) {
                bool all = branch->else_body && guarantees_return(*branch->else_body);
                for (const conditional_branch &part : branch->branches)
                    all = all && guarantees_return(*part.body);
                if (all)
                    return true;
            }
        }
        return false;
    }

  public:
    void run(const program &root) {
        global_mode = true;
        for (const decl_ptr &entry : root.declarations)
            entry->accept(*this);
        global_mode = false;
        if (main_count != 1)
            fail("SEM000",
                 root.declarations.empty() ? source_span{0, 0, 0, 0}
                                           : root.declarations.back()->source,
                 "exactly one fn main() is required");

        // Method calls on this can transmit receiver writes through recursive call chains.
        bool changed;
        do {
            changed = false;
            for (const method_call &call : method_calls) {
                if (call.caller && call.through_receiver && writes_receiver[call.method] &&
                    !writes_receiver[call.caller]) {
                    writes_receiver[call.caller] = true;
                    changed = true;
                }
            }
        } while (changed);
        for (const method_call &call : method_calls) {
            if (!writes_receiver[call.method])
                continue;
            if (!call.location || call.immutable)
                fail("SEM012", call.source, "writing method requires a mutable receiver");
            if (call.state)
                fail("SEM017", call.source, "ordinary method cannot modify state storage");
        }
    }

    void visit(const program &) override {
    }

    void visit(const literal &node) override {
        if (std::holds_alternative<std::int32_t>(node.value))
            result = expr_result{value_type{primitive_type::INT}};
        else if (std::holds_alternative<double>(node.value))
            result = expr_result{value_type{primitive_type::FLOAT}};
        else if (std::holds_alternative<char>(node.value))
            result = expr_result{value_type{primitive_type::CHAR}};
        else if (std::holds_alternative<std::byte>(node.value))
            result = expr_result{value_type{primitive_type::BYTE}};
        else if (std::holds_alternative<bool>(node.value))
            result = expr_result{value_type{primitive_type::BOOL}};
        else if (const auto *text = std::get_if<std::string>(&node.value)) {
            if (text->size() > static_cast<size_t>(std::numeric_limits<std::int32_t>::max()))
                fail("SEM013", node.source, "string literal is too long");
            const value_type chars{primitive_type::CHAR};
            if (initializer_context && expected_array &&
                expected_array->shape == value_type::form::ARRAY &&
                same(*expected_array->element, chars)) {
                if (text->size() > static_cast<size_t>(expected_array->length))
                    fail("SEM013", node.source, "string has more characters than the array holds");
                result = expr_result{*expected_array};
            } else {
                if (text->empty())
                    fail("SEM013", node.source, "empty string requires a char-array initializer");
                result =
                    expr_result{value_type::array(chars, static_cast<std::int32_t>(text->size()))};
            }
        }
    }
    void visit(const identifier &node) override {
        if (const binding *symbol = symbols.lookup_variable(node.lexeme)) {
            if (in_invariant && !symbol->state)
                fail("SEM020", node.source, "invariants may read only state variables");
            result = expr_result{symbol->type, true, symbol->state, symbol->immutable};
        } else if (const auto fn = symbols.lookup_function(node.lexeme)) {
            if (in_invariant)
                fail("SEM020", node.source, "invariants cannot reference functions");
            result = expr_result{value_type{}, false, false, false, *fn};
        } else
            fail("SEM001", node.source, "unknown name '" + node.lexeme + "'");
    }
    void visit(const conversion_expr &node) override {
        const value_type target = resolve(*node.target_type);
        if (target.shape != value_type::form::PRIMITIVE || target.primitive == primitive_type::VOID)
            fail("SEM004", node.source, "invalid conversion target");
        const value_type source = evaluate(*node.value).type;
        if (source.shape != value_type::form::PRIMITIVE || source.primitive == primitive_type::VOID)
            fail("SEM004", node.source, "invalid conversion source");
        result = expr_result{target};
    }
    void visit(const unary_expr &node) override {
        if (in_invariant &&
            (node.operation == unary_op::DEREFERENCE || node.operation == unary_op::ADDRESS))
            fail("SEM020", node.source, "invariants cannot use pointer operators");
        const expr_result operand = evaluate(*node.right);
        switch (node.operation) {
        case unary_op::LOGICAL_NOT:
            if (truthy(operand.type)) {
                result = expr_result{value_type{primitive_type::BOOL}};
                return;
            }
            break;
        case unary_op::NEGATE:
            if (numeric(operand.type)) {
                result = expr_result{operand.type};
                return;
            }
            break;
        case unary_op::ADDRESS:
            if (operand.location && !operand.immutable && !operand.state &&
                !operand.receiver_storage) {
                result = expr_result{value_type::pointer(operand.type)};
                return;
            }
            fail("SEM016", node.source, "address-of requires mutable non-state storage");
        case unary_op::DEREFERENCE:
            if (operand.type.shape == value_type::form::POINTER) {
                result = expr_result{*operand.type.element, true};
                return;
            }
            fail("SEM015", node.source, "dereference requires a pointer");
        }
        fail("SEM004", node.source, "invalid unary operand");
    }
    void visit(const binary_expr &node) override {
        const value_type left = evaluate(*node.left).type;
        const value_type right = evaluate(*node.right).type;
        const bool both_numeric = numeric(left) && numeric(right);
        switch (node.operation) {
        case binary_op::ADD:
        case binary_op::SUBTRACT:
            if (node.operation == binary_op::SUBTRACT && left.shape == value_type::form::POINTER &&
                right.shape == value_type::form::POINTER && same(left, right)) {
                result = expr_result{value_type{primitive_type::INT}};
                return;
            }
            if (left.shape == value_type::form::POINTER && right.primitive == primitive_type::INT &&
                right.shape == value_type::form::PRIMITIVE) {
                result = expr_result{left};
                return;
            }
            if (node.operation == binary_op::ADD && numeric(left) &&
                left.primitive == primitive_type::INT && right.shape == value_type::form::POINTER) {
                result = expr_result{right};
                return;
            }
            [[fallthrough]];
        case binary_op::MULTIPLY:
        case binary_op::DIVIDE:
        case binary_op::FLOOR_DIVIDE:
        case binary_op::EXPONENT:
            if (both_numeric) {
                if ((node.operation == binary_op::DIVIDE ||
                     node.operation == binary_op::FLOOR_DIVIDE) &&
                    numeric_constant(*node.right) == 0.0)
                    fail("SEM005", node.right->source, "division by constant zero");
                result = expr_result{value_type{left.primitive == primitive_type::FLOAT ||
                                                        right.primitive == primitive_type::FLOAT
                                                    ? primitive_type::FLOAT
                                                    : primitive_type::INT}};
                return;
            }
            break;
        case binary_op::BITWISE_AND:
        case binary_op::BITWISE_OR:
        case binary_op::BITWISE_XOR:
        case binary_op::LEFT_SHIFT:
        case binary_op::RIGHT_SHIFT:
            if (left.shape == value_type::form::PRIMITIVE &&
                right.shape == value_type::form::PRIMITIVE &&
                left.primitive == primitive_type::INT && right.primitive == primitive_type::INT) {
                if (node.operation == binary_op::LEFT_SHIFT ||
                    node.operation == binary_op::RIGHT_SHIFT) {
                    const auto shift = constant(*node.right);
                    if (shift && (*shift < 0 || *shift >= 32))
                        fail("SEM004", node.right->source, "shift count must be between 0 and 31");
                }
                result = expr_result{value_type{primitive_type::INT}};
                return;
            }
            break;
        case binary_op::LOGICAL_AND:
        case binary_op::LOGICAL_OR:
            if (truthy(left) && truthy(right)) {
                result = expr_result{value_type{primitive_type::BOOL}};
                return;
            }
            break;
        case binary_op::EQUALS:
        case binary_op::NOT_EQUALS:
            if (both_numeric || (same(left, right) && (left.shape == value_type::form::POINTER ||
                                                       (left.shape == value_type::form::PRIMITIVE &&
                                                        left.primitive != primitive_type::VOID)))) {
                result = expr_result{value_type{primitive_type::BOOL}};
                return;
            }
            break;
        case binary_op::GT:
        case binary_op::GTE:
        case binary_op::LT:
        case binary_op::LTE:
            if (both_numeric || (same(left, right) && left.shape == value_type::form::PRIMITIVE &&
                                 (left.primitive == primitive_type::CHAR ||
                                  left.primitive == primitive_type::BYTE))) {
                result = expr_result{value_type{primitive_type::BOOL}};
                return;
            }
            break;
        }
        if (left.shape == value_type::form::POINTER || right.shape == value_type::form::POINTER)
            fail("SEM015", node.source, "invalid pointer operation");
        fail("SEM004", node.source, "invalid binary operand types");
    }
    void visit(const assignment_expr &node) override {
        if (in_invariant)
            fail("SEM020", node.source, "invariants cannot assign to storage");
        const expr_result target = evaluate(*node.target);
        if (!target.location)
            fail("SEM004", node.target->source, "assignment target is not storage");
        if (target.immutable)
            fail("SEM012", node.target->source, "cannot assign to immutable storage");
        const bool in_update = current_function && current_function->kind == function_kind::UPDATE;
        if (target.state &&
            (in_reject || (!in_step && !in_update) ||
             (current_function && !in_update && current_function->name->lexeme != "main")))
            fail("SEM017", node.target->source, "state write outside a step or update context");
        if (target.receiver_storage && current_function)
            writes_receiver[current_function] = true;
        const expr_result value = evaluate(*node.value, target.type);
        require(target.type, value.type, node.value->source);
        result = expr_result{target.type, false, false, false, nullptr, true};
    }
    void visit(const array_literal &node) override {
        std::optional<value_type> target = expected_array;
        if (!target && !node.elements.empty())
            target = value_type::array(evaluate(*node.elements.front()).type,
                                       static_cast<std::int32_t>(node.elements.size()));
        if (!target || target->shape != value_type::form::ARRAY)
            fail("SEM013", node.source, "array literal needs an array type");
        if (node.elements.size() > static_cast<size_t>(target->length))
            fail("SEM013", node.source, "too many array elements");
        for (const expr_ptr &element : node.elements)
            require(*target->element, evaluate(*element, *target->element, true).type,
                    element->source);
        result = expr_result{*target};
    }
    void visit(const struct_literal &node) override {
        const struct_decl *structure = symbols.lookup_struct(node.struct_type).value_or(nullptr);
        if (!structure)
            fail("SEM001", node.source, "unknown struct '" + node.struct_type + "'");
        std::unordered_set<std::string> seen;
        for (const field_init &field : node.init_list) {
            const field_decl *member = nullptr;
            for (const field_decl &candidate : structure->fields)
                if (candidate.name == field.name) {
                    member = &candidate;
                    break;
                }
            if (!member || !seen.insert(field.name).second)
                fail("SEM004", field.val->source,
                     "unknown or repeated struct field '" + field.name + "'");
            const value_type type = resolve(*member->declared_type);
            require(type, evaluate(*field.val, type, true).type, field.val->source);
        }
        result = expr_result{value_type::named(node.struct_type)};
    }
    void visit(const dynamic_alloc &node) override {
        if (in_invariant)
            fail("SEM020", node.source, "invariants cannot allocate storage");
        const value_type element = resolve(*node.element_type);
        const value_type count = evaluate(*node.count).type;
        require(value_type{primitive_type::INT}, count, node.count->source, true);
        result = expr_result{value_type::pointer(element)};
    }
    void visit(const this_expr &node) override {
        if (in_invariant)
            fail("SEM020", node.source, "invariants cannot reference this");
        if (!current_struct)
            fail("SEM001", node.source, "this is only valid in a method");
        result = expr_result{value_type::named(current_struct->name->lexeme), true,
                             current_function && current_function->kind == function_kind::UPDATE};
        result->receiver_storage = true;
    }
    void visit(const postfix_expr &node) override {
        if (in_invariant &&
            (node.op == postfix_op::FUNCT_CALL || node.op == postfix_op::UPDATE_CALL))
            fail("SEM020", node.source, "invariants cannot call functions");
        const expr_result object = evaluate(*node.obj);
        switch (node.op) {
        case postfix_op::SUBSCRIPT: {
            if (in_invariant && object.type.shape == value_type::form::POINTER)
                fail("SEM020", node.source, "invariants cannot dereference pointers");
            const value_type index = evaluate(*std::get<expr_ptr>(node.arg)).type;
            require(value_type{primitive_type::INT}, index, node.source, true);
            if (object.type.shape != value_type::form::ARRAY &&
                object.type.shape != value_type::form::POINTER)
                fail("SEM014", node.source, "indexing requires an array or pointer");
            const bool pointer = object.type.shape == value_type::form::POINTER;
            result = expr_result{*object.type.element, pointer || object.location,
                                 !pointer && object.state, !pointer && object.immutable};
            result->receiver_storage = !pointer && object.receiver_storage;
            return;
        }
        case postfix_op::MEMBER_ACCESS: {
            if (object.type.shape != value_type::form::STRUCT)
                fail("SEM004", node.source, "member access requires a struct");
            const std::string &member = std::get<std::string>(node.arg);
            if (const auto method = symbols.lookup_method(object.type.name, member)) {
                result = expr_result{value_type{}, object.location, object.state, object.immutable,
                                     *method,      false,           true};
                result->receiver_storage = object.receiver_storage;
                return;
            }
            const struct_decl *structure =
                symbols.lookup_struct(object.type.name).value_or(nullptr);
            if (!structure)
                fail("SEM001", node.source, "unknown struct type");
            for (const field_decl &field : structure->fields)
                if (field.name == member) {
                    result = expr_result{resolve(*field.declared_type), object.location,
                                         object.state, object.immutable};
                    result->receiver_storage = object.receiver_storage;
                    return;
                }
            fail("SEM001", node.source, "unknown field '" + member + "'");
        }
        case postfix_op::FUNCT_CALL:
        case postfix_op::UPDATE_CALL: {
            const function_decl *fn = object.callable;
            if (!fn)
                fail("SEM006", node.source, "expression is not callable");
            current_call_span = node.source;
            const bool update = fn->kind == function_kind::UPDATE;
            if (update && object.receiver &&
                (!object.location || !object.state || object.immutable))
                fail("SEM018", node.source, "update method requires a mutable state receiver");
            if (!update && object.receiver)
                method_calls.push_back(method_call{fn, current_function, node.source,
                                                   object.location, object.state, object.immutable,
                                                   object.receiver_storage});
            if (node.op == postfix_op::UPDATE_CALL && !update)
                fail("SEM018", node.source, "two argument clauses require an update function");
            if (update && (!in_step && (!current_function ||
                                        current_function->kind != function_kind::UPDATE) ||
                           in_reject))
                fail("SEM018", node.source, "update call requires an active step");
            if (node.op == postfix_op::UPDATE_CALL) {
                const auto &args = std::get<update_arguments>(node.arg);
                arguments(fn->state_parameters, args.state, true);
                arguments(fn->ordinary_parameters, args.regular, false);
            } else {
                if (update && !fn->state_parameters.empty())
                    fail("SEM018", node.source, "update function requires state arguments");
                arguments(fn->ordinary_parameters, std::get<arg_list>(node.arg), false);
            }
            result = expr_result{fn->return_type ? resolve(*fn->return_type)
                                                 : value_type{primitive_type::VOID},
                                 false,
                                 false,
                                 false,
                                 nullptr,
                                 true};
            return;
        }
        }
    }

    void visit(const primitive_type_syntax &node) override {
        resolved_type = value_type{node.type};
    }
    void visit(const named_type_syntax &node) override {
        if (!symbols.lookup_struct(node.name))
            fail("SEM001", node.source, "unknown type '" + node.name + "'");
        resolved_type = value_type::named(node.name);
    }
    void visit(const pointer_type_syntax &node) override {
        resolved_type = value_type::pointer(resolve(*node.pointee));
    }
    void visit(const array_type_syntax &node) override {
        const auto length = constant(*node.size);
        if (!length || *length <= 0)
            fail("SEM013", node.size->source, "array size must be a positive constant");
        resolved_type = value_type::array(resolve(*node.element), *length);
    }

    void visit(const variable_decl &node) override {
        if (in_reject && node.storage == variable_storage::STATE)
            fail("SEM017", node.source, "reject branch cannot declare state");
        const value_type type = resolve(*node.declared_type);
        if (type.shape == value_type::form::PRIMITIVE && type.primitive == primitive_type::VOID)
            fail("SEM004", node.source, "void is not a variable type");
        if ((node.immutable || node.storage == variable_storage::STATE) && !node.initializer)
            fail("SEM011", node.source, "state and immutable variables need an initializer");
        if (node.initializer)
            require(type, evaluate(*node.initializer, type, true).type, node.initializer->source);
        declare(node.name->lexeme,
                binding{type, node.storage == variable_storage::STATE, node.immutable},
                node.name->source);
    }
    void visit(const struct_decl &node) override {
        if (!global_mode || !symbols.declare_struct(node.name->lexeme, &node))
            fail("SEM002", node.source, "duplicate or nested struct");
        std::unordered_set<std::string> names;
        for (const field_decl &field : node.fields) {
            if (!names.insert(field.name).second)
                fail("SEM002", node.source, "duplicate field '" + field.name + "'");
            resolve(*field.declared_type);
        }
    }
    void visit(const impl_decl &node) override {
        const struct_decl *structure =
            symbols.lookup_struct(node.type_name->lexeme).value_or(nullptr);
        if (!structure)
            fail("SEM001", node.source, "unknown impl type");
        for (const function_decl_ptr &method : node.methods)
            if (!symbols.declare_method(node.type_name->lexeme, method->name->lexeme, method.get()))
                fail("SEM002", method->source, "duplicate method");
        const struct_decl *prior = current_struct;
        current_struct = structure;
        for (const function_decl_ptr &method : node.methods)
            method->accept(*this);
        current_struct = prior;
    }
    void visit(const function_decl &node) override {
        if (!current_struct) {
            if (!global_mode || !symbols.declare_function(node.name->lexeme, &node))
                fail("SEM002", node.source, "duplicate function");
        }
        if (node.name->lexeme == "main") {
            if (current_struct || node.kind != function_kind::ORDINARY ||
                !node.ordinary_parameters.empty() || ++main_count != 1)
                fail("SEM002", node.source, "invalid main function");
        }
        auto function_scope = symbols.enter_scope();
        const function_decl *prior = current_function;
        const bool prior_global_mode = global_mode;
        current_function = &node;
        global_mode = false;
        for (const parameter &param : node.state_parameters)
            declare(param.name, binding{resolve(*param.declared_type), true}, node.source);
        for (const parameter &param : node.ordinary_parameters)
            declare(param.name, binding{resolve(*param.declared_type)}, node.source);
        node.body->accept(*this);
        if (node.return_type) {
            const value_type return_type = resolve(*node.return_type);
            if (!(return_type.shape == value_type::form::PRIMITIVE &&
                  return_type.primitive == primitive_type::VOID) &&
                !guarantees_return(*node.body))
                fail("SEM008", node.source, "function has a path without a return value");
        }
        current_function = prior;
        global_mode = prior_global_mode;
    }
    void visit(const invariant_decl &node) override {
        in_invariant = true;
        const value_type type = evaluate(*node.condition).type;
        if (type.shape != value_type::form::PRIMITIVE || type.primitive != primitive_type::BOOL)
            fail("SEM020", node.condition->source, "invariant condition must have type bool");
        in_invariant = false;
    }
    void visit(const block_stmt &node) override {
        auto block_scope = symbols.enter_scope();
        for (const stmt_ptr &stmt : node.statements)
            stmt->accept(*this);
    }
    void visit(const decl_stmt &node) override {
        node.declaration->accept(*this);
    }
    void visit(const expr_stmt &node) override {
        if (!evaluate(*node.expression).statement)
            fail("SEM004", node.source, "statement must be an assignment or call");
    }
    void visit(const return_stmt &node) override {
        if (!current_function || in_step || in_reject)
            fail("SEM023", node.source, "return is not allowed here");
        if (!current_function->return_type) {
            if (node.value)
                fail("SEM007", node.source, "update function cannot return a value");
        } else {
            const value_type type = resolve(*current_function->return_type);
            if (node.value)
                require(type, evaluate(*node.value, type).type, node.value->source, true);
            else if (type.primitive != primitive_type::VOID ||
                     type.shape != value_type::form::PRIMITIVE)
                fail("SEM007", node.source, "return value required");
        }
    }
    void visit(const if_stmt &node) override {
        for (const conditional_branch &branch : node.branches) {
            if (!truthy(evaluate(*branch.condition).type))
                fail("SEM004", branch.condition->source, "condition must be bool or numeric");
            branch.body->accept(*this);
        }
        if (node.else_body)
            node.else_body->accept(*this);
    }
    void visit(const while_stmt &node) override {
        if (!truthy(evaluate(*node.condition).type))
            fail("SEM004", node.condition->source, "condition must be bool or numeric");
        node.body->accept(*this);
    }
    void visit(const for_stmt &node) override {
        auto loop_scope = symbols.enter_scope();
        node.initializer->accept(*this);
        if (!truthy(evaluate(*node.condition).type))
            fail("SEM004", node.condition->source, "condition must be bool or numeric");
        evaluate(*node.increment);
        node.body->accept(*this);
    }
    void visit(const step_stmt &node) override {
        if (in_step || in_reject ||
            (current_function && current_function->kind == function_kind::UPDATE))
            fail("SEM019", node.source, "nested or update-function step");
        in_step = true;
        node.body->accept(*this);
        in_step = false;
        if (node.reject_body) {
            in_reject = true;
            node.reject_body->accept(*this);
            in_reject = false;
        }
    }
};
} // namespace

void analyse(const program &root) {
    checker{}.run(root);
}
