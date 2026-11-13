#ifndef SCOPE_H
#define SCOPE_H

#include <map>
#include <string>
#include <vector>
#include <memory>
#include <utility>

#include "Symbol.h"

// the different kinds of scopes
enum class ScopeKind {
    GLOBAL,
    FUNCTION,
    BLOCK
};


// define scope using tree, to know the relationships between symbols and which
// have been declared

struct Scope {
    // each scope has a category it belongs to
  //name identifying this scope
    string name;

    // pointer to the scope that contains current symbol
    // global scope is a nullptr, no parent
    // TODO: std::shared_ptr
    Scope *parent;

    // category of scope
    ScopeKind kind;

    // every symbol has a unique name within its scope, so using a map
    // stores all symbols declared in the scope, with the identifier as the key
    //     Symbol contains all its declaration information\
  //TODO: map of symbols would work, maybe map of unique symbol pointers has some benfiits,
    // either works
    map<string, Symbol> symbols;

    // stores pointers to all its child scopes
    // unique_ptr means this scope owns its children
    vector<unique_ptr<Scope>> children;


    // constructor for scope, taking scopeName and parentScope parameters and
    // initialising them
    Scope(std::string scopeName,
          Scope* parentScope = nullptr,
          ScopeKind scopeKind = ScopeKind::BLOCK)
        : name(std::move(scopeName)),
          kind(scopeKind),
          parent(parentScope) {
    }
};

#endif

