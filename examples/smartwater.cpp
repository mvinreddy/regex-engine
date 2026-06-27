// in Graphviz "dot" format, ready to be rendered into an image:
//
//   
//   ./smartwater > smartwater-dfa.dot
//   dot -Tpng smartwater-dfa.dot -o smartwater-dfa.png
 
#include <iostream>
#include "../include/regex_engine.hpp"
 
using rgx::Regex;
 
int main() {
    // 1. Compile your regex pattern
    Regex re("W(A|B)*[0-9]+");
 
    // 2. Print the Graphviz dot syntax directly to the console
    std::cout << re.dot() << std::endl;
 
    return 0;
}