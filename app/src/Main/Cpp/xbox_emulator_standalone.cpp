#include "x86_core.h"
#include "xbox_memory.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

class SimpleXboxMemory : public XboxMemory {
public:
    SimpleXboxMemory(size_t size = 16 * 1024 * 1024) : memorySize(size) {
        memory = new uint8_t[size];
        memset(memory, 0, size);
    }

    ~SimpleXboxMemory() {
        delete[] memory;
    }

    uint8_t read8(uint32_t address) const override {
        if (address >= memorySize) return 0;
        return memory[address];
    }

    uint16_t read16(uint32_t address) const override {
        if (address + 1 >= memorySize) return 0;
        return *reinterpret_cast<uint16_t*>(&memory[address]);
    }

    uint32_t read32(uint32_t address) const override {
        if (address + 3 >= memorySize) return 0;
        return *reinterpret_cast<uint32_t*>(&memory[address]);
    }

    void write8(uint32_t address, uint8_t value) override {
        if (address >= memorySize) return;
        memory[address] = value;
    }

    void write16(uint32_t address, uint16_t value) override {
        if (address + 1 >= memorySize) return;
        *reinterpret_cast<uint16_t*>(&memory[address]) = value;
    }

    void write32(uint32_t address, uint32_t value) override {
        if (address + 3 >= memorySize) return;
        *reinterpret_cast<uint32_t*>(&memory[address]) = value;
    }

    size_t getSize() const override {
        return memorySize;
    }

private:
    uint8_t* memory;
    size_t memorySize;
};

int main(int argc, char* argv[]) {
    std::cout << "Xanite Standalone Xbox Emulator" << std::endl;
    std::cout << "=================================" << std::endl;

    try {

        SimpleXboxMemory* memory = new SimpleXboxMemory();
        X86Core* cpu = new X86Core(memory);

        std::cout << "CPU and Memory initialized successfully!" << std::endl;
        std::cout << "EIP: 0x" << std::hex << cpu->getEIP() << std::endl;
        std::cout << "ESP: 0x" << std::hex << cpu->getESP() << std::endl;


        std::cout << "\nExecuting test instructions..." << std::endl;

        for (int i = 0; i < 10; ++i) {
            cpu->executeNextInstruction();
            std::cout << "Instruction " << i + 1 << " executed" << std::endl;
        }

        std::cout << "Test completed successfully!" << std::endl;


        delete cpu;
        delete memory;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}


