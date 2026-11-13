#include "code_generator.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace {

// Keep source names unchanged in the AST and symbol tables. Only emitted C++
// identifiers are mangled. Escape names that could still be reserved/collide
// after prefixing; encoding the escape prefix itself makes this injective.
std::string cpp_name(std::string_view name) {
    if (name.starts_with('_') || name.find("__") != std::string_view::npos || name == "runtime" ||
        name.starts_with("escaped_")) {
        constexpr std::string_view hex = "0123456789abcdef";
        std::string result = "jaqr_escaped_";
        for (unsigned char ch : name) {
            result += hex[ch >> 4U];
            result += hex[ch & 15U];
        }
        return result;
    }
    return "jaqr_" + std::string(name);
}

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

std::string quoted_literal(std::string_view value, char quote) {
    std::string result(1, quote);
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch == static_cast<unsigned char>(quote)) {
                result += '\\';
                result += static_cast<char>(ch);
            } else if (ch >= 0x20 && ch <= 0x7e) {
                result += static_cast<char>(ch);
            } else {
                // Three octal digits prevent following characters extending the escape.
                result += '\\';
                result += static_cast<char>('0' + ((unsigned int)(ch >> 6U) & 7U));
                result += static_cast<char>('0' + ((unsigned int)(ch >> 3U) & 7U));
                result += static_cast<char>('0' + (ch & 7U));
            }
        }
    }
    result += quote;
    return result;
}

} // namespace

const code_generator::state_binding *code_generator::find_binding(const std::string &name) const {
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        if (const auto found = scope->find(name); found != scope->end())
            return &found->second;
    }
    return nullptr;
}

std::string code_generator::state_value(const void *identity, const std::string &name) const {
    for (auto values = step_values.rbegin(); values != step_values.rend(); ++values) {
        if (const auto found = values->find(identity); found != values->end())
            return found->second;
    }
    return cpp_name(name);
}

// Copying a pointer would still alias the original storage and make rollback ineffective.
// Only recursively pointer-free value types can use this copy/validate/commit strategy.
bool code_generator::can_copy_state(const type_syntax &type) const {
    std::unordered_set<std::string> visiting;
    const auto check = [&](const auto &self, const type_syntax &candidate) -> bool {
        if (const auto *primitive = dynamic_cast<const primitive_type_syntax *>(&candidate))
            return primitive->type != primitive_type::VOID;
        if (const auto *array = dynamic_cast<const array_type_syntax *>(&candidate))
            return self(self, *array->element);
        if (const auto *named = dynamic_cast<const named_type_syntax *>(&candidate)) {
            const auto declaration = structs.find(named->name);
            if (declaration == structs.end() || !visiting.insert(named->name).second)
                return false;
            const bool copyable =
                std::all_of(declaration->second->fields.begin(), declaration->second->fields.end(),
                            [&](const auto &field) { return self(self, *field.declared_type); });
            visiting.erase(named->name);
            return copyable;
        }
        // A pointer copy would still allow writes to the original pointee.
        return false;
    };
    return check(check, type);
}

void code_generator::visit(const program &node) {
    scopes.emplace_back();
    for (const auto &declaration : node.declarations) {
        if (const auto *record = dynamic_cast<const struct_decl *>(declaration.get()))
            structs.emplace(record->name->lexeme, record);
    }
    for (const auto &declr : node.declarations) {
        declr->accept(*this);

        if (dynamic_cast<const variable_decl *>(declr.get())) {
            loc.back() += ';';
            loc.emplace_back();
        }
    }
    scopes.pop_back();
}

// Expressions:
const char *operator_text(binary_op op) {
    switch (op) {
    case binary_op::LOGICAL_OR:
        return " || ";
    case binary_op::LOGICAL_AND:
        return " && ";
    case binary_op::BITWISE_OR:
        return " | ";
    case binary_op::BITWISE_AND:
        return " & ";
    case binary_op::BITWISE_XOR:
        return " ^ ";
    case binary_op::ADD:
        return " + ";
    case binary_op::SUBTRACT:
        return " - ";
    case binary_op::MULTIPLY:
        return " * ";
    case binary_op::DIVIDE:
        return " / ";
    case binary_op::EQUALS:
        return " == ";
    case binary_op::NOT_EQUALS:
        return " != ";
    case binary_op::GT:
        return " > ";
    case binary_op::GTE:
        return " >= ";
    case binary_op::LT:
        return " < ";
    case binary_op::LTE:
        return " <= ";
    case binary_op::LEFT_SHIFT:
        return " << ";
    case binary_op::RIGHT_SHIFT:
        return " >> ";
    default:
        throw std::invalid_argument("Operator requires special handling");
    }
}

void code_generator::visit(const binary_expr &node) {
    if (emit_constant(node))
        return;
    loc.back() += " (";
    switch (node.operation) {
    case binary_op::FLOOR_DIVIDE:
        loc.back() += " std::floor((double)(";
        node.left->accept(*this);
        loc.back() += ") / (double)(";
        node.right->accept(*this);
        loc.back() += "))";
        break;
    case binary_op::EXPONENT:
        loc.back() += " std::pow(";
        node.left->accept(*this);
        loc.back() += ',';
        node.right->accept(*this);
        loc.back() += ')';
        break;
    default:
        node.left->accept(*this);
        loc.back() += operator_text(node.operation);
        node.right->accept(*this);
    }
    loc.back() += " )";
}

void code_generator::visit(const unary_expr &node) {
    if (emit_constant(node))
        return;
    loc.back() += " (";
    switch (node.operation) {
    case unary_op::NEGATE:
        loc.back() += '-';
        break;
    case unary_op::DEREFERENCE:
        loc.back() += '*';
        break;
    case unary_op::ADDRESS:
        loc.back() += '&';
        break;
    case unary_op::LOGICAL_NOT:
        loc.back() += '!';
        break;
    }
    loc.back() += '(';
    node.right->accept(*this);
    loc.back() += "))";
}

void code_generator::visit(const postfix_expr &node) {
    loc.back() += '(';
    if (const auto found = builtins.find(&node); found != builtins.end())
        loc.back() +=
            found->second == builtin_function::print ? "jaqr_runtime::print" : "jaqr_runtime::scan";
    else
        node.obj->accept(*this);

    switch (node.op) {
    case postfix_op::FUNCT_CALL: {
        loc.back() += '(';
        bool first = true;
        for (const auto &a : std::get<arg_list>(node.arg)) {
            if (!first)
                loc.back() += ", ";
            first = false;
            // Only print takes a string as text; everywhere else it is a char array.
            const auto *text = dynamic_cast<const literal *>(a.get());
            if (builtins.contains(&node) && text &&
                std::holds_alternative<std::string>(text->value))
                loc.back() += quoted_literal(std::get<std::string>(text->value), '"');
            else
                a->accept(*this);
        }
        loc.back() += ')';
        break;
    }
    case postfix_op::SUBSCRIPT:
        loc.back() += '[';
        std::get<expr_ptr>(node.arg)->accept(*this);
        loc.back() += ']';
        break;
    case postfix_op::MEMBER_ACCESS:
        loc.back() += '.';
        loc.back() += cpp_name(std::get<std::string>(node.arg));
        break;
    case postfix_op::UPDATE_CALL: {
        loc.back() += '(';
        bool first = true;
        auto &args = std::get<update_arguments>(node.arg);
        for (const auto &a : args.regular) {
            if (!first)
                loc.back() += ',';
            first = false;
            a->accept(*this);
        }
        for (const auto &a : args.state) {
            if (!first)
                loc.back() += ',';
            first = false;
            // Identifier emission selects the enclosing step's working copy.
            // Validation belongs to that step, not to an individual update.
            a->accept(*this);
        }
        loc.back() += ')';
        break;
    }
    }
    loc.back() += ')';
}

void code_generator::visit(const literal &node) {
    if (const auto *text = std::get_if<std::string>(&node.value)) {
        // A char array value: std::array needs double braces, and any elements
        // left over (including the terminating zero) are zero-initialised.
        loc.back() += "{{";
        bool first = true;
        for (const char ch : *text) {
            if (!first)
                loc.back() += ',';
            first = false;
            loc.back() += quoted_literal(std::string_view(&ch, 1), '\'');
        }
        loc.back() += "}}";
        return;
    }
    loc.back() += '(';
    std::visit(
        overloaded{
            [this](std::int32_t value) { loc.back() += std::to_string(value); },
            [this](std::byte value) {
                loc.back() += "std::byte{";
                loc.back() += std::to_string(std::to_integer<unsigned int>(value));
                loc.back() += '}';
            },
            [this](const std::string &value) { loc.back() += quoted_literal(value, '"'); },
            [this](double value) { loc.back() += std::format("{:#0.17g}", value); },
            [this](char value) { loc.back() += quoted_literal(std::string_view(&value, 1), '\''); },
            [this](bool value) { loc.back() += value ? "true" : "false"; },
        },
        node.value);
    loc.back() += ')';
}

bool code_generator::emit_constant(const expr &node) {
    const auto found = optimizations.constants.find(&node);
    if (found == optimizations.constants.end())
        return false;
    const bool integer = std::holds_alternative<std::int32_t>(found->second);
    if (integer)
        loc.back() += "static_cast<std::int32_t>(";
    visit(literal(found->second));
    if (integer)
        loc.back() += ')';
    return true;
}

void code_generator::visit(const identifier &node) {
    const auto *binding = find_binding(node.lexeme);
    if (binding)
        loc.back() += state_value(binding->identity, node.lexeme);
    else
        loc.back() +=
            node.lexeme == "main" && current_impl.empty() ? "main" : cpp_name(node.lexeme);
}

void code_generator::visit(const assignment_expr &node) {
    loc.back() += '(';
    node.target->accept(*this);
    loc.back() += " = ";
    node.value->accept(*this);
    loc.back() += ')';
}

void code_generator::visit(const array_literal &node) {
    // std::array wraps a C array, so nested literals need both brace levels.
    loc.back() += "{{";
    bool first = true;
    for (const auto &arg : node.elements) {
        if (!first)
            loc.back() += ',';
        first = false;
        arg->accept(*this);
    }
    loc.back() += "}}";
}

void code_generator::visit(const struct_literal &node) {
    loc.back() += cpp_name(node.struct_type) + "{";
    bool first = true;
    for (const auto &arg : node.init_list) {
        if (!first)
            loc.back() += ',';
        loc.back() += "." + cpp_name(arg.name) + " = ";
        first = false;
        arg.val->accept(*this);
    }
    loc.back() += '}';
}

void code_generator::visit(const dynamic_alloc &node) {
    loc.back() += "(new ";
    node.element_type->accept(*this);
    loc.back() += '[';
    node.count->accept(*this);
    loc.back() += "]())";
}

void code_generator::visit(const conversion_expr &node) {
    loc.back() += "(static_cast<";
    node.target_type->accept(*this);
    loc.back() += ">(";
    node.value->accept(*this);
    loc.back() += "))";
}

void code_generator::visit(const this_expr &node) {
    loc.back() += "(*this)";
}

// Statements
void code_generator::visit(const block_stmt &node) {
    scopes.emplace_back();
    const auto invariant_count = active_invariants.size();
    loc.back() += '{';
    const auto body_start = loc.size();
    loc.emplace_back();
    for (const auto &stmt : node.statements) {
        if (!optimizations.omitted.contains(stmt.get()))
            stmt->accept(*this);
    }
    // Indent body lines, leaving the fresh final line for the closing brace.
    for (auto i = body_start; i < loc.size(); ++i) {
        if (!loc[i].empty())
            loc[i].insert(0, 4, ' ');
    }
    loc.back() += '}';
    loc.emplace_back();
    active_invariants.resize(invariant_count);
    scopes.pop_back();
}

void code_generator::visit(const expr_stmt &node) {
    node.expression->accept(*this);
    loc.back() += ';';
    loc.emplace_back();
}

void code_generator::visit(const decl_stmt &node) {
    node.declaration->accept(*this);
    if (dynamic_cast<const invariant_decl *>(node.declaration.get()))
        return; // The invariant visitor emits its own terminator and fresh line.
    loc.back() += ';';
    loc.emplace_back();
}

void code_generator::visit(const return_stmt &node) {
    if (!step_values.empty())
        throw std::invalid_argument("A return inside a step would bypass invariant validation");
    loc.back() += "return";
    if (node.value) {
        loc.back() += ' ';
        node.value->accept(*this);
    }
    loc.back() += ';';
    loc.emplace_back();
}

void code_generator::visit(const if_stmt &node) {
    bool first = true;
    for (const auto &branch : node.branches) {
        const auto constant = optimizations.boolean(branch.condition.get());
        if (constant == false)
            continue;
        if (constant == true) {
            if (!first)
                loc.back() += "else ";
            branch.body->accept(*this);
            return;
        }
        loc.back() += first ? "if (" : "else if (";
        branch.condition->accept(*this);
        loc.back() += ") ";
        branch.body->accept(*this);
        first = false;
    }
    if (node.else_body) {
        if (!first)
            loc.back() += "else ";
        node.else_body->accept(*this);
    }
}

void code_generator::visit(const while_stmt &node) {
    loc.back() += "while (";
    node.condition->accept(*this);
    loc.back() += ") ";
    node.body->accept(*this);
}

void code_generator::visit(const for_stmt &node) {
    scopes.emplace_back();
    if (const auto found = optimizations.unrolled.find(&node);
        found != optimizations.unrolled.end()) {
        loc.back() += '{';
        const auto start = loc.size();
        loc.emplace_back();
        if (node.initializer) {
            node.initializer->accept(*this);
            loc.back() += ';';
            loc.emplace_back();
        }
        for (std::size_t i = 0; i < found->second; ++i) {
            node.body->accept(*this);
            node.increment->accept(*this);
            loc.back() += ';';
            loc.emplace_back();
        }
        for (auto i = start; i < loc.size(); ++i)
            if (!loc[i].empty())
                loc[i].insert(0, 4, ' ');
        loc.back() += '}';
        loc.emplace_back();
        scopes.pop_back();
        return;
    }
    loc.back() += "for (";
    if (node.initializer)
        node.initializer->accept(*this);
    loc.back() += "; ";
    if (node.condition)
        node.condition->accept(*this);
    loc.back() += "; ";
    if (node.increment)
        node.increment->accept(*this);
    loc.back() += ") ";
    node.body->accept(*this);
    scopes.pop_back();
}

void code_generator::visit(const step_stmt &node) {
    if (!step_values.empty() || in_update_function)
        throw std::invalid_argument("Steps cannot be nested or declared inside update functions");

    // Copy visible state once, so every update in this step shares the same values.
    // Sorting keeps generated names stable despite unordered scope maps.
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
        for (const auto &[name, binding] : *scope) {
            if (seen.insert(name).second && binding.state)
                names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());

    const auto prefix = "jaqrInternal_step_" + std::to_string(next_step++) + "_";
    std::unordered_map<const void *, std::string> values;
    std::vector<std::pair<std::string, std::string>> commits;
    loc.back() += '{';
    const auto start = loc.size();
    loc.emplace_back("bool " + prefix + "accepted = false;");
    loc.emplace_back("{");
    const auto body_start = loc.size();
    loc.emplace_back();

    for (const auto &name : names) {
        const auto &binding = *find_binding(name);
        const auto suffix = std::to_string(values.size());
        const auto target = std::format("{}target_{}", prefix, suffix);
        loc.back() += std::format("auto &{} = {};", target, cpp_name(name));
        loc.emplace_back();
        if (binding.immutable) {
            values.emplace(binding.identity, target);
            continue;
        }
        if (!binding.type || !can_copy_state(*binding.type))
            throw std::invalid_argument("Step state must have a value type without pointers: " +
                                        name);
        const auto copy = std::format("{}value_{}", prefix, suffix);
        loc.back() += std::format("auto {} = {};", copy, target);
        // A throwing assignment could leave only some originals committed after validation.
        loc.emplace_back("static_assert(std::is_nothrow_copy_assignable_v<decltype(" + copy +
                         ")>);");
        loc.emplace_back();
        values.emplace(binding.identity, copy);
        commits.emplace_back(target, copy);
    }

    // Redirect the whole body to candidates. Updates share them, and invariants run only
    // after the body finishes, so intermediate states need not satisfy the invariants.
    step_values.push_back(std::move(values));
    scopes.emplace_back();
    step_scope_depth = scopes.size();
    const auto invariant_count = active_invariants.size();
    for (const auto &statement : node.body->statements)
        statement->accept(*this);

    loc.back() += prefix + "accepted = true;";
    loc.emplace_back();
    for (const auto *invariant : active_invariants) {
        loc.back() += prefix + "accepted = " + invariant_names.at(invariant) + "(";
        bool first = true;
        for (const auto *input : invariant->state_vars) {
            if (!first)
                loc.back() += ", ";
            first = false;
            const auto value = step_values.back().find(input);
            if (value != step_values.back().end()) {
                loc.back() += value->second;
            } else {
                const auto *binding = find_binding(input->name->lexeme);
                if (!binding || binding->identity != input)
                    throw std::invalid_argument("Invariant input is hidden or out of scope: " +
                                                input->name->lexeme);
                loc.back() += cpp_name(input->name->lexeme);
            }
        }
        // Put the call first: evaluate every invariant even after one has failed.
        loc.back() += ") && " + prefix + "accepted;";
        loc.emplace_back();
    }
    loc.back() += "if (" + prefix + "accepted) {";
    loc.emplace_back();
    for (const auto &[target, copy] : commits) {
        loc.back() += std::format("    {} = {};", target, copy);
        loc.emplace_back();
    }
    loc.back() += '}';
    loc.emplace_back();
    for (auto i = body_start; i < loc.size(); ++i) {
        if (!loc[i].empty())
            loc[i].insert(0, 4, ' ');
    }
    loc.back() += '}';
    loc.emplace_back();

    active_invariants.resize(invariant_count);
    scopes.pop_back();
    // End redirection before the reject body: it observes the unchanged original state.
    // Without a reject body, failure simply discards the candidates and continues.
    step_values.pop_back();
    step_scope_depth = 0;
    if (node.reject_body) {
        loc.back() += "if (!" + prefix + "accepted) ";
        node.reject_body->accept(*this);
    }
    for (auto i = start; i < loc.size(); ++i) {
        if (!loc[i].empty())
            loc[i].insert(0, 4, ' ');
    }
    loc.back() += '}';
    loc.emplace_back();
}

// Declarations
void code_generator::visit(const variable_decl &node) {
    // Emit a reusable declaration fragment: the enclosing statement, global declaration
    // or for header owns its separator. Trailing const makes pointer bindings T* const.
    node.declared_type->accept(*this);
    if (node.immutable)
        loc.back() += " const";
    loc.back() += " " + cpp_name(node.name->lexeme);

    if (node.initializer) {
        loc.back() += " = ";
        node.initializer->accept(*this);
    }
    if (!scopes.empty())
        scopes.back().insert_or_assign(node.name->lexeme,
                                       state_binding{&node, node.declared_type.get(),
                                                     node.storage == variable_storage::STATE,
                                                     node.immutable});
}

void code_generator::visit(const function_decl &node) {
    // Update calls and definitions share an ABI: ordinary arguments by value first,
    // followed by state arguments by reference. The surrounding step owns validation.
    in_update_function = node.kind == function_kind::UPDATE;
    scopes.emplace_back();
    if (node.name->lexeme == "main" && current_impl.empty()) {
        loc.back() += "int";
    } else if (node.kind == function_kind::UPDATE) {
        loc.back() += "void";
    } else if (node.return_type) {
        node.return_type->accept(*this);
    } else {
        throw std::invalid_argument("Ordinary function requires a return type");
    }
    loc.back() += ' ';
    if (!current_impl.empty())
        loc.back() += cpp_name(current_impl) + "::";
    loc.back() +=
        node.name->lexeme == "main" && current_impl.empty() ? "main" : cpp_name(node.name->lexeme);
    loc.back() += '(';

    bool first = true;
    for (const auto &param : node.ordinary_parameters) {
        if (!first) {
            loc.back() += ", ";
        }
        first = false;

        param.declared_type->accept(*this);
        loc.back() += " " + cpp_name(param.name);
        scopes.back().emplace(param.name,
                              state_binding{&param, param.declared_type.get(), false, false});
    }

    for (const auto &param : node.state_parameters) {
        if (!first) {
            loc.back() += ", ";
        }
        first = false;

        param.declared_type->accept(*this);
        loc.back() += " &" + cpp_name(param.name);
        scopes.back().emplace(param.name,
                              state_binding{&param, param.declared_type.get(), true, false});
    }

    loc.back() += ") ";
    node.body->accept(*this);
    scopes.pop_back();
    in_update_function = false;
}

void code_generator::visit(const struct_decl &node) {
    loc.back() += "struct " + cpp_name(node.name->lexeme) + " {";
    loc.emplace_back();
    for (const auto &field : node.fields) {
        loc.back() += "    ";
        field.declared_type->accept(*this);
        loc.back() += " " + cpp_name(field.name) + ";";
        loc.emplace_back();
    }

    // function declarations (no body) to go here:
    const auto &fn_impls = impls[node.name->lexeme];
    for (const auto &fn : fn_impls) {
        if (fn->kind == function_kind::UPDATE)
            loc.back() += "void";
        else
            fn->return_type->accept(*this);

        loc.back() += " " + cpp_name(fn->name->lexeme) + "(";

        bool first = true;
        for (const auto &param : fn->ordinary_parameters) {
            if (!first)
                loc.back() += ", ";
            first = false;

            param.declared_type->accept(*this);
            loc.back() += " " + cpp_name(param.name);
        }

        for (const auto &param : fn->state_parameters) {
            if (!first)
                loc.back() += ", ";
            first = false;

            param.declared_type->accept(*this);
            loc.back() += " &" + cpp_name(param.name);
        }

        loc.back() += ");";
        loc.emplace_back();
    }
    loc.back() += "};";
    loc.emplace_back();
}

void code_generator::visit(const impl_decl &node) {
    current_impl = node.type_name->lexeme;
    for (const auto &fn : node.methods) {
        fn->accept(*this);
    }
    current_impl.clear();
}

void code_generator::visit(const invariant_decl &node) {
    // Explicit const-reference inputs make the condition reusable with candidate state;
    // capturing the original variables would validate the wrong values during a step.
    if (!step_values.empty() && scopes.size() != step_scope_depth)
        throw std::invalid_argument(
            "An invariant inside a step must be declared directly in its body");
    const auto entry =
        invariant_names
            .try_emplace(&node, "jaqrInternal_invariant_" + std::to_string(invariant_names.size()))
            .first;
    loc.back() += "const auto " + entry->second + " = [](";

    bool first = true;
    for (const auto *input : node.state_vars) {
        if (!first)
            loc.back() += ", ";
        first = false;

        input->declared_type->accept(*this);
        loc.back() += " const &" + cpp_name(input->name->lexeme);
    }

    loc.back() += ") -> bool {";
    loc.emplace_back("    return ");
    // These identifiers refer to the lambda's parameters, not step temporaries.
    scopes.emplace_back();
    for (const auto *input : node.state_vars)
        scopes.back().emplace(input->name->lexeme,
                              state_binding{nullptr, input->declared_type.get(), false, true});
    node.condition->accept(*this);
    scopes.pop_back();
    loc.back() += ';';
    loc.emplace_back("};");
    loc.emplace_back();
    active_invariants.push_back(&node);
}

// Type syntax
void code_generator::visit(const primitive_type_syntax &node) {
    switch (node.type) {
    case primitive_type::VOID:
        loc.back() += "void";
        break;
    case primitive_type::INT:
        loc.back() += "std::int32_t";
        break;
    case primitive_type::FLOAT:
        loc.back() += "double";
        break;
    case primitive_type::CHAR:
        loc.back() += "char";
        break;
    case primitive_type::BOOL:
        loc.back() += "bool";
        break;
    case primitive_type::BYTE:
        loc.back() += "std::byte";
        break;
    }
}

void code_generator::visit(const named_type_syntax &node) {
    loc.back() += cpp_name(node.name);
}

void code_generator::visit(const pointer_type_syntax &node) {
    node.pointee->accept(*this);
    loc.back() += '*';
}

void code_generator::visit(const array_type_syntax &node) {
    loc.back() += "std::array<";
    node.element->accept(*this);
    loc.back() += ", ";
    node.size->accept(*this);
    loc.back() += '>';
}

std::vector<std::string> code_generator::gen_code(const program &node,
                                                  optimization_options options) {
    builtins = resolve_builtins(node);
    optimizations = optimize(node, options);
    loc.clear();
    invariant_names.clear();
    scopes.clear();
    step_values.clear();
    active_invariants.clear();
    structs.clear();
    current_impl.clear();
    next_step = 0;
    step_scope_depth = 0;
    in_update_function = false;
    loc.emplace_back("#include <cstdint>");
    loc.emplace_back("#include <cstddef>");
    loc.emplace_back("#include <cmath>");
    loc.emplace_back("#include <array>");
    loc.emplace_back("#include <type_traits>");
    if (!builtins.empty())
        loc.emplace_back("#include <jaqr/runtime/io.h>");
    loc.emplace_back();

    visit(node);
    return loc;
}
