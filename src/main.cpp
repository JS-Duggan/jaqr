#include <iostream>

#include "Scope.h"
#include "Symbol.h"
#include "SymbolTable.h"
#include "tests.cpp"

using namespace std;

int main() {
  std::cout << "JAQR, Silent R Language (aka Step)"<< endl;

  declarationAndLookupTest("hello");

  return 0;
}
