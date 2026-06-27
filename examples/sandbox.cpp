// sandbox.cpp -- minimal smoke test: compile a pattern, run a couple
// of matches, and print NFA/DFA state counts.
//
//   g++ -std=c++17 -O2 -I.. -o sandbox examples/sandbox.cpp
//   ./sandbox

#include <iostream>
#include "../include/regex_engine.hpp"

using rgx::Regex;

int main() {
    // 1. Immediate sanity check to ensure the binary is running
    std::cout << "[LOG] Program started successfully." << std::endl;

    std::cout << "[LOG] Compiling regex pattern..." << std::endl;
    Regex re("[A-Za-z_][A-Za-z0-9_]*");
    std::cout << "[LOG] Compilation complete!" << std::endl;

    // 2. Test matching API
    std::cout << std::boolalpha;
    std::cout << "Full Match 'hello_world': " << re.fullMatch("hello_world") << std::endl;
    std::cout << "Search '2 cats, 1 dog':   " << re.search("2 cats, 1 dog") << std::endl;

    // 3. Test state counts
    std::cout << "NFA State Count:          " << re.nfaStateCount() << std::endl;
    std::cout << "Minimized DFA States:     " << re.dfaStateCount() << std::endl;

    return 0;
}