// main.cpp -- demo & test driver for the from-scratch regex engine.
//
// Build:   make
// Run:     ./regex_demo                 (runs the built-in test suite)
//          ./regex_demo '<pattern>' '<string>'    (full-match a single case)

#include <iomanip>
#include <iostream>
#include "../include/regex_engine.hpp"

using rgx::Regex;

struct Case { std::string pattern, input; bool expected; };

static int runSuite() {
    std::vector<Case> cases = {
        // literals & concatenation
        {"abc", "abc", true},
        {"abc", "abd", false},
        {"abc", "ab", false},

        // alternation
        {"a|b", "a", true},
        {"a|b", "b", true},
        {"a|b", "c", false},
        {"cat|dog", "dog", true},
        {"cat|dog", "do", false},

        // Kleene star
        {"a*", "", true},
        {"a*", "aaaaa", true},
        {"a*", "aab", false},

        // plus
        {"a+", "", false},
        {"a+", "a", true},
        {"a+", "aaa", true},

        // optional
        {"colou?r", "color", true},
        {"colou?r", "colour", true},
        {"colou?r", "colouur", false},

        // grouping + precedence
        {"(ab)+", "ababab", true},
        {"(ab)+", "aba", false},
        {"(a|b)*abb", "ababb", true},
        {"(a|b)*abb", "aaababb", true},
        {"(a|b)*abb", "abbab", false},

        // wildcard
        {"a.c", "abc", true},
        {"a.c", "azc", true},
        {"a.c", "ac", false},

        // character classes
        {"[abc]+", "aabbcc", true},
        {"[abc]+", "aabbccd", false},
        {"[a-z]+", "hello", true},
        {"[a-z]+", "Hello", false},
        {"[A-Za-z]+", "Hello", true},
        {"[^abc]+", "xyz", true},
        {"[^abc]+", "xaz", false},

        // shorthand classes
        {"\\d+", "12345", true},
        {"\\d+", "12a45", false},
        {"\\w+", "var_1", true},
        {"\\s+", "   \t", true},

        // a "real" pattern: simple identifier  [A-Za-z_][A-Za-z0-9_]*
        {"[A-Za-z_][A-Za-z0-9_]*", "_count2", true},
        {"[A-Za-z_][A-Za-z0-9_]*", "2count", false},

        // a "real" pattern: simple decimal number
        {"-?[0-9]+(\\.[0-9]+)?", "3.14", true},
        {"-?[0-9]+(\\.[0-9]+)?", "-42", true},
        {"-?[0-9]+(\\.[0-9]+)?", "42.", false},

        // empty pattern / epsilon
        {"", "", true},
        {"", "a", false},
        {"()*", "", true},
    };

    int passed = 0;
    for (auto& c : cases) {
        bool ok;
        std::string err;
        try {
            Regex re(c.pattern);
            ok = (re.fullMatch(c.input) == c.expected);
        } catch (const std::exception& e) {
            ok = false;
            err = e.what();
        }
        std::cout << (ok ? "[PASS] " : "[FAIL] ")
                  << "/" << c.pattern << "/ vs \"" << c.input << "\""
                  << "  (expected " << std::boolalpha << c.expected << ")";
        if (!err.empty()) std::cout << "  -- exception: " << err;
        std::cout << "\n";
        if (ok) ++passed;
    }
    std::cout << "\n" << passed << "/" << cases.size() << " tests passed.\n";
    return passed == static_cast<int>(cases.size()) ? 0 : 1;
}

static void inspect(const std::string& pattern) {
    Regex re(pattern);
    std::cout << "Pattern:        /" << pattern << "/\n";
    std::cout << "NFA states:     " << re.nfaStateCount() << "\n";
    std::cout << "DFA states:     " << re.dfaStateCount() << " (after minimization)\n";
}

static void searchDemo() {
    Regex re("\\d+");
    std::string text = "Order #4827 shipped on day 12, total $399.";
    auto matches = re.findAll(text);
    std::cout << "Pattern /\\d+/ findAll on:\n  \"" << text << "\"\n";
    for (auto& [b, e] : matches) {
        std::cout << "  match [" << b << "," << e << ") = \"" << text.substr(b, e - b) << "\"\n";
    }
}

int main(int argc, char** argv) {
    if (argc == 3) {
        // Single ad-hoc check: ./regex_demo "<pattern>" "<string>"
        try {
            Regex re(argv[1]);
            bool m = re.fullMatch(argv[2]);
            std::cout << (m ? "MATCH" : "NO MATCH") << "\n";
            return m ? 0 : 1;
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 2;
        }
    }

    std::cout << "=== Regex engine test suite ===\n\n";
    int rc = runSuite();

    std::cout << "\n=== NFA/DFA state-count inspection ===\n";
    inspect("(a|b)*abb");
    std::cout << "\n";
    inspect("[A-Za-z_][A-Za-z0-9_]*");

    std::cout << "\n=== findAll() / unanchored search demo ===\n";
    searchDemo();

    std::cout << "\n=== Graphviz dot export demo (pattern: (a|b)*abb) ===\n";
    {
        Regex re("(a|b)*abb");
        std::cout << re.dot();
    }

    return rc;
}
