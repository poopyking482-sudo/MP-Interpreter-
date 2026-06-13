#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <cstdlib>

namespace mp_module {

// cache full module source (IMPORTANT FIX)
static std::unordered_map<std::string, std::string> module_cache;

// download module
inline bool download_module(const std::string& name) {
    std::string path = "ext/" + name + ".mp";

    std::ifstream test(path);
    if (test.is_open()) {
        return true;
    }

    std::cout << "📡 Downloading module: " << name << "\n";

#ifdef _WIN32
    std::system("if not exist ext mkdir ext");
#else
    std::system("mkdir -p ext");
#endif

    std::string cmd =
        "curl -s -f https://raw.githubusercontent.com/poopyking482-sudo/MP-Interpreter/main/modules/"
        + name + ".mp -o " + path;

    return std::system(cmd.c_str()) == 0;
}

// read file
inline std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// main import
inline std::string import_module(const std::string& name) {

    // 1. return cached version if exists
    if (module_cache.count(name)) {
        std::cout << "💡 Module cache hit: " << name << "\n";
        return module_cache[name];
    }

    std::string path = "ext/" + name + ".mp";

    // 2. download if needed
    if (!download_module(name)) {
        std::cerr << "✗ Failed to download module: " << name << "\n";
        return "";
    }

    // 3. read module
    std::string code = read_file(path);

    if (code.empty()) {
        std::cerr << "✗ Module empty: " << name << "\n";
        return "";
    }

    // 4. cache it
    module_cache[name] = code;

    std::cout << "✓ Module loaded: " << name << "\n";
    return code;
}

} // namespace mp_module