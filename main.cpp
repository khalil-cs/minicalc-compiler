// ============================================================
//  MiniCalc Compiler  -  Compiler Construction practical
//  Supports: Addition (+), Subtraction (-), Multiplication (*)
//
//  Phases (each one really runs, in order):
//    1. Lexical analysis   : characters  -> tokens
//    2. Syntax analysis    : tokens      -> parse tree (AST)
//    3. Semantic analysis  : checks the AST (e.g. numeric overflow)
//    4. Intermediate code  : AST         -> three-address code
//    5. Code generation    : AST         -> stack-machine (assembly-like) code
//    6. Execution          : a small virtual machine runs that code -> result
//
//  Grammar (no ambiguity, no consecutive operators):
//    expression -> ['-'] term (('+' | '-') term)*
//    term       -> factor ('*' factor)*
//    factor     -> NUMBER | '(' expression ')'
//
//  The optional leading '-' is the ONLY unary operator, and it may
//  appear only at the start of an expression or right after '('.
//  So  -5 + 2  and  (-5) * 2  are valid,
//      2-+5, 2--5, 2*-3, 5 + * 2  are syntax errors.
// ============================================================
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

// ---------- error type: tells which phase failed ----------
struct CompileError : runtime_error {
    CompileError(const string& phase, const string& msg)
        : runtime_error(phase + " error: " + msg) {}
};

// ====================== 1. LEXER ======================
enum class TokenType { Number, Plus, Minus, Multiply, LeftParen, RightParen, End };

struct Token {
    TokenType type;
    string lexeme;
    size_t position;  // 0-based index in the input
};

static string tokenName(TokenType t) {
    switch (t) {
        case TokenType::Number:     return "NUMBER";
        case TokenType::Plus:       return "PLUS";
        case TokenType::Minus:      return "MINUS";
        case TokenType::Multiply:   return "MULTIPLY";
        case TokenType::LeftParen:  return "LEFT_PAREN";
        case TokenType::RightParen: return "RIGHT_PAREN";
        case TokenType::End:        return "END";
    }
    return "UNKNOWN";
}

class Lexer {
    string src;
    size_t pos = 0;

public:
    explicit Lexer(const string& s) : src(s) {}

    vector<Token> tokenize() {
        vector<Token> out;
        while (pos < src.size()) {
            char c = src[pos];
            if (isspace(static_cast<unsigned char>(c))) { ++pos; continue; }

            size_t start = pos;

            if (isdigit(static_cast<unsigned char>(c)) || c == '.') {
                bool seenDot = false, seenDigit = false;
                while (pos < src.size()) {
                    char d = src[pos];
                    if (isdigit(static_cast<unsigned char>(d))) { seenDigit = true; ++pos; }
                    else if (d == '.') {
                        if (seenDot)
                            throw CompileError("Lexical", "number has two decimal points at position " +
                                                          to_string(pos) + ".");
                        seenDot = true; ++pos;
                    } else break;
                }
                if (!seenDigit)
                    throw CompileError("Lexical", "'.' at position " + to_string(start) +
                                                  " is not a valid number.");
                out.push_back({TokenType::Number, src.substr(start, pos - start), start});
                continue;
            }

            TokenType t;
            switch (c) {
                case '+': t = TokenType::Plus;       break;
                case '-': t = TokenType::Minus;      break;
                case '*': t = TokenType::Multiply;   break;
                case '(': t = TokenType::LeftParen;  break;
                case ')': t = TokenType::RightParen; break;
                default:
                    throw CompileError("Lexical", string("unsupported character '") + c +
                                                  "' at position " + to_string(pos) + ".");
            }
            out.push_back({t, string(1, c), start});
            ++pos;
        }
        out.push_back({TokenType::End, "", src.size()});
        return out;
    }
};

// ====================== 2. PARSER (builds AST) ======================
struct Node {
    enum Kind { Num, Neg, Bin } kind;
    double value = 0;       // for Num
    char op = 0;            // for Bin: + - *
    unique_ptr<Node> left;  // Neg uses left only
    unique_ptr<Node> right;
};
using NodePtr = unique_ptr<Node>;

static NodePtr makeNum(double v) {
    auto n = make_unique<Node>(); n->kind = Node::Num; n->value = v; return n;
}
static NodePtr makeNeg(NodePtr c) {
    auto n = make_unique<Node>(); n->kind = Node::Neg; n->left = move(c); return n;
}
static NodePtr makeBin(char op, NodePtr l, NodePtr r) {
    auto n = make_unique<Node>(); n->kind = Node::Bin; n->op = op;
    n->left = move(l); n->right = move(r); return n;
}

class Parser {
    vector<Token> toks;
    size_t cur = 0;

    const Token& peek() const { return toks[cur]; }

    [[noreturn]] void fail(const string& expected) const {
        const Token& t = peek();
        string found = (t.type == TokenType::End) ? "end of input" : "'" + t.lexeme + "'";
        throw CompileError("Syntax", "expected " + expected + " but found " + found +
                                     " at position " + to_string(t.position) + ".");
    }

    // expression -> ['-'] term (('+' | '-') term)*
    NodePtr expression() {
        bool negate = false;
        if (peek().type == TokenType::Minus) { ++cur; negate = true; }

        NodePtr left = term();
        if (negate) left = makeNeg(move(left));

        while (peek().type == TokenType::Plus || peek().type == TokenType::Minus) {
            char op = toks[cur++].lexeme[0];
            NodePtr right = term();
            left = makeBin(op, move(left), move(right));
        }
        return left;
    }

    // term -> factor ('*' factor)*
    NodePtr term() {
        NodePtr left = factor();
        while (peek().type == TokenType::Multiply) {
            ++cur;
            NodePtr right = factor();
            left = makeBin('*', move(left), move(right));
        }
        return left;
    }

    // factor -> NUMBER | '(' expression ')'
    NodePtr factor() {
        if (peek().type == TokenType::Number)
        {
            const string& text = toks[cur++].lexeme;
            try { return makeNum(stod(text)); }
            catch (const out_of_range&) {
                throw CompileError("Semantic", "number '" + text.substr(0, 20) + "...' is too large to represent.");
            }
        }

        if (peek().type == TokenType::LeftParen) {
            ++cur;
            NodePtr inner = expression();
            if (peek().type != TokenType::RightParen) fail("')'");
            ++cur;
            return inner;
        }
        fail("a number or '('");
    }

public:
    explicit Parser(vector<Token> t) : toks(move(t)) {}

    NodePtr parse() {
        NodePtr root = expression();
        if (peek().type != TokenType::End) fail("an operator or end of input");
        return root;
    }
};

// ====================== 3. SEMANTIC ANALYSIS ======================
// Walks the AST. Rejects numbers too large to represent and
// (during constant folding) results that overflow to infinity.
static double semanticCheck(const Node& n) {
    switch (n.kind) {
        case Node::Num:
            if (!isfinite(n.value))
                throw CompileError("Semantic", "number is too large to represent.");
            return n.value;
        case Node::Neg:
            return -semanticCheck(*n.left);
        case Node::Bin: {
            double a = semanticCheck(*n.left), b = semanticCheck(*n.right);
            double r = (n.op == '+') ? a + b : (n.op == '-') ? a - b : a * b;
            if (!isfinite(r))
                throw CompileError("Semantic", "arithmetic overflow while computing the result.");
            return r;
        }
    }
    return 0;
}

// ====================== 4. INTERMEDIATE CODE (3-address) ======================
struct Instr {
    string op;     // "+", "-", "*", "neg", "load"
    string a, b;   // operands (numbers or temps)
    string dest;   // temp name
};

class CodeGen {
    vector<Instr> code;
    int temps = 0;
    string newTemp() { return "t" + to_string(++temps); }

    static string fmt(double v) {
        ostringstream o; o << setprecision(12) << v; return o.str();
    }

    // returns the name (number literal or temp) holding the value
    string gen(const Node& n) {
        switch (n.kind) {
            case Node::Num: return fmt(n.value);
            case Node::Neg: {
                string x = gen(*n.left);
                string t = newTemp();
                code.push_back({"neg", x, "", t});
                return t;
            }
            case Node::Bin: {
                string x = gen(*n.left);
                string y = gen(*n.right);
                string t = newTemp();
                code.push_back({string(1, n.op), x, y, t});
                return t;
            }
        }
        return "";
    }

public:
    string result;  // where the final value lives
    vector<Instr> generate(const Node& root) {
        result = gen(root);
        return code;
    }
};

// ====================== 5. CODE GENERATION (stack machine) ======================
// Target language: PUSH <n>, ADD, SUB, MUL, NEG, HALT
// The tree is walked in post-order: operands first, then the operator.
struct Asm {
    string op;       // PUSH, ADD, SUB, MUL, NEG, HALT
    double arg = 0;  // only for PUSH
};

static void emit(const Node& n, vector<Asm>& out) {
    switch (n.kind) {
        case Node::Num: out.push_back({"PUSH", n.value}); break;
        case Node::Neg: emit(*n.left, out); out.push_back({"NEG", 0}); break;
        case Node::Bin:
            emit(*n.left, out);
            emit(*n.right, out);
            out.push_back({n.op == '+' ? "ADD" : n.op == '-' ? "SUB" : "MUL", 0});
            break;
    }
}

static vector<Asm> generateAsm(const Node& root) {
    vector<Asm> prog;
    emit(root, prog);
    prog.push_back({"HALT", 0});
    return prog;
}

// ====================== 6. EXECUTION (virtual machine) ======================
// Returns the result; if 'trace' is true, prints the stack after every step.
static double runVM(const vector<Asm>& prog, bool trace) {
    vector<double> stack;
    auto printStack = [&]() {
        cout << "[";
        for (size_t i = 0; i < stack.size(); ++i) cout << (i ? ", " : "") << setprecision(12) << stack[i];
        cout << "]";
    };
    for (const Asm& ins : prog) {
        if (ins.op == "HALT") break;
        if (ins.op == "PUSH") stack.push_back(ins.arg);
        else if (ins.op == "NEG") { stack.back() = -stack.back(); }
        else {
            double b = stack.back(); stack.pop_back();
            double a = stack.back(); stack.pop_back();
            stack.push_back(ins.op == "ADD" ? a + b : ins.op == "SUB" ? a - b : a * b);
        }
        if (trace) {
            cout << "  " << left << setw(6) << ins.op;
            if (ins.op == "PUSH") cout << left << setw(10) << setprecision(12) << ins.arg;
            else cout << setw(10) << "";
            cout << " stack = "; printStack(); cout << '\n';
        }
    }
    return stack.back();
}

// ====================== display helpers ======================
static void showTokens(const vector<Token>& toks) {
    cout << left << setw(6) << "POS" << setw(14) << "TOKEN" << "LEXEME\n";
    cout << string(32, '-') << '\n';
    for (const Token& t : toks) {
        if (t.type == TokenType::End) break;
        cout << left << setw(6) << t.position << setw(14) << tokenName(t.type) << t.lexeme << '\n';
    }
}

static string toInfix(const Node& n) {
    switch (n.kind) {
        case Node::Num: { ostringstream o; o << setprecision(12) << n.value; return o.str(); }
        case Node::Neg: return "(-" + toInfix(*n.left) + ")";
        case Node::Bin: return "(" + toInfix(*n.left) + " " + n.op + " " + toInfix(*n.right) + ")";
    }
    return "";
}

static void showTree(const Node& n, const string& prefix = "", bool last = true, bool root = true) {
    string label;
    if (n.kind == Node::Num) { ostringstream o; o << setprecision(12) << n.value; label = o.str(); }
    else if (n.kind == Node::Neg) label = "neg";
    else label = string(1, n.op);

    cout << prefix << (root ? "" : (last ? "`-- " : "|-- ")) << label << '\n';
    string child = prefix + (root ? "" : (last ? "    " : "|   "));
    if (n.kind == Node::Neg) showTree(*n.left, child, true, false);
    if (n.kind == Node::Bin) {
        showTree(*n.left, child, false, false);
        showTree(*n.right, child, true, false);
    }
}

static void showCode(const vector<Instr>& code, const string& result) {
    if (code.empty()) { cout << "(no instructions)  result = " << result << '\n'; return; }
    for (const Instr& i : code) {
        if (i.op == "neg") cout << i.dest << " = -" << i.a << '\n';
        else cout << i.dest << " = " << i.a << ' ' << i.op << ' ' << i.b << '\n';
    }
}

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Runs the whole pipeline on one expression.
// verbose = print every phase. Returns true on success and stores the result.
static bool compile(const string& line, bool verbose, double& result, string& error) {
    try {
        if (verbose) cout << "\n[1] LEXICAL ANALYSIS\n";
        vector<Token> tokens = Lexer(line).tokenize();
        if (verbose) showTokens(tokens);

        if (verbose) cout << "\n[2] SYNTAX ANALYSIS\n";
        NodePtr tree = Parser(tokens).parse();
        if (verbose) {
            cout << "Syntax: VALID\nParse tree:\n";
            showTree(*tree);
            cout << "Fully parenthesised: " << toInfix(*tree) << '\n';
        }

        if (verbose) cout << "\n[3] SEMANTIC ANALYSIS\n";
        double expected = semanticCheck(*tree);
        if (verbose) cout << "Semantics: VALID (all numbers representable, no overflow)\n";

        if (verbose) cout << "\n[4] INTERMEDIATE CODE (three-address)\n";
        CodeGen gen;
        vector<Instr> tac = gen.generate(*tree);
        if (verbose) showCode(tac, gen.result);

        if (verbose) cout << "\n[5] CODE GENERATION (stack machine)\n";
        vector<Asm> prog = generateAsm(*tree);
        if (verbose) {
            for (const Asm& i : prog) {
                cout << "  " << i.op;
                if (i.op == "PUSH") cout << ' ' << setprecision(12) << i.arg;
                cout << '\n';
            }
            cout << "\n[6] EXECUTION (virtual machine)\n";
        }
        result = runVM(prog, verbose);

        // Safety net: the machine code must agree with the tree evaluation.
        if (result != expected)
            throw CompileError("Internal", "generated code disagrees with the parse tree.");
        return true;
    } catch (const exception& e) {
        error = e.what();
        return false;
    }
}

static void showHelp() {
    cout << "Commands:\n"
            "  <expression>   compile and run it, e.g.  10 + 5 * 2\n"
            "  brief          show only the result (and errors)\n"
            "  full           show every compiler phase (default)\n"
            "  test           run the built-in test suite\n"
            "  help           show this message\n"
            "  exit           quit\n"
            "Language: numbers, + - *, parentheses, and a leading '-' (e.g. -5 + 2).\n\n";
}

// Built-in self test: prints PASS/FAIL for each case.
static void runTests() {
    struct Case { string in; bool ok; double value; };
    vector<Case> cases = {
        {"10 + 5", true, 15},        {"10 - 5", true, 5},        {"10 * 5", true, 50},
        {"10 + 5 * 2", true, 20},    {"(10 + 5) * 2", true, 30}, {"20 - 6 * 2", true, 8},
        {"2 - 5 - 3", true, -6},     {"-4 * 3 + 2", true, -10},  {"-5 + 2", true, -3},
        {"(-5) * 2", true, -10},     {"-(2 + 3)", true, -5},     {"2.5 * 4", true, 10},
        {"2*(3+(4-1))*2", true, 24}, {"   7   *   6  ", true, 42},
        {"2-+5", false, 0},          {"2--5", false, 0},         {"2*-3", false, 0},
        {"10 + * 5", false, 0},      {"10 +", false, 0},         {"(3 + 2", false, 0},
        {"3 + 2)", false, 0},        {"2 3", false, 0},          {"()", false, 0},
        {"8 / 2", false, 0},         {"abc", false, 0},          {"1.2.3", false, 0},
        {"+5", false, 0},
    };
    int passed = 0;
    for (const Case& c : cases) {
        double r = 0; string err;
        bool ok = compile(c.in, false, r, err);
        bool pass = (ok == c.ok) && (!ok || fabs(r - c.value) < 1e-9);
        passed += pass;
        cout << (pass ? "PASS  " : "FAIL  ") << left << setw(18) << ("\"" + c.in + "\"");
        if (ok) cout << "= " << setprecision(12) << r << '\n';
        else cout << "rejected: " << err << '\n';
    }
    cout << "\n" << passed << " / " << cases.size() << " tests passed.\n\n";
}

int main(int argc, char** argv) {
    // Command-line mode:  minicalc "10 + 5 * 2"
    if (argc > 1) {
        string expr;
        for (int i = 1; i < argc; ++i) expr += string(i > 1 ? " " : "") + argv[i];
        double r = 0; string err;
        if (compile(expr, true, r, err)) { cout << "Result: " << setprecision(12) << r << '\n'; return 0; }
        cout << "\nCOMPILATION FAILED\n" << err << '\n';
        return 1;
    }

    cout << "========================================\n";
    cout << "          MINICALC COMPILER\n";
    cout << "   Addition | Subtraction | Multiplication\n";
    cout << "========================================\n";
    cout << "Type an expression, 'help' for commands, or 'exit' to quit.\n";
    cout << "Examples: 10 + 5 * 2   |   (10 - 3) * 4   |   -5 + 2\n\n";

    bool verbose = true;
    string line;
    while (true) {
        cout << "MiniCalc> " << flush;
        if (!getline(cin, line)) break;
        line = trim(line);

        if (line == "exit" || line == "EXIT" || line == "Exit" || line == "quit") {
            cout << "Compiler closed. Goodbye!\n";
            break;
        }
        if (line.empty()) { cout << "Please enter an expression.\n\n"; continue; }
        if (line == "help") { showHelp(); continue; }
        if (line == "test") { runTests(); continue; }
        if (line == "brief") { verbose = false; cout << "Brief mode.\n\n"; continue; }
        if (line == "full")  { verbose = true;  cout << "Full mode (all phases).\n\n"; continue; }

        double result = 0; string err;
        if (compile(line, verbose, result, err)) {
            cout << (verbose ? "\nResult: " : "Result: ") << setprecision(12) << result << '\n';
        } else {
            cout << "\nCOMPILATION FAILED\n" << err << '\n';
        }
        cout << "\n----------------------------------------\n\n";
    }
    return 0;
}
