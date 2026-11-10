#ifndef SYMBOL_H
#define SYMBOL_H

#include <string>
#include <vector>

using namespace std;

// the different kinds of declarations supported in JAQR
enum class SymbolKind { LET, IMUT, STATE, FUNCTION, UPDATE_FUNCTION };

// a struct which has symbol information components
struct Symbol {
    // identifier's name
    string name;

    // what kind of delcaration it is
    // from the class above SymbolKind
    SymbolKind kind;

    // declared datatype (like int, char, float)
    // for a function it represents the return type
    // TODO: use enum
    string type;

    // size of the variable
    // TODO: nit picky but maybe mention what unit this is in the comment (bytes)
    int size = 0;

    // stores array dimensions
    // TODO: probably should be an int
    vector<int> dimensions;

    // line where the symbol was originally declared
    int declarationLine = 0;

    // every line where this symbol is used is stored here
    vector<int> linesUsedIn;

    // the memory address assigned during code generation
    //  initialised to -1, which means no address has been assigned yet
    // TODO: uint64
    int memoryAddress = -1;

    // function-specific information
    // stores the function's parameters types
    // TODO: types updated here as well
    vector<string> parameterTypes;

    // only ordinary functions have return type
    // no return value for update function
    // TODO: types updated here as well and here
    string returnType;
};

#endif
