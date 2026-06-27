// regex_engine.hpp
//
// A regex engine built from scratch:
//   pattern string -> AST (recursive-descent parser)
//                   -> NFA (Thompson's construction)
//                   -> DFA (subset construction / powerset construction)
//                   -> minimized DFA (partition refinement, Moore's algorithm)
//
// Supported syntax:
//   literals          a, b, 5, ...
//   concatenation     ab
//   alternation       a|b
//   Kleene star       a*
//   plus              a+
//   optional          a?
//   grouping          (a|b)c
//   wildcard          .            (any byte except '\n')
//   character class   [abc] [a-z] [^a-z0-9]
//   shorthand classes \d \D \w \W \s \S
//   escapes           \. \* \+ \? \( \) \[ \] \| \\ \n \t \r
//
// Author: written for educational / demonstration purposes.

#ifndef REGEX_ENGINE_HPP
#define REGEX_ENGINE_HPP

#include <array>
#include <algorithm>
#include <bitset>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace rgx {

// =============================================================================
// 1. AST  (Abstract Syntax Tree)
// =============================================================================

enum class NodeType {
    Epsilon,   // matches empty string (used for `()`  or fully-empty pattern)
    Char,      // a single literal character
    Any,       // '.'  (any character except newline)
    Class,     // [abc] / [^abc] / \d / \w / \s ...
    Concat,    // left then right
    Union,     // left | right
    Star,      // left*
    Plus,      // left+
    Optional,  // left?
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Node {
    NodeType type;
    char ch = 0;                 // valid when type == Char
    std::bitset<256> classSet;   // valid when type == Class / Any
    NodePtr left, right;         // children (right unused for unary ops)

    explicit Node(NodeType t) : type(t) {}
};

inline NodePtr makeEpsilon() { return std::make_shared<Node>(NodeType::Epsilon); }
inline NodePtr makeChar(char c) {
    auto n = std::make_shared<Node>(NodeType::Char);
    n->ch = c;
    return n;
}
inline NodePtr makeAny() {
    auto n = std::make_shared<Node>(NodeType::Any);
    for (int c = 0; c < 256; ++c) {
        if (c != '\n') n->classSet.set(static_cast<size_t>(c));
    }
    return n;
}
inline NodePtr makeClass(const std::bitset<256>& set) {
    auto n = std::make_shared<Node>(NodeType::Class);
    n->classSet = set;
    return n;
}
inline NodePtr makeConcat(NodePtr l, NodePtr r) {
    auto n = std::make_shared<Node>(NodeType::Concat);
    n->left = std::move(l);
    n->right = std::move(r);
    return n;
}
inline NodePtr makeUnion(NodePtr l, NodePtr r) {
    auto n = std::make_shared<Node>(NodeType::Union);
    n->left = std::move(l);
    n->right = std::move(r);
    return n;
}
inline NodePtr makeStar(NodePtr l) {
    auto n = std::make_shared<Node>(NodeType::Star);
    n->left = std::move(l);
    return n;
}
inline NodePtr makePlus(NodePtr l) {
    auto n = std::make_shared<Node>(NodeType::Plus);
    n->left = std::move(l);
    return n;
}
inline NodePtr makeOptional(NodePtr l) {
    auto n = std::make_shared<Node>(NodeType::Optional);
    n->left = std::move(l);
    return n;
}

// =============================================================================
// 2. Parser   (recursive descent, hand-written, no external dependencies)
//
// Grammar (lowest to highest precedence):
//
//   union     := concat ('|' concat)*
//   concat    := repeat*                      (juxtaposition = implicit AND)
//   repeat    := atom ('*' | '+' | '?')*
//   atom      := CHAR | '.' | '(' union ')' | '[' class ']' | '\' escape
//
// =============================================================================

class RegexSyntaxError : public std::runtime_error {
public:
    explicit RegexSyntaxError(const std::string& msg) : std::runtime_error(msg) {}
};

class Parser {
public:
    explicit Parser(std::string pattern) : pat_(std::move(pattern)), pos_(0) {}

    NodePtr parse() {
        NodePtr node = parseUnion();
        if (pos_ != pat_.size()) {
            error("unexpected character");
        }
        return node;
    }

private:
    std::string pat_;
    size_t pos_;

    char peek() const { return pos_ < pat_.size() ? pat_[pos_] : '\0'; }
    char peek2() const { return pos_ + 1 < pat_.size() ? pat_[pos_ + 1] : '\0'; }
    char advance() { return pat_[pos_++]; }
    bool atEnd() const { return pos_ >= pat_.size(); }
    [[noreturn]] void error(const std::string& msg) const {
        std::ostringstream oss;
        oss << "regex syntax error at position " << pos_ << ": " << msg;
        throw RegexSyntaxError(oss.str());
    }

    NodePtr parseUnion() {
        NodePtr node = parseConcat();
        while (peek() == '|') {
            advance();
            NodePtr rhs = parseConcat();
            node = makeUnion(node, rhs);
        }
        return node;
    }

    NodePtr parseConcat() {
        NodePtr node;
        while (!atEnd() && peek() != '|' && peek() != ')') {
            NodePtr atom = parseRepeat();
            node = node ? makeConcat(node, atom) : atom;
        }
        return node ? node : makeEpsilon();
    }

    NodePtr parseRepeat() {
        NodePtr atom = parseAtom();
        for (;;) {
            if (peek() == '*') { advance(); atom = makeStar(atom); }
            else if (peek() == '+') { advance(); atom = makePlus(atom); }
            else if (peek() == '?') { advance(); atom = makeOptional(atom); }
            else break;
        }
        return atom;
    }

    NodePtr parseAtom() {
        if (atEnd()) error("unexpected end of pattern");
        char c = peek();
        if (c == '(') {
            advance();
            NodePtr inner = parseUnion();
            if (peek() != ')') error("expected ')'");
            advance();
            return inner;
        }
        if (c == '[') return parseCharClass();
        if (c == '.') { advance(); return makeAny(); }
        if (c == '\\') {
            advance();
            if (atEnd()) error("dangling escape '\\'");
            char e = advance();
            return parseEscapeAtom(e);
        }
        if (c == '*' || c == '+' || c == '?' || c == ')') {
            error(std::string("unexpected metacharacter '") + c + "'");
        }
        advance();
        return makeChar(c);
    }

    // Build the bitset for a shorthand class. Returns false if `e` is not
    // a recognized shorthand (e.g. it's a plain escaped literal).
    static bool shorthandClass(char e, std::bitset<256>& set) {
        switch (e) {
            case 'd':
                for (int c = '0'; c <= '9'; ++c) set.set(c);
                return true;
            case 'D':
                for (int c = 0; c < 256; ++c) set.set(c);
                for (int c = '0'; c <= '9'; ++c) set.reset(c);
                return true;
            case 'w':
                for (int c = 'a'; c <= 'z'; ++c) set.set(c);
                for (int c = 'A'; c <= 'Z'; ++c) set.set(c);
                for (int c = '0'; c <= '9'; ++c) set.set(c);
                set.set('_');
                return true;
            case 'W': {
                std::bitset<256> w;
                shorthandClass('w', w);
                set = ~w;
                return true;
            }
            case 's':
                for (char c : {' ', '\t', '\n', '\r', '\f', '\v'}) set.set(static_cast<unsigned char>(c));
                return true;
            case 'S': {
                std::bitset<256> s;
                shorthandClass('s', s);
                set = ~s;
                return true;
            }
            default:
                return false;
        }
    }

    static char literalEscape(char e) {
        switch (e) {
            case 'n': return '\n';
            case 't': return '\t';
            case 'r': return '\r';
            case 'f': return '\f';
            case 'v': return '\v';
            case '0': return '\0';
            default: return e; // ".", "*", "(", "\\", etc. -> themselves
        }
    }

    NodePtr parseEscapeAtom(char e) {
        std::bitset<256> set;
        if (shorthandClass(e, set)) return makeClass(set);
        return makeChar(literalEscape(e));
    }

    // Parses the contents of [...] including the brackets, '^' negation and
    // ranges, and shorthand classes such as \d inside a bracket expression.
    NodePtr parseCharClass() {
        advance(); // consume '['
        bool negate = false;
        if (peek() == '^') { negate = true; advance(); }

        std::bitset<256> set;
        bool first = true;
        while (!atEnd() && (peek() != ']' || first)) {
            first = false;

            // shorthand class inside brackets, e.g. [\d_]
            if (peek() == '\\' && peek2() != '\0') {
                advance();
                char e = advance();
                std::bitset<256> shSet;
                if (shorthandClass(e, shSet)) {
                    set |= shSet;
                    continue;
                }
                char lo = literalEscape(e);
                if (peek() == '-' && peek2() != ']' && peek2() != '\0') {
                    advance();
                    char hi = advance();
                    if (hi == '\\') hi = literalEscape(advance());
                    addRange(set, lo, hi);
                } else {
                    set.set(static_cast<unsigned char>(lo));
                }
                continue;
            }

            char lo = advance();
            if (peek() == '-' && peek2() != ']' && peek2() != '\0') {
                advance(); // consume '-'
                char hi = advance();
                if (hi == '\\') hi = literalEscape(advance());
                addRange(set, lo, hi);
            } else {
                set.set(static_cast<unsigned char>(lo));
            }
        }
        if (peek() != ']') error("expected ']' to close character class");
        advance();
        if (negate) set.flip();
        return makeClass(set);
    }

    static void addRange(std::bitset<256>& set, char lo, char hi) {
        unsigned char a = static_cast<unsigned char>(lo);
        unsigned char b = static_cast<unsigned char>(hi);
        if (a > b) std::swap(a, b);
        for (int c = a; c <= b; ++c) set.set(static_cast<size_t>(c));
    }
};

// =============================================================================
// 3. NFA  (built with Thompson's construction)
// =============================================================================

struct NFAState {
    // outgoing transitions on a concrete input byte
    std::unordered_map<unsigned char, std::vector<int>> trans;
    // outgoing epsilon (no-input) transitions
    std::vector<int> epsilon;
    bool isAccept = false;
};

class NFA {
public:
    std::vector<NFAState> states;
    int start = 0;
    int accept = 0;

    int addState() {
        states.emplace_back();
        return static_cast<int>(states.size()) - 1;
    }
    void addTransition(int from, unsigned char symbol, int to) {
        states[from].trans[symbol].push_back(to);
    }
    void addEpsilon(int from, int to) {
        states[from].epsilon.push_back(to);
    }
};

// Thompson's construction: walks the AST and glues together small NFA
// "fragments" (each with exactly one dangling start and one dangling
// accept state) according to the standard rules for each operator.
class ThompsonBuilder {
public:
    NFA build(const NodePtr& root) {
        NFA nfa;
        Frag f = buildNode(root, nfa);
        nfa.start = f.start;
        nfa.accept = f.accept;
        nfa.states[f.accept].isAccept = true;
        return nfa;
    }

private:
    struct Frag { int start; int accept; };

    Frag buildNode(const NodePtr& n, NFA& nfa) {
        switch (n->type) {
            case NodeType::Epsilon: {
                int s = nfa.addState(), a = nfa.addState();
                nfa.addEpsilon(s, a);
                return {s, a};
            }
            case NodeType::Char: {
                int s = nfa.addState(), a = nfa.addState();
                nfa.addTransition(s, static_cast<unsigned char>(n->ch), a);
                return {s, a};
            }
            case NodeType::Any:
            case NodeType::Class: {
                int s = nfa.addState(), a = nfa.addState();
                for (int c = 0; c < 256; ++c) {
                    if (n->classSet.test(static_cast<size_t>(c))) {
                        nfa.addTransition(s, static_cast<unsigned char>(c), a);
                    }
                }
                return {s, a};
            }
            case NodeType::Concat: {
                Frag l = buildNode(n->left, nfa);
                Frag r = buildNode(n->right, nfa);
                nfa.addEpsilon(l.accept, r.start);
                return {l.start, r.accept};
            }
            case NodeType::Union: {
                Frag l = buildNode(n->left, nfa);
                Frag r = buildNode(n->right, nfa);
                int s = nfa.addState(), a = nfa.addState();
                nfa.addEpsilon(s, l.start);
                nfa.addEpsilon(s, r.start);
                nfa.addEpsilon(l.accept, a);
                nfa.addEpsilon(r.accept, a);
                return {s, a};
            }
            case NodeType::Star: {
                Frag in = buildNode(n->left, nfa);
                int s = nfa.addState(), a = nfa.addState();
                nfa.addEpsilon(s, in.start);
                nfa.addEpsilon(s, a);
                nfa.addEpsilon(in.accept, in.start);
                nfa.addEpsilon(in.accept, a);
                return {s, a};
            }
            case NodeType::Plus: {
                Frag in = buildNode(n->left, nfa);
                int s = nfa.addState(), a = nfa.addState();
                nfa.addEpsilon(s, in.start);
                nfa.addEpsilon(in.accept, in.start);
                nfa.addEpsilon(in.accept, a);
                return {s, a};
            }
            case NodeType::Optional: {
                Frag in = buildNode(n->left, nfa);
                int s = nfa.addState(), a = nfa.addState();
                nfa.addEpsilon(s, in.start);
                nfa.addEpsilon(s, a);
                nfa.addEpsilon(in.accept, a);
                return {s, a};
            }
        }
        throw std::logic_error("unreachable: unknown NodeType");
    }
};

// =============================================================================
// 4. DFA  (built from the NFA via subset / powerset construction)
// =============================================================================

struct DFAState {
    bool isAccept = false;
    std::array<int, 256> trans; // -1 means "no transition" (reject)
    DFAState() { trans.fill(-1); }
};

class DFA {
public:
    std::vector<DFAState> states;
    int start = 0;

    // Full-string match: every byte of s must be consumed and the
    // automaton must end on an accepting state.
    bool fullMatch(const std::string& s) const {
        if (states.empty()) return false;
        int cur = start;
        for (unsigned char c : s) {
            cur = states[cur].trans[c];
            if (cur == -1) return false;
        }
        return states[cur].isAccept;
    }

    // Leftmost-longest search: returns the [begin,end) of the first,
    // longest match found scanning left to right, or std::nullopt.
    std::optional<std::pair<size_t, size_t>> search(const std::string& s) const {
        if (states.empty()) return std::nullopt;
        for (size_t startPos = 0; startPos <= s.size(); ++startPos) {
            int cur = start;
            long bestEnd = states[cur].isAccept ? static_cast<long>(startPos) : -1;
            for (size_t i = startPos; i < s.size(); ++i) {
                cur = states[cur].trans[static_cast<unsigned char>(s[i])];
                if (cur == -1) break;
                if (states[cur].isAccept) bestEnd = static_cast<long>(i + 1);
            }
            if (bestEnd != -1) return std::make_pair(startPos, static_cast<size_t>(bestEnd));
        }
        return std::nullopt;
    }

    // Graphviz "dot" representation, handy for visualizing the automaton:
    //   dfa.toDot() > dfa.dot && dot -Tpng dfa.dot -o dfa.png
    std::string toDot() const {
        std::ostringstream oss;
        oss << "digraph DFA {\n  rankdir=LR;\n";
        for (size_t i = 0; i < states.size(); ++i) {
            oss << "  s" << i << " [shape="
                << (states[i].isAccept ? "doublecircle" : "circle") << "];\n";
        }
        oss << "  start [shape=point];\n  start -> s" << start << ";\n";
        // merge parallel edges between the same pair of states into one label
        std::map<std::pair<int, int>, std::vector<std::string>> grouped;
        for (size_t i = 0; i < states.size(); ++i) {
            for (int c = 0; c < 256; ++c) {
                int t = states[i].trans[c];
                if (t == -1) continue;
                std::string label = (c == ' ') ? "' '" : (std::isprint(c) ? std::string(1, static_cast<char>(c)) : "0x" + std::to_string(c));
                grouped[{static_cast<int>(i), t}].push_back(label);
            }
        }
        for (auto& [edge, labels] : grouped) {
            std::sort(labels.begin(), labels.end());
            if (labels.size() > 12) { // collapse big ranges for readability
                labels = {labels.front() + ".." + labels.back() + " (" + std::to_string(labels.size()) + " syms)"};
            }
            std::string joined;
            for (size_t i = 0; i < labels.size(); ++i) {
                joined += labels[i];
                if (i + 1 < labels.size()) joined += ",";
            }
            oss << "  s" << edge.first << " -> s" << edge.second << " [label=\"" << joined << "\"];\n";
        }
        oss << "}\n";
        return oss.str();
    }
};

class DFABuilder {
public:
    DFA build(const NFA& nfa) const {
        DFA dfa;
        std::map<std::set<int>, int> stateIndex;
        std::queue<std::set<int>> worklist;

        std::set<int> startSet = epsilonClosure(nfa, {nfa.start});
        stateIndex[startSet] = 0;
        dfa.states.push_back(makeDfaState(nfa, startSet));
        dfa.start = 0;
        worklist.push(startSet);

        while (!worklist.empty()) {
            std::set<int> cur = worklist.front();
            worklist.pop();
            int curId = stateIndex[cur];

            for (int c = 0; c < 256; ++c) {
                std::set<int> moved = move(nfa, cur, static_cast<unsigned char>(c));
                if (moved.empty()) continue;
                std::set<int> closure = epsilonClosure(nfa, moved);

                auto it = stateIndex.find(closure);
                int targetId;
                if (it == stateIndex.end()) {
                    targetId = static_cast<int>(dfa.states.size());
                    stateIndex[closure] = targetId;
                    dfa.states.push_back(makeDfaState(nfa, closure));
                    worklist.push(closure);
                } else {
                    targetId = it->second;
                }
                dfa.states[curId].trans[c] = targetId;
            }
        }
        return dfa;
    }

private:
    static std::set<int> epsilonClosure(const NFA& nfa, std::set<int> states) {
        std::vector<int> stack(states.begin(), states.end());
        while (!stack.empty()) {
            int s = stack.back();
            stack.pop_back();
            for (int t : nfa.states[s].epsilon) {
                if (states.insert(t).second) stack.push_back(t);
            }
        }
        return states;
    }

    static std::set<int> move(const NFA& nfa, const std::set<int>& states, unsigned char c) {
        std::set<int> result;
        for (int s : states) {
            auto it = nfa.states[s].trans.find(c);
            if (it != nfa.states[s].trans.end()) {
                for (int t : it->second) result.insert(t);
            }
        }
        return result;
    }

    static DFAState makeDfaState(const NFA& nfa, const std::set<int>& states) {
        DFAState ds;
        for (int s : states) {
            if (nfa.states[s].isAccept) { ds.isAccept = true; break; }
        }
        return ds;
    }
};

// =============================================================================
// 5. DFA minimization (Moore's algorithm / iterative partition refinement)
//
// Two states are merged iff, for every input symbol, they always lead to
// states in the same partition class -- and they agree on acceptance.
// Repeating the refinement until it stabilizes yields the coarsest such
// partition, i.e. the minimal-state DFA recognizing the same language.
// =============================================================================

inline DFA minimizeDFA(const DFA& dfa) {
    int n = static_cast<int>(dfa.states.size());
    if (n == 0) return dfa;

    std::vector<int> group(n);
    for (int i = 0; i < n; ++i) group[i] = dfa.states[i].isAccept ? 1 : 0;

    bool changed = true;
    while (changed) {
        changed = false;
        std::map<std::vector<int>, int> seen;
        std::vector<int> newGroup(n);
        int nextId = 0;

        for (int i = 0; i < n; ++i) {
            std::vector<int> sig;
            sig.reserve(257);
            sig.push_back(group[i]);
            for (int c = 0; c < 256; ++c) {
                int t = dfa.states[i].trans[c];
                sig.push_back(t == -1 ? -1 : group[t]);
            }
            auto it = seen.find(sig);
            if (it == seen.end()) {
                seen[sig] = nextId;
                newGroup[i] = nextId;
                ++nextId;
            } else {
                newGroup[i] = it->second;
            }
        }
        if (newGroup != group) changed = true;
        group = newGroup;
    }

    int numGroups = *std::max_element(group.begin(), group.end()) + 1;
    DFA result;
    result.states.resize(numGroups);
    for (int i = 0; i < n; ++i) {
        if (dfa.states[i].isAccept) result.states[group[i]].isAccept = true;
    }
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < 256; ++c) {
            int t = dfa.states[i].trans[c];
            if (t != -1) result.states[group[i]].trans[c] = group[t];
        }
    }
    result.start = group[dfa.start];
    return result;
}

// =============================================================================
// 6. Regex  -- the public-facing facade tying everything together
// =============================================================================

class Regex {
public:
    explicit Regex(const std::string& pattern, bool minimize = true) : pattern_(pattern) {
        Parser parser(pattern);
        NodePtr ast = parser.parse();

        ThompsonBuilder thompson;
        nfa_ = thompson.build(ast);

        DFABuilder dfaBuilder;
        dfa_ = dfaBuilder.build(nfa_);
        if (minimize) dfa_ = minimizeDFA(dfa_);
    }

    // Anchored match: does the *entire* string match the pattern?
    bool fullMatch(const std::string& s) const { return dfa_.fullMatch(s); }

    // Unanchored search: does the pattern occur anywhere in the string?
    bool search(const std::string& s) const { return dfa_.search(s).has_value(); }

    // Returns the [begin, end) of the first leftmost-longest match.
    std::optional<std::pair<size_t, size_t>> find(const std::string& s) const {
        return dfa_.search(s);
    }

    // Returns all non-overlapping matches, scanning left to right.
    std::vector<std::pair<size_t, size_t>> findAll(const std::string& s) const {
        std::vector<std::pair<size_t, size_t>> results;
        size_t pos = 0;
        while (pos <= s.size()) {
            std::string suffix = s.substr(pos);
            auto m = dfa_.search(suffix);
            if (!m) break;
            size_t b = pos + m->first, e = pos + m->second;
            if (e == b) { // avoid infinite loop on empty matches
                results.push_back({b, e});
                pos = b + 1;
            } else {
                results.push_back({b, e});
                pos = e;
            }
        }
        return results;
    }

    const NFA& nfa() const { return nfa_; }
    const DFA& dfa() const { return dfa_; }
    const std::string& pattern() const { return pattern_; }

    int nfaStateCount() const { return static_cast<int>(nfa_.states.size()); }
    int dfaStateCount() const { return static_cast<int>(dfa_.states.size()); }

    std::string dot() const { return dfa_.toDot(); }

private:
    std::string pattern_;
    NFA nfa_;
    DFA dfa_;
};

} // namespace rgx

#endif // REGEX_ENGINE_HPP
