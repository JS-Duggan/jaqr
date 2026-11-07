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

  //or exiting a scope when a block is finished/closed
  //when finished processing a block, return to its parent
  void exitScope() {
    //cannot exit the global scope because it has no parent
    if (currentScope->parent != nullptr) {
      //go back to parent scope
      currentScope = currentScope->parent;
    }
  }


  // a function to check when declaring a variable, if something with that name
  // already exists and not allow shadowing and adding to table if not


  // one for if it is already declared

  
  // something to look up a symbol, which might be used in that?
  // a function to lookup a symbol to find where it has been used before, go to
  // symbol in table and find in other section of struct (linesUsedIn)?
};

#endif