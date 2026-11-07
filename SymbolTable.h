#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "Scope.h"

// class with the SymbolTable's behaviours
class SymbolTable {
 private:
  // root of the scope tree, represents global scope
  Scope* root;

  // points to current scope
  Scope* currentScope;

 public:
  // SymbolTable constructor
  SymbolTable() {
    // creates the global scope
    root = new Scope("global");

    // start with working in global scope
    currentScope = root;
  }

  // need to include other functions like creating child scope when entering a
  // new block
  void enterScope(string name) {
    // creates a new scope whose parent is the current scope
    Scope* newScope = new Scope(name, currentScope);

    // add new scope to the current scope's children
    currentScope->children.push_back(newScope);

    // move into newly created scope, changes where future declarations will be
    // stored
    currentScope = newScope;
  }

  // or exiting a scope when a block is finished/closed
  // when finished processing a block, return to its parent
  void exitScope() {
    // cannot exit the global scope because it has no parent
    if (currentScope->parent != nullptr) {
      // go back to parent scope
      currentScope = currentScope->parent;
    }
  }

  // a function to check when declaring a variable, if something with that name
  // already exists and not allow shadowing if it is already declared, boolean?
  // it either is or isn't declared...
  bool alreadyDeclared(string name) {
    // start searching from current scope
    Scope* scope = currentScope;

    // continue until global scope checked
    while (scope != nullptr) {
      // check if scope contains the identifier
      if (scope->symbols.find(name) != scope->symbols.end()) {
        // identifier already exists
        return true;
      }
      // move up to the parent scope
      scope = scope->parent;
    }
    // once here, every scope in initial scope's lineage has been checked and
    // identifier has not been declared
    return false;
  }

  // one for declaring a variable that hasn't already been declared, add to
  // table if not
  bool declare(Symbol symbol, bool isGlobal = false) {
    // check if this name already exists in current scope or any of its parent
    // scopes with alreadyDeclared function above
    if (alreadyDeclared(symbol.name)) {
      // its either a duplicate declaration or shadowing and end because don't
      // need to add to symbol table
      return false;
    }

    // decide where the symbol should be stored
    Scope* destination = currentScope;

    // define keyword means the declaration belongs in the global scope
    if (isGlobal) {
      destination = root;
    }

    // insert symbol into selected scope
    destination->symbols[symbol.name] = symbol;

    return true;
  }

  // something to look up a symbol
  Symbol* lookup(string name) {
    // start searching in current scope
    Scope* scope = currentScope;

    // keep searching until root reached
    while (scope != nullptr) {
      // try to find the identifier in scope
      auto it = scope->symbols.find(name);

      // if it exists, return a pointer to its symbol
      if (it != scope->symbols.end()) {
        return &it->second;
      }
      // if it wasn't found, search the parent scope
      scope = scope->parent;
    }

    // identifier wasn't declared in any parent or current scopes
    return nullptr;
  }

  // a function to lookup a symbol to find where it has been used before, go to
  // symbol in table and find in other section of struct (linesUsedIn)?
};

#endif