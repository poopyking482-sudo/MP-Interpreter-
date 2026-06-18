#include <iostream>
#include <vector>
#include <cstring>

// Platform-specific headers for changing memory permissions
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

// Allocates memory that the CPU is legally allowed to execute
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
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

int main() {
    std::cout << "=== MP++ Raw Hardware JIT Engine ===\n";

    // This vector acts as your dynamic compiler buffer
    std::vector<uint8_t> machine_code;

    // Imagine your parser reads: "x = 510"
    // We translate that into x86-64 Assembly:
    // mov eax, 510   -> Hex: b8 fe 01 00 00
    // ret            -> Hex: c3

    int user_value = 510; 

    // 1. Emit 'mov eax, <value>' opcode (0xB8)
    machine_code.push_back(0xB8);
    
    // 2. Push the 32-bit integer value (Little Endian bytes)
    machine_code.push_back((user_value >> 0) & 0xFF);
    machine_code.push_back((user_value >> 8) & 0xFF);
    machine_code.push_back((user_value >> 16) & 0xFF);
    machine_code.push_back((user_value >> 24) & 0xFF);

    // 3. Emit 'ret' opcode (0xC3) to return control back to C++
    machine_code.push_back(0xC3);

    // 4. Request executable memory page from the Operating System
    void* exec_mem = allocate_executable_memory(machine_code.size());
    if (!exec_mem) {
        std::cerr << "OS refused to grant executable memory rights.\n";
        return 1;
    }

    // 5. Flash our compiled bytecode buffer into silicon-ready memory
    std::memcpy(exec_mem, machine_code.data(), machine_code.size());

    // 6. Cast the raw memory address into a standard C++ function pointer
    typedef int (*JitFunction)();
    JitFunction run_code = (JitFunction)exec_mem;

    // 7. Light the fuse. The CPU runs your dynamically generated code at native speed.
    std::cout << "Executing machine code buffer...\n";
    int result = run_code();
    
    std::cout << "Result from Hardware: " << result << "\n"; // Prints 510

    free_executable_memory(exec_mem, machine_code.size());
    return 0;
}
// we don't know why is it here