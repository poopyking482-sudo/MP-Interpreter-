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
#include <fstream> // Crucial for reading downloaded files!

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// ============================================================================
// 1. HARDWARE JIT ENGINE BACKEND
// ============================================================================
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

    // Emit 'mov eax, imm32' (0xB8)
    void emit_mov_eax(int value) {
        machine_code.push_back(0xB8);
        machine_code.push_back((value >> 0) & 0xFF);
        machine_code.push_back((value >> 8) & 0xFF);
        machine_code.push_back((value >> 16) & 0xFF);
        machine_code.push_back((value >> 24) & 0xFF);
    }

    // Emit 'mov ecx, imm32' (0xB9)
    void emit_mov_ecx(int value) {
        machine_code.push_back(0xB9);
        machine_code.push_back((value >> 0) & 0xFF);
        machine_code.push_back((value >> 8) & 0xFF);
        machine_code.push_back((value >> 16) & 0xFF);
        machine_code.push_back((value >> 24) & 0xFF);
    }

    // Emit 'add eax, ecx' (0x01 0xC8)
    void emit_add_eax_ecx() {
        machine_code.push_back(0x01);
        machine_code.push_back(0xC8);
    }

    // Emit 'sub eax, ecx' (0x29 0xC8)
    void emit_sub_eax_ecx() {
        machine_code.push_back(0x29);
        machine_code.push_back(0xC8);
    }

    // Emit 'imul eax, ecx' (0x0F 0xAF 0xC1)
    void emit_imul_eax_ecx() {
        machine_code.push_back(0x0F);
        machine_code.push_back(0xAF);
        machine_code.push_back(0xC1);
    }

    // Emit 'ret' (0xC3)
    void emit_ret() {
        machine_code.push_back(0xC3);
    }

    int execute() {
        if (machine_code.empty()) return 0;

        allocated_size = machine_code.size();
        exec_mem = allocate_executable_memory(allocated_size);
        if (!exec_mem) {
            std::cerr << "✗ OS Error: Execution allocation denied.\n";
            return -1;
        }

        std::memcpy(exec_mem, machine_code.data(), allocated_size);

        typedef int (*JitFunc)();
        JitFunc run = reinterpret_cast<JitFunc>(exec_mem);
        return run();
    }
};

JITEngine jit;

// ============================================================================
// 2. PARSER AND INTERPRETER FRONTEND
// ============================================================================
struct string_hash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
    size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); }
};

struct Environment {
    std::unordered_map<std::string, std::string, string_hash, std::equal_to<>> variables;
    std::shared_ptr<Environment> parent = nullptr;

    void set(std::string_view name, std::string_view val) {
        variables[std::string(name)] = std::string(val);
    }

    std::string get(std::string_view name, bool& found) {
        auto it = variables.find(name);
        if (it != variables.end()) {
            found = true;
            return it->second;
        }
        if (parent) return parent->get(name, found);
        found = false;
        return "";
    }
};

struct InterpreterContext {
    std::shared_ptr<Environment> global_env = std::make_shared<Environment>();
    std::unordered_map<std::string, std::vector<std::string>, string_hash, std::equal_to<>> functions;
};

InterpreterContext ctx;

std::string_view trim(std::string_view str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string resolve_vars(std::string_view expression, std::shared_ptr<Environment> env) {
    std::string_view token = trim(expression);
    if (token.empty()) return "";

    if (token.length() >= 2 && 
        ((token.front() == '"' && token.back() == '"') || 
         (token.front() == '\'' && token.back() == '\''))) {
        return std::string(token.substr(1, token.length() - 2));
    }
    
    bool found = false;
    std::string val = env->get(token, found);
    if (found) return val;

    return std::string(token);
}

int safe_stoi(const std::string& str, int default_val = 0) {
    try {
        return std::stoi(str);
    } catch (...) {
        return default_val;
    }
}

bool handle_jit_math(std::string_view left_tok, std::string_view right_tok, char op, std::shared_ptr<Environment> env, int& out_result) {
    std::string lhs = resolve_vars(left_tok, env);
    std::string rhs = resolve_vars(right_tok, env);

    int val1 = safe_stoi(lhs);
    int val2 = safe_stoi(rhs);

    jit.clear();
    jit.emit_mov_eax(val1);
    jit.emit_mov_ecx(val2);

    if (op == '+') jit.emit_add_eax_ecx();
    else if (op == '-') jit.emit_sub_eax_ecx();
    else if (op == '*') jit.emit_imul_eax_ecx();
    else return false;

    jit.emit_ret();
    out_result = jit.execute();
    return true;
}

bool evaluate_single_condition(std::string_view condition, std::shared_ptr<Environment> env) {
    std::string_view cond = trim(condition);
    
    size_t op_pos = cond.find("==");
    if (op_pos != std::string_view::npos) {
        std::string left = resolve_vars(cond.substr(0, op_pos), env);
        std::string right = resolve_vars(cond.substr(op_pos + 2), env);
        return left == right;
    }
    
    op_pos = cond.find('<');
    if (op_pos != std::string_view::npos) {
        std::string left = resolve_vars(cond.substr(0, op_pos), env);
        std::string right = resolve_vars(cond.substr(op_pos + 1), env);
        return safe_stoi(left) < safe_stoi(right);
    }

    op_pos = cond.find('>');
    if (op_pos != std::string_view::npos) {
        std::string left = resolve_vars(cond.substr(0, op_pos), env);
        std::string right = resolve_vars(cond.substr(op_pos + 1), env);
        return safe_stoi(left) > safe_stoi(right);
    }

    std::string val = resolve_vars(cond, env);
    return (val == "true" || val == "1");
}

bool evaluate_condition(std::string_view condition, std::shared_ptr<Environment> env) {
    std::string_view cond = trim(condition);

    size_t or_pos = cond.find("||");
    if (or_pos != std::string_view::npos) {
        return evaluate_condition(cond.substr(0, or_pos), env) || 
               evaluate_condition(cond.substr(or_pos + 2), env);
    }

    size_t and_pos = cond.find("&&");
    if (and_pos != std::string_view::npos) {
        return evaluate_condition(cond.substr(0, and_pos), env) && 
               evaluate_condition(cond.substr(and_pos + 2), env);
    }

    return evaluate_single_condition(cond, env);
}

void execute_block(const std::vector<std::string>& lines, size_t& index, size_t end, std::shared_ptr<Environment> env);

void execute_line(std::string_view line, std::shared_ptr<Environment> env) {
    std::string_view stripped = trim(line);
    if (stripped.empty() || stripped.rfind("//", 0) == 0) return;

    if (stripped.rfind("say ", 0) == 0 || stripped.rfind("print ", 0) == 0) {
        size_t space_pos = stripped.find(' ');
        std::cout << resolve_vars(stripped.substr(space_pos + 1), env) << "\n";
        return;
    }

    if (stripped == "fetch") {
        std::cout << "■ MiniPhone Engine OS (MP++) ■\n";
        bool found = false;
        std::string os = ctx.global_env->get("os_name", found);
        std::cout << "OS: " << (found ? os : "Unknown OS") << "\nTarget: High-Performance JIT Architecture\n";
        return;
    }

    // GLITCH-PROOFED AUTOMATIC PACKAGE MANAGER COMMAND
    if (stripped.rfind("mpkg install ", 0) == 0) {
        std::string pkg_name{trim(stripped.substr(13))};
        if(pkg_name.find_first_of(";&|`$") != std::string::npos) {
            std::cout << "✗ Security Error: Invalid package character set detected.\n";
            return;
        }

        std::string local_path = "ext/" + pkg_name + ".mp";
        
        // Glitch Prevention: Skip loading duplicates to prevent execution loops
        std::ifstream check_file(local_path);
        if (check_file.is_open()) {
            std::cout << "💡 Notice: Module '" << pkg_name << "' is already running inside memory layout context.\n";
            check_file.close();
            return;
        }

        std::cout << "📡 Downloading community module '" << pkg_name << "' from poopyking482-sudo...\n";
#ifdef _WIN32
        std::system("if not exist ext mkdir ext");
#else
        std::system("mkdir -p ext");
#endif
        std::stringstream cmd;
        cmd << "curl -s -f https://raw.githubusercontent.com/poopyking482-sudo/MP-Interpreter/main/modules/"
            << pkg_name << ".mpp -o " << local_path;
            
        if (std::system(cmd.str().c_str()) == 0) {
            // Read and automatically execute the downloaded code file into memory
            std::ifstream file(local_path);
            if (file.is_open()) {
                std::string file_line;
                std::vector<std::string> file_lines;
                while (std::getline(file, file_line)) {
                    file_lines.push_back(file_line);
                }
                file.close();
                
                size_t module_idx = 0;
                execute_block(file_lines, module_idx, file_lines.size(), env);
                std::cout << "✓ Module '" << pkg_name << "' safely parsed into language environment registry.\n";
            }
        } else {
            std::cout << "✗ Network Error: Unable to resolve package path on poopyking482-sudo/MP-Interpreter repository.\n";
        }
        return;
    }

    if (auto it = ctx.functions.find(stripped); it != ctx.functions.end()) {
        size_t local_idx = 0;
        auto local_env = std::make_shared<Environment>();
        local_env->parent = env; 
        execute_block(it->second, local_idx, it->second.size(), local_env);
        return;
    }

    size_t assign_pos = stripped.find('=');
    if (assign_pos != std::string_view::npos && stripped.find("==") == std::string_view::npos) {
        std::string_view var_name = trim(stripped.substr(0, assign_pos));
        std::string_view raw_val = trim(stripped.substr(assign_pos + 1));
        
        if (var_name == raw_val) {
            bool found = false;
            env->get(raw_val, found);
            if (!found) {
                std::cout << "Warning: Syntax Error -> Assigning undefined variable '" << raw_val << "' to itself.\n";
                return;
            }
        }

        char math_op = 0;
        size_t op_pos = std::string_view::npos;
        if ((op_pos = raw_val.find('+')) != std::string_view::npos) math_op = '+';
        else if ((op_pos = raw_val.find('-')) != std::string_view::npos) math_op = '-';
        else if ((op_pos = raw_val.find('*')) != std::string_view::npos) math_op = '*';

        if (math_op != 0) {
            std::string_view left_side = raw_val.substr(0, op_pos);
            std::string_view right_side = raw_val.substr(op_pos + 1);
            int jit_res = 0;
            if (handle_jit_math(left_side, right_side, math_op, env, jit_res)) {
                env->set(var_name, std::to_string(jit_res));
                return;
            }
        }
        
        env->set(var_name, resolve_vars(raw_val, env));
        return;
    }

    char math_op = 0;
    size_t op_pos = std::string_view::npos;
    if ((op_pos = stripped.find('+')) != std::string_view::npos) math_op = '+';
    else if ((op_pos = stripped.find('-')) != std::string_view::npos) math_op = '-';
    else if ((op_pos = stripped.find('*')) != std::string_view::npos) math_op = '*';

    if (math_op != 0) {
        std::string_view left_side = stripped.substr(0, op_pos);
        std::string_view right_side = stripped.substr(op_pos + 1);
        int jit_res = 0;
        if (handle_jit_math(left_side, right_side, math_op, env, jit_res)) {
            std::cout << "» [Silicon JIT Output]: " << jit_res << "\n";
            return;
        }
    }

    std::cout << "Warning: Syntax Error -> '" << stripped << "'\n";
}

size_t skip_block(const std::vector<std::string>& lines, size_t start_index, size_t end) {
    size_t depth = 1;
    size_t i = start_index;
    while (i < end && depth > 0) {
        std::string_view s = trim(lines[i]);
        if (s.rfind("if ", 0) == 0 || s.rfind("while ", 0) == 0) {
            if (s.find('{') != std::string_view::npos) depth++;
        } else if (s == "}" || s == "end" || s.find('}') != std::string_view::npos) {
            depth--;
        }
        if (depth == 0) return i;
        i++;
    }
    return end;
}

void execute_block(const std::vector<std::string>& lines, size_t& index, size_t end, std::shared_ptr<Environment> env) {
    while (index < end) {
        std::string_view stripped = trim(lines[index]);

        if (stripped.empty() || stripped.rfind("//", 0) == 0) {
            index++;
            continue;
        }

        if (stripped == "}" || stripped == "end" || stripped == "return 0;") {
            return; 
        }

        if (stripped.rfind("if ", 0) == 0 && stripped.find('{') != std::string_view::npos) {
            size_t brace_pos = stripped.find('{');
            std::string_view condition = trim(stripped.substr(3, brace_pos - 3));
            bool is_true = evaluate_condition(condition, env);
            
            size_t if_body_start = index + 1;
            size_t if_body_end = skip_block(lines, if_body_start, end);
            
            if (if_body_end == end) {
                std::cerr << "Syntax Error: Unclosed 'if' block.\n";
                return;
            }

            size_t next_line_idx = if_body_end + 1;
            bool has_else = false;
            size_t else_body_start = 0;
            size_t else_body_end = 0;

            if (next_line_idx < end && (trim(lines[next_line_idx]).rfind("else", 0) != std::string_view::npos)) {
                has_else = true;
                else_body_start = next_line_idx + 1;
                else_body_end = skip_block(lines, else_body_start, end);
            }

            if (is_true) {
                size_t local_idx = if_body_start;
                execute_block(lines, local_idx, if_body_end, env);
            } else if (has_else) {
                size_t local_idx = else_body_start;
                execute_block(lines, local_idx, else_body_end, env);
            }

            index = has_else ? (else_body_end + 1) : (if_body_end + 1);
            continue;
        }

        if (stripped.rfind("while ", 0) == 0 && stripped.find('{') != std::string_view::npos) {
            size_t brace_pos = stripped.find('{');
            std::string_view condition = trim(stripped.substr(6, brace_pos - 6));
            size_t loop_body_start = index + 1;
            size_t loop_body_end = skip_block(lines, loop_body_start, end);

            if (loop_body_end == end) {
                std::cerr << "Syntax Error: Unclosed 'while' loop.\n";
                return;
            }

            size_t iterations = 0;
            while (evaluate_condition(condition, env)) {
                if (++iterations > 1000000) {
                    std::cerr << "Runtime Error: Infinite loop safeguard triggered.\n";
                    break;
                }
                size_t local_idx = loop_body_start;
                execute_block(lines, local_idx, loop_body_end, env);
            }
            index = loop_body_end + 1;
            continue;
        }

        if (stripped.rfind("def ", 0) == 0) {
            std::string func_name{trim(stripped.substr(4))};
            std::vector<std::string> body;
            index++;
            while (index < end && trim(lines[index]) != "end") {
                body.push_back(lines[index]);
                index++;
            }
            ctx.functions[func_name] = body;
            index++;
            continue;
        }

        execute_line(lines[index], env);
        index++;
    }
}

void run_repl() {
    std::cout << "=========================================================\n";
    std::cout << "  Welcome to MiniPhone++ (MP++) Inline Hardware JIT Shell \n";
    std::cout << "  Type 'exit' or 'quit' to terminate program execution   \n";
    std::cout << "=========================================================\n";

    std::string line;
    while (true) {
        std::cout << "mp++ > ";
        if (!std::getline(std::cin, line)) break;

        std::string_view stripped = trim(line);
        if (stripped == "exit" || stripped == "quit") {
            std::cout << "Goodbye!\n";
            break;
        }
        
        if (stripped.empty()) continue;

        if (stripped.find('{') != std::string_view::npos || stripped.rfind("def ", 0) == 0) {
            std::vector<std::string> block_lines;
            block_lines.push_back(std::string(stripped));
            
            std::string sub_line;
            int open_blocks = 1;
            
            while (open_blocks > 0) {
                std::cout << "   ... ";
                if (!std::getline(std::cin, sub_line)) break;
                std::string_view sub_stripped = trim(sub_line);
                
                if (sub_stripped.find('{') != std::string_view::npos || sub_stripped.rfind("def ", 0) == 0) open_blocks++;
                if (sub_stripped == "}" || sub_stripped == "end") open_blocks--;
                
                block_lines.push_back(sub_line);
            }
            size_t idx = 0;
            execute_block(block_lines, idx, block_lines.size(), ctx.global_env);
        } else {
            execute_line(stripped, ctx.global_env);
        }
    }
}

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

#ifdef _WIN32
    ctx.global_env->set("os_name", "Windows");
#elif __APPLE__
    ctx.global_env->set("os_name", "macOS");
#else
    ctx.global_env->set("os_name", "Linux");
#endif

    run_repl();
    return 0;
}
