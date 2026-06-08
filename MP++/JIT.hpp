#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

class JITEngine {
private:
    std::vector<uint8_t> machine_code;
    void* exec_mem = nullptr;
    size_t allocated_size = 0;

    void* allocate_memory(size_t size);
    void free_memory(void* ptr, size_t size);

public:
    JITEngine() = default;
    ~JITEngine();

    void emit_mov_eax(int value);
    void emit_ret();
    int execute();
    void clear();
};
