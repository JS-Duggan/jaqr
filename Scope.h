#ifndef SCOPE_H
#define SCOPE_H

#include <map>
#include <string>
#include <vector>

#include "Symbol.h"

using namespace std; // TODO: probably dont use this

// define scope using tree, to know the relationships between symbols and which
// have been declared

struct Scope {
    // each scope has a category it belongs to
    // TODO: think about it
    string name;

    // pointer to the scope that contains current symbol
    // global scope is a nullptr, no parent
    // TODO: std::shared_ptr
    Scope *parent;

    // every symbol has a unique name within its scope, so using a map
    // stores all symbols declared in the scope, with the identifier as the key
    //     Symbol contains all its declaration information\
  //TODO: map of symbols would work, maybe map of unique symbol pointers has some benfiits,
    // either works
    map<string, Symbol> symbols;

    // stores pointers to all its child scopes\
  //TODO: std::unique_ptr
    vector<Scope *> children;

    // constructor for scope, taking scopeName and parentScope parameters and
    // initialising them
    Scope(string scopeName, Scope *parentScope = nullptr) {
        // TODO: std::move() maybe? or pass scope name as string_view/const & and copy
        name = scopeName;
        parent = parentScope;
    }
};

#endif

