#include "builtins.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {
struct value_info {
    const type_syntax *type = nullptr;
    bool string = false;
    bool lvalue = false;
    bool writable = false;
    bool state = false;
};
struct symbol {
    value_info value;
    const function_decl *function = nullptr;
    std::optional<builtin_function> builtin;
};

class resolver {
    const primitive_type_syntax int_type{primitive_type::INT};
    const primitive_type_syntax float_type{primitive_type::FLOAT};
    const primitive_type_syntax char_type{primitive_type::CHAR};
    const primitive_type_syntax bool_type{primitive_type::BOOL};
    const primitive_type_syntax byte_type{primitive_type::BYTE};
    const primitive_type_syntax void_type{primitive_type::VOID};
    std::vector<std::unordered_map<std::string, symbol>> scopes;
    std::unordered_map<std::string, const struct_decl *> records;
    std::unordered_map<std::string, std::unique_ptr<named_type_syntax>> record_types;
    std::unordered_map<std::string, std::unordered_map<std::string, const function_decl *>> methods;
    builtin_calls calls;
    std::unordered_map<const function_decl *, std::unordered_set<const function_decl *>> edges;
    std::unordered_set<const function_decl *> io_functions;
    std::unordered_set<const function_decl *> restricted_calls;
    const function_decl *current_function = nullptr;
    const type_syntax *receiver = nullptr;
    bool restricted = false;

    const symbol *lookup(const std::string &name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (const auto found = it->find(name); found != it->end())
                return &found->second;
        return nullptr;
    }
    static bool stream_scalar(const value_info &value) {
        const auto *type = dynamic_cast<const primitive_type_syntax *>(value.type);
        return type && (type->type == primitive_type::INT || type->type == primitive_type::FLOAT ||
                        type->type == primitive_type::CHAR || type->type == primitive_type::BOOL);
    }
    static void fail(const expr &node, const std::string &message) {
        throw std::invalid_argument("I/O at line " + std::to_string(node.source.start_line) + ": " +
                                    message);
    }
    static bool comparison(binary_op op) {
        return op == binary_op::EQUALS || op == binary_op::NOT_EQUALS || op == binary_op::GT ||
               op == binary_op::GTE || op == binary_op::LT || op == binary_op::LTE ||
               op == binary_op::LOGICAL_AND || op == binary_op::LOGICAL_OR;
    }
    value_info expression(const expr *node) {
        if (!node)
            return {};
        if (const auto *name = dynamic_cast<const identifier *>(node)) {
            const auto *binding = lookup(name->lexeme);
            return binding ? binding->value : value_info{};
        }
        if (const auto *value = dynamic_cast<const literal *>(node)) {
            switch (value->value.index()) {
            case 0:
                return {&int_type};
            case 1:
                return {&byte_type};
            case 2:
                return {nullptr, true};
            case 3:
                return {&float_type};
            case 4:
                return {&char_type};
            case 5:
                return {&bool_type};
            default:
                return {};
            }
        }
        if (const auto *binary = dynamic_cast<const binary_expr *>(node)) {
            const auto left = expression(binary->left.get());
            const auto right = expression(binary->right.get());
            if (comparison(binary->operation))
                return {&bool_type};
            // Pointer arithmetic keeps the pointer's type.
            if (binary->operation == binary_op::ADD || binary->operation == binary_op::SUBTRACT) {
                if (dynamic_cast<const pointer_type_syntax *>(left.type))
                    return {left.type};
                if (dynamic_cast<const pointer_type_syntax *>(right.type))
                    return {right.type};
            }
            const auto *a = dynamic_cast<const primitive_type_syntax *>(left.type);
            const auto *b = dynamic_cast<const primitive_type_syntax *>(right.type);
            if (!a || !b)
                return {};
            if (binary->operation == binary_op::FLOOR_DIVIDE ||
                binary->operation == binary_op::EXPONENT || a->type == primitive_type::FLOAT ||
                b->type == primitive_type::FLOAT)
                return {&float_type};
            if (a->type == primitive_type::BYTE || b->type == primitive_type::BYTE)
                return {&byte_type};
            return {&int_type};
        }
        if (const auto *unary = dynamic_cast<const unary_expr *>(node)) {
            auto operand = expression(unary->right.get());
            if (unary->operation == unary_op::LOGICAL_NOT)
                return {&bool_type};
            if (unary->operation == unary_op::DEREFERENCE) {
                if (const auto *pointer = dynamic_cast<const pointer_type_syntax *>(operand.type))
                    return {pointer->pointee.get(), false, true, true, operand.state};
                return {};
            }
            if (unary->operation == unary_op::ADDRESS)
                return {};
            operand.lvalue = false;
            operand.writable = false;
            return operand;
        }
        if (const auto *assignment = dynamic_cast<const assignment_expr *>(node)) {
            auto target = expression(assignment->target.get());
            expression(assignment->value.get());
            // scan targets must denote storage, not an expression that writes first.
            target.lvalue = false;
            target.writable = false;
            return target;
        }
        if (const auto *conversion = dynamic_cast<const conversion_expr *>(node)) {
            expression(conversion->value.get());
            return {conversion->target_type.get()};
        }
        if (const auto *postfix = dynamic_cast<const postfix_expr *>(node))
            return postfix_expression(*postfix);
        if (const auto *allocation = dynamic_cast<const dynamic_alloc *>(node))
            expression(allocation->count.get());
        if (const auto *array = dynamic_cast<const array_literal *>(node))
            for (const auto &element : array->elements)
                expression(element.get());
        if (const auto *record = dynamic_cast<const struct_literal *>(node)) {
            for (const auto &field : record->init_list)
                expression(field.val.get());
            if (const auto found = record_types.find(record->struct_type);
                found != record_types.end())
                return {found->second.get()};
        }
        if (dynamic_cast<const this_expr *>(node))
            return {receiver, false, true, true, true};
        return {};
    }
    value_info postfix_expression(const postfix_expr &node) {
        const auto object = expression(node.obj.get());
        if (node.op == postfix_op::SUBSCRIPT) {
            expression(std::get<expr_ptr>(node.arg).get());
            if (const auto *array = dynamic_cast<const array_type_syntax *>(object.type))
                return {array->element.get(), false, object.lvalue, object.writable, object.state};
            if (const auto *pointer = dynamic_cast<const pointer_type_syntax *>(object.type))
                return {pointer->pointee.get(), false, true, true, object.state};
            return {};
        }
        if (node.op == postfix_op::MEMBER_ACCESS) {
            if (const auto *named = dynamic_cast<const named_type_syntax *>(object.type)) {
                if (const auto found = records.find(named->name); found != records.end())
                    for (const auto &field : found->second->fields)
                        if (field.name == std::get<std::string>(node.arg))
                            return {field.declared_type.get(), false, object.lvalue,
                                    object.writable, object.state};
            }
            return {};
        }
        std::vector<value_info> arguments;
        if (node.op == postfix_op::FUNCT_CALL) {
            for (const auto &argument : std::get<arg_list>(node.arg))
                arguments.push_back(expression(argument.get()));
        } else {
            const auto &args = std::get<update_arguments>(node.arg);
            for (const auto &argument : args.regular)
                arguments.push_back(expression(argument.get()));
            for (const auto &argument : args.state)
                arguments.push_back(expression(argument.get()));
        }
        const symbol *callee = nullptr;
        if (const auto *name = dynamic_cast<const identifier *>(node.obj.get()))
            callee = lookup(name->lexeme);
        if (callee && callee->builtin) {
            if (node.op != postfix_op::FUNCT_CALL)
                fail(node, "print and scan require ordinary call syntax");
            const bool scanning = *callee->builtin == builtin_function::scan;
            const std::string name = scanning ? "scan" : "print";
            if (arguments.size() != 1)
                fail(node, name + " requires exactly one argument");
            if (restricted)
                fail(node, "SEM021: " + name +
                               " is not allowed in a step, update function or invariant");
            if (scanning) {
                const auto &argument = arguments.front();
                if (!argument.lvalue || !argument.writable || argument.state)
                    fail(node, "scan requires writable, non-state storage");
                if (!stream_scalar(argument))
                    fail(node, "scan supports int, float, char and bool");
            } else if (!stream_scalar(arguments.front()) && !arguments.front().string)
                fail(node, "print supports int, float, char, bool and string literals");
            calls.emplace(&node, *callee->builtin);
            if (current_function)
                io_functions.insert(current_function);
            return {scanning ? &bool_type : &void_type};
        }
        const function_decl *function = callee ? callee->function : nullptr;
        if (const auto *member = dynamic_cast<const postfix_expr *>(node.obj.get());
            member && member->op == postfix_op::MEMBER_ACCESS) {
            const auto owner = expression(member->obj.get());
            if (const auto *type = dynamic_cast<const named_type_syntax *>(owner.type)) {
                if (const auto group = methods.find(type->name); group != methods.end()) {
                    if (const auto found = group->second.find(std::get<std::string>(member->arg));
                        found != group->second.end())
                        function = found->second;
                }
            }
        }
        if (function) {
            if (current_function)
                edges[current_function].insert(function);
            if (restricted)
                restricted_calls.insert(function);
            return {function->return_type ? function->return_type.get() : &void_type};
        }
        return {};
    }
    void block(const block_stmt &node) {
        scopes.emplace_back();
        for (const auto &item : node.statements)
            statement(*item);
        scopes.pop_back();
    }
    void statement(const stmt &node) {
        if (const auto *body = dynamic_cast<const block_stmt *>(&node))
            block(*body);
        else if (const auto *item = dynamic_cast<const expr_stmt *>(&node))
            expression(item->expression.get());
        else if (const auto *item = dynamic_cast<const decl_stmt *>(&node))
            declaration(*item->declaration);
        else if (const auto *item = dynamic_cast<const return_stmt *>(&node))
            expression(item->value.get());
        else if (const auto *item = dynamic_cast<const if_stmt *>(&node)) {
            for (const auto &branch : item->branches) {
                expression(branch.condition.get());
                block(*branch.body);
            }
            if (item->else_body)
                block(*item->else_body);
        } else if (const auto *item = dynamic_cast<const while_stmt *>(&node)) {
            expression(item->condition.get());
            block(*item->body);
        } else if (const auto *item = dynamic_cast<const for_stmt *>(&node)) {
            scopes.emplace_back();
            if (item->initializer)
                declaration(*item->initializer);
            expression(item->condition.get());
            expression(item->increment.get());
            block(*item->body);
            scopes.pop_back();
        } else if (const auto *item = dynamic_cast<const step_stmt *>(&node)) {
            const bool previous = restricted;
            restricted = true;
            block(*item->body);
            restricted = previous;
            if (item->reject_body)
                block(*item->reject_body);
        }
    }
    static symbol variable_symbol(const variable_decl &variable) {
        return {{variable.declared_type.get(), false, true, !variable.immutable,
                 variable.storage == variable_storage::STATE},
                nullptr,
                {}};
    }
    void declaration(const decl &node) {
        if (const auto *variable = dynamic_cast<const variable_decl *>(&node)) {
            expression(variable->initializer.get());
            scopes.back().insert_or_assign(variable->name->lexeme, variable_symbol(*variable));
        } else if (const auto *function = dynamic_cast<const function_decl *>(&node)) {
            const auto *previous = current_function;
            const bool previous_restricted = restricted;
            current_function = function;
            restricted = function->kind == function_kind::UPDATE;
            scopes.emplace_back();
            for (const auto &param : function->ordinary_parameters)
                scopes.back().emplace(
                    param.name,
                    symbol{{param.declared_type.get(), false, true, true, false}, nullptr, {}});
            for (const auto &param : function->state_parameters)
                scopes.back().emplace(
                    param.name,
                    symbol{{param.declared_type.get(), false, true, true, true}, nullptr, {}});
            block(*function->body);
            scopes.pop_back();
            current_function = previous;
            restricted = previous_restricted;
        } else if (const auto *invariant = dynamic_cast<const invariant_decl *>(&node)) {
            const bool previous = restricted;
            restricted = true;
            expression(invariant->condition.get());
            restricted = previous;
        } else if (const auto *impl = dynamic_cast<const impl_decl *>(&node)) {
            const named_type_syntax type(impl->type_name->lexeme);
            receiver = &type;
            scopes.emplace_back();
            for (const auto &method : impl->methods)
                scopes.back().emplace(method->name->lexeme, symbol{{}, method.get(), {}});
            for (const auto &method : impl->methods)
                declaration(*method);
            scopes.pop_back();
            receiver = nullptr;
        }
    }

  public:
    builtin_calls run(const program &node) {
        scopes.emplace_back();
        scopes.back().emplace("print", symbol{{}, nullptr, builtin_function::print});
        scopes.back().emplace("scan", symbol{{}, nullptr, builtin_function::scan});
        // User declarations shadow the prelude by symbol identity, not text replacement.
        scopes.emplace_back();
        for (const auto &decl : node.declarations) {
            if (const auto *function = dynamic_cast<const function_decl *>(decl.get()))
                scopes.back().emplace(function->name->lexeme, symbol{{}, function, {}});
            else if (const auto *variable = dynamic_cast<const variable_decl *>(decl.get()))
                scopes.back().emplace(variable->name->lexeme, variable_symbol(*variable));
            else if (const auto *record = dynamic_cast<const struct_decl *>(decl.get())) {
                records.emplace(record->name->lexeme, record);
                record_types.emplace(record->name->lexeme,
                                     std::make_unique<named_type_syntax>(record->name->lexeme));
            } else if (const auto *impl = dynamic_cast<const impl_decl *>(decl.get()))
                for (const auto &method : impl->methods)
                    methods[impl->type_name->lexeme].emplace(method->name->lexeme, method.get());
        }
        for (const auto &decl : node.declarations)
            declaration(*decl);
        bool changed;
        do {
            changed = false;
            for (const auto &[caller, callees] : edges)
                for (const auto *callee : callees)
                    if (io_functions.contains(callee))
                        changed |= io_functions.insert(caller).second;
        } while (changed);
        for (const auto *function : restricted_calls)
            if (io_functions.contains(function))
                throw std::invalid_argument("SEM021: I/O is not allowed here through function " +
                                            function->name->lexeme);
        return std::move(calls);
    }
};
} // namespace

builtin_calls resolve_builtins(const program &node) {
    return resolver().run(node);
}
