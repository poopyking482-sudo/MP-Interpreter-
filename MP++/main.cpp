#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <memory>
#include <cstring>
#include <fstream>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ============================================================================
// 1. HARDWARE JIT ENGINE BACKEND
// ============================================================================
enum class JitOp { Add, Sub, Mul };

class JITEngine {
private:
    std::vector<uint8_t> machine_code;
    void* exec_mem = nullptr;
    size_t allocated_size = 0;

    void* allocate_executable_memory(size_t size) {
#ifdef _WIN32
        return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
        void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (ptr == MAP_FAILED) return nullptr;
        return ptr;
#endif
    }

    void free_executable_memory(void* ptr, size_t size) {
        if (!ptr) return;
#ifdef _WIN32
        VirtualFree(ptr, 0, MEM_RELEASE);
#else
        munmap(ptr, size);
#endif
    }

public:
    JITEngine() = default;
    ~JITEngine() { clear(); }

    void clear() {
        machine_code.clear();
        if (exec_mem) {
            free_executable_memory(exec_mem, allocated_size);
            exec_mem = nullptr;
            allocated_size = 0;
        }
    }

    void emit_mov_eax(int value) {
        machine_code.push_back(0xB8);
        machine_code.push_back((value >> 0) & 0xFF);
        machine_code.push_back((value >> 8) & 0xFF);
        machine_code.push_back((value >> 16) & 0xFF);
        machine_code.push_back((value >> 24) & 0xFF);
    }

    void emit_mov_ecx(int value) {
        machine_code.push_back(0xB9);
        machine_code.push_back((value >> 0) & 0xFF);
        machine_code.push_back((value >> 8) & 0xFF);
        machine_code.push_back((value >> 16) & 0xFF);
        machine_code.push_back((value >> 24) & 0xFF);
    }

    void emit_add_eax_ecx()  { machine_code.push_back(0x01); machine_code.push_back(0xC8); }
    void emit_sub_eax_ecx()  { machine_code.push_back(0x29); machine_code.push_back(0xC8); }
    void emit_imul_eax_ecx() { machine_code.push_back(0x0F); machine_code.push_back(0xAF); machine_code.push_back(0xC1); }
    void emit_ret()          { machine_code.push_back(0xC3); }

    int execute() {
        if (machine_code.empty()) return 0;
        allocated_size = machine_code.size();
        exec_mem = allocate_executable_memory(allocated_size);
        if (!exec_mem) return -1;

        std::memcpy(exec_mem, machine_code.data(), allocated_size);
        typedef int (*JitFunc)();
        return reinterpret_cast<JitFunc>(exec_mem)();
    }

    int compile_and_run(int lhs, int rhs, JitOp op) {
        clear();
        emit_mov_eax(lhs);
        emit_mov_ecx(rhs);
        switch (op) {
            case JitOp::Add: emit_add_eax_ecx(); break;
            case JitOp::Sub: emit_sub_eax_ecx(); break;
            case JitOp::Mul: emit_imul_eax_ecx(); break;
        }
        emit_ret();
        return execute();
    }
};

JITEngine jit;

// ============================================================================
// 2. LEXICAL TOKENIZER
// ============================================================================
enum class TokenType {
    Keyword_Auto, Keyword_Const, Keyword_If, Keyword_Else, Keyword_While,
    Identifier, Number, String,
    Plus, Minus, Star, Assign, EqualEqual, Arrow,
    OpenBrace, CloseBrace, OpenParen, CloseParen, Semicolon,
    Unknown
};

struct Token {
    TokenType type;
    std::string value;
};

std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < source.length()) {
        if (std::isspace(source[i])) { i++; continue; }
        if (source.substr(i, 2) == "//") {
            while (i < source.length() && source[i] != '\n') i++;
            continue;
        }

        if (source.substr(i, 2) == "==") { tokens.push_back({TokenType::EqualEqual, "=="}); i += 2; continue; }
        if (source.substr(i, 2) == "->") { tokens.push_back({TokenType::Arrow, "->"}); i += 2; continue; }

        if (source[i] == '=') { tokens.push_back({TokenType::Assign, "="}); i++; continue; }
        if (source[i] == '+') { tokens.push_back({TokenType::Plus, "+"}); i++; continue; }
        if (source[i] == '-') { tokens.push_back({TokenType::Minus, "-"}); i++; continue; }
        if (source[i] == '*') { tokens.push_back({TokenType::Star, "*"}); i++; continue; }
        if (source[i] == '{') { tokens.push_back({TokenType::OpenBrace, "{"}); i++; continue; }
        if (source[i] == '}') { tokens.push_back({TokenType::CloseBrace, "}"}); i++; continue; }
        if (source[i] == '(') { tokens.push_back({TokenType::OpenParen, "("}); i++; continue; }
        if (source[i] == ')') { tokens.push_back({TokenType::CloseParen, ")"}); i++; continue; }
        if (source[i] == ';') { tokens.push_back({TokenType::Semicolon, ";"}); i++; continue; }

        if (source[i] == '"' || source[i] == '\'') {
            char quote = source[i++];
            std::string str;
            while (i < source.length() && source[i] != quote) str += source[i++];
            if (i < source.length()) i++;
            tokens.push_back({TokenType::String, str});
            continue;
        }

        if (std::isdigit(source[i])) {
            std::string num;
            while (i < source.length() && std::isdigit(source[i])) num += source[i++];
            tokens.push_back({TokenType::Number, num});
            continue;
        }

        if (std::isalpha(source[i]) || source[i] == '_') {
            std::string ident;
            while (i < source.length() && (std::isalnum(source[i]) || source[i] == '_')) ident += source[i++];
            if (ident == "auto") tokens.push_back({TokenType::Keyword_Auto, ident});
            else if (ident == "const") tokens.push_back({TokenType::Keyword_Const, ident});
            else if (ident == "if") tokens.push_back({TokenType::Keyword_If, ident});
            else if (ident == "else") tokens.push_back({TokenType::Keyword_Else, ident});
            else if (ident == "while") tokens.push_back({TokenType::Keyword_While, ident});
            else tokens.push_back({TokenType::Identifier, ident});
            continue;
        }
        i++;
    }
    return tokens;
}

// ============================================================================
// 3. ENVIRONMENT AND VARIABLE SYSTEM
// ============================================================================
struct Variable {
    std::string value;
    bool is_const = false;
};

struct string_hash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
    size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); }
};

struct Environment {
    std::unordered_map<std::string, Variable, string_hash, std::equal_to<>> variables;
    std::shared_ptr<Environment> parent = nullptr;

    void declare(std::string_view name, std::string_view val, bool is_const) {
        variables[std::string(name)] = {std::string(val), is_const};
    }

    bool update(std::string_view name, std::string_view val) {
        auto it = variables.find(name);
        if (it != variables.end()) {
            if (it->second.is_const) {
                std::cerr << "✗ Error: Cannot assign modification value to const-qualified variable '" << name << "'.\n";
                return false;
            }
            it->second.value = std::string(val);
            return true;
        }
        if (parent) return parent->update(name, val);
        std::cerr << "✗ Error: Variable '" << name << "' must be explicitly defined using 'auto'.\n";
        return false;
    }

    std::string get(std::string_view name, bool& found) {
        auto it = variables.find(name);
        if (it != variables.end()) {
            found = true;
            return it->second.value;
        }
        if (parent) return parent->get(name, found);
        found = false;
        return "";
    }
};

struct InterpreterContext {
    std::shared_ptr<Environment> global_env = std::make_shared<Environment>();
    std::unordered_map<std::string, std::vector<Token>, string_hash, std::equal_to<>> functions;
};

InterpreterContext ctx;

// ============================================================================
// 4. PARSER AND INTERPRETER ENGINE
// ============================================================================
std::string resolve_token(const Token& tok, std::shared_ptr<Environment> env) {
    if (tok.type == TokenType::Identifier) {
        bool found = false;
        std::string val = env->get(tok.value, found);
        return found ? val : tok.value;
    }
    return tok.value;
}

int safe_stoi(const std::string& str) {
    try { return std::stoi(str); } catch (...) { return 0; }
}

size_t skip_cpp_block(const std::vector<Token>& tokens, size_t start) {
    size_t depth = 1;
    size_t i = start;
    while (i < tokens.size() && depth > 0) {
        if (tokens[i].type == TokenType::OpenBrace) depth++;
        else if (tokens[i].type == TokenType::CloseBrace) depth--;
        if (depth == 0) return i;
        i++;
    }
    return tokens.size();
}

bool evaluate_condition(const std::vector<Token>& tokens, size_t start, size_t end, std::shared_ptr<Environment> env) {
    if (start >= end) return false;
    // Simple lookahead evaluation engine for conditions inside paren matrices
    for (size_t i = start; i < end; ++i) {
        if (tokens[i].type == TokenType::EqualEqual) {
            std::string lhs = resolve_token(tokens[start], env);
            std::string rhs = resolve_token(tokens[i + 1], env);
            return lhs == rhs;
        }
    }
    std::string val = resolve_token(tokens[start], env);
    return (val == "true" || val == "1");
}

void execute_tokens(const std::vector<Token>& tokens, size_t& idx, size_t end, std::shared_ptr<Environment> env) {
    while (idx < end) {
        if (tokens[idx].type == TokenType::CloseBrace) {
            idx++;
            return;
        }

        // 1. Function Call Expressions or Native Commands
        if (tokens[idx].type == TokenType::Identifier) {
            std::string name = tokens[idx].value;

            if ((name == "print" || name == "println") && tokens[idx + 1].type == TokenType::OpenParen) {
                idx += 2; // skip print and '('
                std::cout << resolve_token(tokens[idx], env) << (name == "println" ? "\n" : "");
                idx += 3; // skip value, ')', and ';'
                continue;
            }

            if (name == "fetch" && tokens[idx + 1].type == TokenType::Semicolon) {
                std::cout << "■ MiniPhone Engine OS (MP++) ■\nTarget: High-Performance Modern C++ JIT Architecture\n";
                idx += 2;
                continue;
            }

            // User Defined Functions
            if (auto it = ctx.functions.find(name); it != ctx.functions.end() && tokens[idx + 1].type == TokenType::OpenParen) {
                idx += 4; // skip name, '(', ')', and '{' -> clean execution jumping
                auto local_env = std::make_shared<Environment>();
                local_env->parent = env;
                size_t func_idx = 0;
                execute_tokens(it->second, func_idx, it->second.size(), local_env);
                continue;
            }

            // Variable Assignment (Mutations)
            if (tokens[idx + 1].type == TokenType::Assign) {
                std::string var_name = tokens[idx].value;
                idx += 2; // skip identifier and '='
                
                std::string target_val;
                if (idx + 2 < end && (tokens[idx + 1].type == TokenType::Plus || tokens[idx + 1].type == TokenType::Minus || tokens[idx + 1].type == TokenType::Star)) {
                    int val1 = safe_stoi(resolve_token(tokens[idx], env));
                    int val2 = safe_stoi(resolve_token(tokens[idx + 2], env));
                    JitOp op = (tokens[idx + 1].type == TokenType::Plus) ? JitOp::Add : ((tokens[idx + 1].type == TokenType::Minus) ? JitOp::Sub : JitOp::Mul);
                    target_val = std::to_string(jit.compile_and_run(val1, val2, op));
                    idx += 3;
                } else {
                    target_val = resolve_token(tokens[idx], env);
                    idx++;
                }
                env->update(var_name, target_val);
                if (tokens[idx].type == TokenType::Semicolon) idx++;
                continue;
            }
        }

        // 2. Variable Declarations: auto/const auto declarations
        bool is_const = false;
        if (tokens[idx].type == TokenType::Keyword_Const) { is_const = true; idx++; }
        if (tokens[idx].type == TokenType::Keyword_Auto) {
            idx++; // step over auto
            
            // Function Registry: auto main() -> void { ... }
            if (tokens[idx + 1].type == TokenType::OpenParen) {
                std::string func_name = tokens[idx].value;
                idx += 5; // skip name, '(', ')', '->', type to target open brace '{'
                size_t body_start = idx + 1;
                size_t body_end = skip_cpp_block(tokens, body_start);
                
                std::vector<Token> body_tokens(tokens.begin() + body_start, tokens.begin() + body_end);
                ctx.functions[func_name] = body_tokens;
                
                idx = body_end + 1;
                continue;
            }

            // Normal Variable Definition: auto x = value;
            std::string var_name = tokens[idx].value;
            idx += 2; // step name and '='
            
            std::string var_val;
            if (idx + 2 < end && (tokens[idx + 1].type == TokenType::Plus || tokens[idx + 1].type == TokenType::Minus || tokens[idx + 1].type == TokenType::Star)) {
                int val1 = safe_stoi(resolve_token(tokens[idx], env));
                int val2 = safe_stoi(resolve_token(tokens[idx + 2], env));
                JitOp op = (tokens[idx + 1].type == TokenType::Plus) ? JitOp::Add : ((tokens[idx + 1].type == TokenType::Minus) ? JitOp::Sub : JitOp::Mul);
                var_val = std::to_string(jit.compile_and_run(val1, val2, op));
                idx += 3;
            } else {
                var_val = resolve_token(tokens[idx], env);
                idx++;
            }
            
            env->declare(var_name, var_val, is_const);
            if (tokens[idx].type == TokenType::Semicolon) idx++;
            continue;
        }

        // 3. Conditional Scopes (If Conditions)
        if (tokens[idx].type == TokenType::Keyword_If) {
            idx += 2; // step 'if' and '('
            size_t cond_end = idx;
            while (tokens[cond_end].type != TokenType::CloseParen) cond_end++;
            
            bool result = evaluate_condition(tokens, idx, cond_end, env);
            idx = cond_end + 2; // skip past ')' and '{'
            
            size_t block_end = skip_cpp_block(tokens, idx);
            if (result) {
                execute_tokens(tokens, idx, block_end, env);
            } else {
                idx = block_end + 1; // Skip block execution cleanly
            }
            continue;
        }

        // 4. While Loop Scopes
        if (tokens[idx].type == TokenType::Keyword_While) {
            idx += 2; // step 'while' and '('
            size_t cond_end = idx;
            while (tokens[cond_end].type != TokenType::CloseParen) cond_end++;
            
            size_t block_start = cond_end + 2; // past ')' and '{'
            size_t block_end = skip_cpp_block(tokens, block_start);
            
            size_t loop_count = 0;
            while (evaluate_condition(tokens, idx, cond_end, env)) {
                if (++loop_count > 100000) {
                    std::cerr << "Runtime Safetynet: Infinite loop breakout activated.\n";
                    break;
                }
                size_t run_idx = block_start;
                execute_tokens(tokens, run_idx, block_end, env);
            }
            idx = block_end + 1;
            continue;
        }

        idx++;
    }
}

// ============================================================================
// 5. DRIVER REPL TERMINAL INTERFACE
// ============================================================================
void run_repl() {
    std::cout << "=========================================================\n";
    std::cout << "   MiniPhone++ (MP++) Modern C++ Language Engine         \n";
    std::cout << "   Engine standard: C++23 Native Context / Tokenizer     \n";
    std::cout << "=========================================================\n";

    std::string program;
    std::string line;
    int brace_depth = 0;

    while (true) {
        if (brace_depth == 0) std::cout << "mp++ > ";
        else std::cout << "   ... ";

        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;

        for (char c : line) {
            if (c == '{') brace_depth++;
            if (c == '}') brace_depth--;
        }

        program += line + "\n";

        if (brace_depth == 0 && !program.empty()) {
            std::vector<Token> tokens = tokenize(program);
            size_t idx = 0;
            execute_tokens(tokens, idx, tokens.size(), ctx.global_env);
            program.clear();
        }
    }
}

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);
    run_repl();
    return 0;
}
