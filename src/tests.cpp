#include <iostream>
#include <string>
#include <vector>

#include "SymbolTable.h"

using namespace std;

// Symbol table tests:
// basic declaration and lookup
// parent scope lookup
// shadowing
// different identifiers in nested scopes
// out of scope identifier

// Test by calling these functions one at a time in main.cpp

// create instance of class
SymbolTable StTest;

int alreadyDeclaredTest(string name) {
  // tests if a variable is already declared

  bool isDeclared = StTest.alreadyDeclared(
      name);  // idk how to get this to work so that it can access the function
              // from SymbolTable.h in this function

  // declare variable already runs this, but this way we can see what it returns
  // without changing the function's code
  if (isDeclared == true) {
    std::cout << name << "is already declared" << endl;
  } else {
    std::cout << name << "is not declared" << endl;
  }
  return 0;
};

int addToSymbolTableTest() {
  // tests variable declaration and adding to symbol table

  return 0;
};

int parentScopeLookupTest() {
  // tests parent scope lookup

  return 0;
};

int shadowingTest() {
  // tests shadowing

  return 0;
};

int nestedScopesTest() {
  // tests different identifiers in nested scopes

  return 0;
};

int outOfScopeTest() {
  // tests out of scope identifier

  return 0;
};

// int main() {
//   alreadyDeclaredTest("variable");

//   return 0;
// };
