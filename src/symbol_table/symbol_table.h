#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

template <class Variable, class Structure, class Function> class symbol_table {
    using scope = std::unordered_map<std::string, Variable>;
    std::vector<scope> scopes{1}; // chucking globals here
    std::unordered_map<std::string, Structure> structs;
    std::unordered_map<std::string, Function> functions;
    std::unordered_map<std::string, std::unordered_map<std::string, Function>> methods;

    void leave_scope() {
        scopes.pop_back();
    }

  public:
    class scope_guard {
        symbol_table *owner;
        explicit scope_guard(symbol_table &table) : owner(&table) {
        }
        friend class symbol_table;

      public:
        scope_guard(const scope_guard &) = delete;
        scope_guard &operator=(const scope_guard &) = delete;
        scope_guard(scope_guard &&other) noexcept : owner(std::exchange(other.owner, nullptr)) {
        }
        ~scope_guard() {
            if (owner)
                owner->leave_scope();
        }
    };

    [[nodiscard]] scope_guard enter_scope() {
        scopes.emplace_back();
        return scope_guard(*this);
    }

    const Variable *lookup_variable(const std::string &name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (const auto found = it->find(name); found != it->end())
                return &found->second;
        return nullptr;
    }
    std::optional<Structure> lookup_struct(const std::string &name) const {
        if (const auto found = structs.find(name); found != structs.end())
            return found->second;
        return std::nullopt;
    }
    std::optional<Function> lookup_function(const std::string &name) const {
        if (const auto found = functions.find(name); found != functions.end())
            return found->second;
        return std::nullopt;
    }
    std::optional<Function> lookup_method(const std::string &owner, const std::string &name) const {
        if (const auto group = methods.find(owner); group != methods.end())
            if (const auto found = group->second.find(name); found != group->second.end())
                return found->second;
        return std::nullopt;
    }

    bool declare_variable(std::string name, Variable symbol) {
        if (lookup_variable(name) || structs.contains(name) || functions.contains(name))
            return false;
        return scopes.back().emplace(std::move(name), std::move(symbol)).second;
    }
    bool declare_struct(std::string name, Structure symbol) {
        if (lookup_variable(name) || structs.contains(name) || functions.contains(name))
            return false;
        return structs.emplace(std::move(name), std::move(symbol)).second;
    }
    bool declare_function(std::string name, Function symbol) {
        if (lookup_variable(name) || structs.contains(name) || functions.contains(name))
            return false;
        return functions.emplace(std::move(name), std::move(symbol)).second;
    }
    bool declare_method(const std::string &owner, std::string name, Function symbol) {
        return methods[owner].emplace(std::move(name), std::move(symbol)).second;
    }
};
