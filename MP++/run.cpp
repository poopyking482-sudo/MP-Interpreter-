// main.cpp
#include "mpp"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char* argv[]) {
    // 1. Initialize your lightweight environment states
    // (Bake your OS names, variables, and 20KB memory limit wrappers here)
    
    // 2. CHECK IF A FILE WAS PASSED: e.g., "mpp test.mpp"
    if (argc > 1) {
        std::string filename = argv[1];
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            std::cerr << "✗ Error: Could not open file '" << filename << "'\n";
            return 1;
        }

        // Read the entire .mpp script into a string stream
        std::stringstream ss;
        ss << file.rdbuf();
        std::string script_contents = ss.str();
        file.close();

        // Feed the script text directly into your tokenizer loop
        std::vector<Token> tokens = tokenize(script_contents);
        size_t idx = 0;
        execute_tokens(tokens, idx, tokens.size(), ctx.global_env);
        
        return 0; // Exit cleanly after running the file
    }

    // 3. FALLBACK: If no file argument is given, launch the interactive REPL shell
    launch_mpp_interpreter();
    return 0;
}
