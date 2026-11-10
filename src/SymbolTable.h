#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <string>
#include <vector>

#include "Scope.h"
#include "Symbol.h"

using namespace std;

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

  // create child scope when entering a new block
  void enterScope(string name) {
    // creates a new scope whose parent is the current scope
    Scope* newScope = new Scope(name, currentScope);

    // add new scope to the current scope's children
    currentScope->children.push_back(newScope);

    // move into newly created scope, changes where future declarations will be
    // stored
    currentScope = newScope;
  }

  // exiting a scope when a block is finished/closed
  // when finished processing a block, return to its parent
  void exitScope() {
    // cannot exit the global scope because it has no parent
    if (currentScope->parent != nullptr) {
      // go back to parent scope
      currentScope = currentScope->parent;
    }
  }

  // check when declaring a variable, if something with that name already exists
  // and not allow shadowing if it is already declared boolean because it either
  // is or isn't declared
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

  // declaring a variable that hasn't already been declare and add to symbol
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

  // lookup a symbol in the symbol table
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

  // lookup a symbol to find where it has been used before (linesUsedIn)
  bool addLinesUsedIn(string name, int line) {
    // find symbol using the current scope and its parents
    Symbol* symbol = lookup(name);  // use lookup function declared above to
                                    // find symbol, so then can update

    // if doesn't exist, hasn't been declared
    if (symbol == nullptr) {
      return false;
    }

    // record line where the identifier was used
    symbol->linesUsedIn.push_back(line);

    return true;
  }
};

// Things i need to think about for including later/ see if other people are
// covering them in their parts: destructor to clean up dynamically allocated
// scopes A way to retrieve the global scope function signature checking using
// parameter and return types array dimensions and data structure size
// calculation Memory address assignment during code generation and where should
// these be implemented...???

#endif