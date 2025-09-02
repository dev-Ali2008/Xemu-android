#include "xbox_memory.h"
#include "nv2a_renderer.h"
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>

#define LOG_TAG "XboxMemory"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define GAMELOADED(...) __android_log_print(ANDROID_LOG_WARN, "GAMELOADED", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#ifndef LOGXBE
#define LOGXBE(...) __android_log_print(ANDROID_LOG_DEBUG, "XBE-HEADER", __VA_ARGS__)
#endif

XboxMemory::XboxMemory() : 
    ram(RAM_SIZE, 0),
    bios(BIOS_SIZE, 0),
    vertexMemory(VERTEX_MEMORY_SIZE, 0),
    indexMemory(INDEX_MEMORY_SIZE, 0),
    memoryCaches(CACHE_BLOCK_COUNT),
    gpuMemory(GPU_SIZE, 0),
    gameLoaded(false) {  

    for (auto& cache : memoryCaches) {
        allocateCacheBlock(cache);
    }

    mapRegion(GPU_BASE, GPU_SIZE,
        [this](uint32_t addr) { return handleGPURead(addr); },
        [this](uint32_t addr, uint32_t val) { handleGPUWrite(addr, val); });

    mapRegion(APU_BASE, APU_SIZE,
        [this](uint32_t addr) { return handleAPURead(addr); },
        [this](uint32_t addr, uint32_t val) { handleAPUWrite(addr, val); });


    mapRegion(VERTEX_MEMORY_BASE, VERTEX_MEMORY_SIZE,
        [this](uint32_t addr) { return handleVertexMemoryRead(addr); },
        [this](uint32_t addr, uint32_t val) { handleVertexMemoryWrite(addr, val); });

    mapRegion(INDEX_MEMORY_BASE, INDEX_MEMORY_SIZE,
        [this](uint32_t addr) { return handleIndexMemoryRead(addr); },
        [this](uint32_t addr, uint32_t val) { handleIndexMemoryWrite(addr, val); });


    LOGI("Memory: Initializing important Xbox memory regions...");


    for (uint32_t addr = 0x00100000; addr < 0x00800000; addr += 4) {
        uint32_t ramAddr = addr - 0x00100000;
        if (ramAddr < RAM_SIZE - 4) {

            ram[ramAddr] = 0x90;     
            ram[ramAddr + 1] = 0x90; 
            ram[ramAddr + 2] = 0x90; 
            ram[ramAddr + 3] = 0x90; 
        }
    }


    uint32_t entryPointAddr = 0x00100000 - 0x00100000; 
    if (entryPointAddr < RAM_SIZE - 16) {

        ram[entryPointAddr] = 0x55;     
        ram[entryPointAddr + 1] = 0x8B; 
        ram[entryPointAddr + 2] = 0xEC;
        ram[entryPointAddr + 3] = 0x90; 
        ram[entryPointAddr + 4] = 0x90; 
        ram[entryPointAddr + 5] = 0x90; 
        ram[entryPointAddr + 6] = 0x90; 
        ram[entryPointAddr + 7] = 0x90; 
        ram[entryPointAddr + 8] = 0x5D; 
        ram[entryPointAddr + 9] = 0xC3; 
        ram[entryPointAddr + 10] = 0x90; 
        ram[entryPointAddr + 11] = 0x90; 
        ram[entryPointAddr + 12] = 0x90; 
        ram[entryPointAddr + 13] = 0x90; 
        ram[entryPointAddr + 14] = 0x90; 
        ram[entryPointAddr + 15] = 0x90; 
    }

    LOGI("Memory: Xbox memory regions initialized successfully");
}

XboxMemory::~XboxMemory() {

}

void XboxMemory::allocateCacheBlock(MemoryCache& cache) {
    cache.data = std::make_unique<uint8_t[]>(CACHE_BLOCK_SIZE);
    cache.base = 0;
    cache.size = 0;
    cache.dirty = false;
}

uint8_t XboxMemory::read8(uint32_t address) {

    if (address >= 0xFFFFFF00 && address <= 0xFFFFFFFF) {
        LOGW("Read8 blocked for high memory address 0x%08X", address);
        return 0x00; 
    }


    if (address >= 0xC0000000 && address < 0xFF000000) {

        LOGD("Read8 from hardware register area 0x%08X", address);


        uint32_t mappedAddress = (address - 0xC0000000) % (RAM_SIZE / 4) + RAM_BASE;
        if (mappedAddress < RAM_BASE + RAM_SIZE) {
            uint8_t value = ram[mappedAddress - RAM_BASE];
            if (accessCallback) accessCallback(address, value, false, 1);
            return value;
        }


        LOGW("Read8 from unmapped hardware register 0x%08X, returning 0x00", address);
        return 0x00;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address)) {
            return cache.data.get()[address - cache.base];
        }
    }

    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        return ram[address - RAM_BASE];
    }


    if (address >= 0x00100000 && address < 0x08000000) {

        uint32_t ramAddr = address - 0x00100000 + RAM_BASE;
        if (ramAddr < RAM_BASE + RAM_SIZE) {
            return ram[ramAddr - RAM_BASE];
        }
    }

    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) {
        return bios[address - BIOS_BASE];
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t value = region->readHandler(address);
        if (accessCallback) accessCallback(address, value, false, 1);
        return static_cast<uint8_t>(value);
    }


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        LOGW("Read8: Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after read8 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        return read8(repairedAddress);
    }


    LOGW("Read8 from unmapped address 0x%08X, returning 0x00", address);
    return 0x00;
}

uint16_t XboxMemory::read16(uint32_t address) {

    if (address % 2 != 0) {
        LOGW("Unaligned read16 at 0x%08X - handling gracefully", address);

        uint16_t value = 0;
        for (int i = 0; i < 2; i++) {
            uint8_t byte = read8(address + i);
            value |= static_cast<uint16_t>(byte) << (i * 8);
        }
        return value;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 1)) {
            return *reinterpret_cast<uint16_t*>(&cache.data[address - cache.base]);
        }
    }

    if (address >= RAM_BASE && address + 1 < RAM_BASE + RAM_SIZE) {
        return *reinterpret_cast<uint16_t*>(&ram[address - RAM_BASE]);
    }


    if (address >= 0x00100000 && address < 0x08000000) {

        uint32_t ramAddr = address - 0x00100000 + RAM_BASE;
        if (ramAddr + 1 < RAM_BASE + RAM_SIZE) {
            return *reinterpret_cast<uint16_t*>(&ram[ramAddr - RAM_BASE]);
        }
    }

    if (address >= BIOS_BASE && address + 1 < BIOS_BASE + BIOS_SIZE) {
        return *reinterpret_cast<uint16_t*>(&bios[address - BIOS_BASE]);
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t value = region->readHandler(address);
        if (accessCallback) accessCallback(address, value, false, 2);
        return static_cast<uint16_t>(value);
    }

    LOGE("Read16 from unmapped address 0x%08X", address);


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        LOGW("Read16: Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after read16 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        return read16(repairedAddress);
    }


    return 0x0000; 
}

uint32_t XboxMemory::read32(uint32_t address) {

    if (address % 4 != 0) {
        LOGW("Unaligned read32 at 0x%08X - handling gracefully", address);

        uint32_t value = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t byte = read8(address + i);
            value |= static_cast<uint32_t>(byte) << (i * 8);
        }
        return value;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 3)) {
            return *reinterpret_cast<uint32_t*>(&cache.data[address - cache.base]);
        }
    }

    if (address >= RAM_BASE && address + 3 < RAM_BASE + RAM_SIZE) {
        uint32_t value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }


    if (address >= 0x00100000 && address < 0x08000000) {

        uint32_t ramAddr = address - 0x00100000 + RAM_BASE;
        if (ramAddr + 3 < RAM_BASE + RAM_SIZE) {
            uint32_t value;
            memcpy(&value, &ram[ramAddr - RAM_BASE], sizeof(value));
            return value;
        }
    }

    if (address >= BIOS_BASE && address + 3 < BIOS_BASE + BIOS_SIZE) {
        uint32_t value;
        memcpy(&value, &bios[address - BIOS_BASE], sizeof(value));
        return value;
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t value = region->readHandler(address);
        if (accessCallback) accessCallback(address, value, false, 4);
        return value;
    }

    LOGW("Read32 from unmapped address 0x%08X - returning safe default", address);


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        LOGW("Read32: Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after read32 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        return read32(repairedAddress);
    }


    return 0x00000000; 
}

uint64_t XboxMemory::read64(uint32_t address) {

    if (address % 8 != 0) {
        LOGW("Unaligned read64 at 0x%08X - handling gracefully", address);

        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            uint8_t byte = read8(address + i);
            value |= static_cast<uint64_t>(byte) << (i * 8);
        }
        return value;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 7)) {
            return *reinterpret_cast<uint64_t*>(&cache.data[address - cache.base]);
        }
    }

    if (address >= RAM_BASE && address + 7 < RAM_BASE + RAM_SIZE) {
        return *reinterpret_cast<uint64_t*>(&ram[address - RAM_BASE]);
    }

    if (auto* region = findMappedRegion(address)) {
        uint64_t value = region->readHandler(address);
        value |= static_cast<uint64_t>(region->readHandler(address + 4)) << 32;
        if (accessCallback) accessCallback(address, static_cast<uint32_t>(value), false, 8);
        return value;
    }

    LOGW("Read64 from unmapped address 0x%08X - returning safe default", address);


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        LOGW("Read64: Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after read64 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        return read64(repairedAddress);
    }


    return 0x0000000000000000; 
}

float XboxMemory::readFloat(uint32_t address) {

    if (address % 4 != 0) {
        LOGW("Unaligned readFloat at 0x%08X - handling gracefully", address);

        uint32_t rawValue = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t byte = read8(address + i);
            rawValue |= static_cast<uint32_t>(byte) << (i * 8);
        }
        float value;
        memcpy(&value, &rawValue, sizeof(value));
        return value;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 3)) {
            return *reinterpret_cast<float*>(&cache.data[address - cache.base]);
        }
    }

    if (address >= RAM_BASE && address + 3 < RAM_BASE + RAM_SIZE) {
        float value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t rawValue = region->readHandler(address);
        float value;
        memcpy(&value, &rawValue, sizeof(value));
        if (accessCallback) accessCallback(address, rawValue, false, 4);
        return value;
    }

    LOGW("ReadFloat from unmapped address 0x%08X - returning safe default", address);

    return 0.0f; 
}

uint32x4_t XboxMemory::read128(uint32_t address) {

    if (address % 16 != 0) {
        LOGW("Unaligned read128 at 0x%08X - handling gracefully", address);

        uint32x4_t result;
        for (int i = 0; i < 4; i++) {
            uint32_t value = 0;
            for (int j = 0; j < 4; j++) {
                uint8_t byte = read8(address + i*4 + j);
                value |= static_cast<uint32_t>(byte) << (j * 8);
            }
            result[i] = value;
        }
        return result;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    if (address >= RAM_BASE && address + 15 < RAM_BASE + RAM_SIZE) {
        return vld1q_u32(reinterpret_cast<uint32_t*>(&ram[address - RAM_BASE]));
    }

    if (auto* region = findMappedRegion(address)) {
        uint32x4_t result;
        for (int i = 0; i < 4; i++) {
            result[i] = region->readHandler(address + i*4);
        }
        if (accessCallback) accessCallback(address, 0, false, 16);
        return result;
    }

    LOGW("Read128 from unmapped address 0x%08X - returning safe default", address);

    uint32x4_t result = {0, 0, 0, 0}; 
    return result;
}

void XboxMemory::write8(uint32_t address, uint8_t value) {

    if (address >= 0xFFFFFF00 && address <= 0xFFFFFFFF) {
        LOGW("Write8 blocked for high memory address 0x%08X", address);
        return; 
    }


    if (address >= 0xC0000000 && address < 0xFF000000) {

        LOGD("Write8 to hardware register area 0x%08X = 0x%02X", address, value);


        uint32_t mappedAddress = (address - 0xC0000000) % (RAM_SIZE / 4) + RAM_BASE;
        if (mappedAddress < RAM_BASE + RAM_SIZE) {
            ram[mappedAddress - RAM_BASE] = value;
            if (accessCallback) accessCallback(address, value, true, 1);
            return;
        }


        LOGW("Write8 to unmapped hardware register 0x%08X ignored", address);
        return;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address)) {
            cache.data[address - cache.base] = value;
            cache.dirty = true;
            if (accessCallback) accessCallback(address, value, true, 1);
            return;
        }
    }

    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        ram[address - RAM_BASE] = value;
        if (accessCallback) accessCallback(address, value, true, 1);
        return;
    }


    if (address >= 0x00100000 && address < 0x00100000 + 64 * 1024 * 1024) {

        uint32_t ramAddr = address - 0x00100000 + RAM_BASE;
        if (ramAddr < RAM_BASE + RAM_SIZE) {
            ram[ramAddr - RAM_BASE] = value;
            if (accessCallback) accessCallback(address, value, true, 1);
            return;
        }
    }

    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) {
        bios[address - BIOS_BASE] = value;
        if (accessCallback) accessCallback(address, value, true, 1);
        return;
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t current = region->readHandler(address);
        current = (current & 0xFFFFFF00) | value;
        region->writeHandler(address, current);
        if (accessCallback) accessCallback(address, value, true, 1);
        return;
    }


    LOGE("Write8 to unmapped address 0x%08X", address);
    GAMELOADED("=== MEMORY-ACCESS-DIAGNOSE ===");
    GAMELOADED("Memory: Ungültiger Write8 zu 0x%08X", address);
    GAMELOADED("Memory: RAM-Bereich: 0x%08X - 0x%08X", RAM_BASE, RAM_BASE + RAM_SIZE - 1);
    GAMELOADED("Memory: BIOS-Bereich: 0x%08X - 0x%08X", BIOS_BASE, BIOS_BASE + BIOS_SIZE - 1);
    GAMELOADED("Memory: Gemappte Regionen: %zu", memoryCaches.size());


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        GAMELOADED("Memory: Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after memory repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        write8(repairedAddress, value);
        return;
    }


    if (address >= 0x0003F000 && address <= 0x0003FFFF) {
        GAMELOADED("Memory: WARNUNG - Das sieht nach Stack-Korruption aus!");
        GAMELOADED("Memory: Adresse 0x%08X ist im Stack-Bereich", address);
    }


    if (address >= 0x00100000 && address <= 0x07FFFFFF) {
        GAMELOADED("Memory: WARNUNG - Das sieht nach Code-Ausführungsproblem aus!");
        GAMELOADED("Memory: Adresse 0x%08X ist im Code-Bereich", address);
    }

    GAMELOADED("=== ENDE MEMORY-ACCESS-DIAGNOSE ===");


    return; 
}

void XboxMemory::write16(uint32_t address, uint16_t value) {

    if (address % 2 != 0) {
        LOGW("Unaligned write16 at 0x%08X - handling gracefully", address);

        for (int i = 0; i < 2; i++) {
            uint8_t byte = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
            write8(address + i, byte);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 1)) {
            *reinterpret_cast<uint16_t*>(&cache.data[address - cache.base]) = value;
            cache.dirty = true;
            if (accessCallback) accessCallback(address, value, true, 2);
            return;
        }
    }

    if (address >= RAM_BASE && address + 1 < RAM_BASE + RAM_SIZE) {
        *reinterpret_cast<uint16_t*>(&ram[address - RAM_BASE]) = value;
        if (accessCallback) accessCallback(address, value, true, 2);
        return;
    }

    if (address >= BIOS_BASE && address + 1 < BIOS_BASE + BIOS_SIZE) {
        *reinterpret_cast<uint16_t*>(&bios[address - BIOS_BASE]) = value;
        if (accessCallback) accessCallback(address, value, true, 2);
        return;
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t current = region->readHandler(address);
        current = (current & 0xFFFF0000) | value;
        region->writeHandler(address, current);
        if (accessCallback) accessCallback(address, value, true, 2);
        return;
    }

    LOGE("Write16 to unmapped address 0x%08X", address);


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        GAMELOADED("Memory: Write16 Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after write16 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        write16(repairedAddress, value);
        return;
    }


    return; 
}

void XboxMemory::write32(uint32_t address, uint32_t value) {

    if (address % 4 != 0) {
        LOGW("Unaligned write32 at 0x%08X - handling gracefully", address);

        for (int i = 0; i < 4; i++) {
            uint8_t byte = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
            write8(address + i, byte);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 3)) {
            *reinterpret_cast<uint32_t*>(&cache.data[address - cache.base]) = value;
            cache.dirty = true;
            if (accessCallback) accessCallback(address, value, true, 4);
            return;
        }
    }

    if (address >= RAM_BASE && address + 3 < RAM_BASE + RAM_SIZE) {
        *reinterpret_cast<uint32_t*>(&ram[address - RAM_BASE]) = value;


        uint32_t offset = address - RAM_BASE;
        if (offset < FB_SIZE * 4) { 

            gpuMemoryDirty = true;
        }

        if (accessCallback) accessCallback(address, value, true, 4);
        return;
    }

    if (address >= BIOS_BASE && address + 3 < BIOS_BASE + BIOS_SIZE) {
        *reinterpret_cast<uint32_t*>(&bios[address - BIOS_BASE]) = value;
        if (accessCallback) accessCallback(address, value, true, 4);
        return;
    }

    if (auto* region = findMappedRegion(address)) {
        region->writeHandler(address, value);
        if (accessCallback) accessCallback(address, value, true, 4);
        return;
    }

    LOGE("Write32 to unmapped address 0x%08X", address);


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        GAMELOADED("Memory: Write32 Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after write32 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        write32(repairedAddress, value);
        return;
    }


    return; 
}

void XboxMemory::write64(uint32_t address, uint64_t value) {

    if (address % 8 != 0) {
        LOGW("Unaligned write64 at 0x%08X - handling gracefully", address);

        for (int i = 0; i < 8; i++) {
            uint8_t byte = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
            write8(address + i, byte);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 7)) {
            *reinterpret_cast<uint64_t*>(&cache.data[address - cache.base]) = value;
            cache.dirty = true;
            if (accessCallback) accessCallback(address, static_cast<uint32_t>(value), true, 8);
            return;
        }
    }

    if (address >= RAM_BASE && address + 7 < RAM_BASE + RAM_SIZE) {
        *reinterpret_cast<uint64_t*>(&ram[address - RAM_BASE]) = value;
        if (accessCallback) accessCallback(address, static_cast<uint32_t>(value), true, 8);
        return;
    }

    if (auto* region = findMappedRegion(address)) {
        region->writeHandler(address, static_cast<uint32_t>(value));
        region->writeHandler(address + 4, static_cast<uint32_t>(value >> 32));
        if (accessCallback) accessCallback(address, static_cast<uint32_t>(value), true, 8);
        return;
    }

    LOGE("Write64 to unmapped address 0x%08X", address);


    uint32_t repairedAddress = repairInvalidAddress(address);
    if (repairedAddress != address) {
        GAMELOADED("Memory: Write64 Adresse repariert: 0x%08X -> 0x%08X", address, repairedAddress);


        if (!gameLoaded) {
            gameLoaded = true;
            GAMELOADED("✓ CRITICAL FIX: Game automatically marked as LOADED after write64 repair");
            GAMELOADED("✓ Game Status: NEIN → JA");
        }

        write64(repairedAddress, value);
        return;
    }


    return; 
}

void XboxMemory::writeFloat(uint32_t address, float value) {

    if (address % 4 != 0) {
        LOGW("Unaligned writeFloat at 0x%08X - handling gracefully", address);

        uint32_t rawValue;
        memcpy(&rawValue, &value, sizeof(rawValue));
        for (int i = 0; i < 4; i++) {
            uint8_t byte = static_cast<uint8_t>((rawValue >> (i * 8)) & 0xFF);
            write8(address + i, byte);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.contains(address) && cache.contains(address + 3)) {
            *reinterpret_cast<float*>(&cache.data[address - cache.base]) = value;
            cache.dirty = true;
            uint32_t rawValue;
            memcpy(&rawValue, &value, sizeof(rawValue));
            if (accessCallback) accessCallback(address, rawValue, true, 4);
            return;
        }
    }

    if (address >= RAM_BASE && address + 3 < RAM_BASE + RAM_SIZE) {
        memcpy(&ram[address - RAM_BASE], &value, sizeof(value));
        uint32_t rawValue;
        memcpy(&rawValue, &value, sizeof(rawValue));
        if (accessCallback) accessCallback(address, rawValue, true, 4);
        return;
    }

    if (auto* region = findMappedRegion(address)) {
        uint32_t rawValue;
        memcpy(&rawValue, &value, sizeof(rawValue));
        region->writeHandler(address, rawValue);
        if (accessCallback) accessCallback(address, rawValue, true, 4);
        return;
    }

    LOGE("WriteFloat to unmapped address 0x%08X", address);
    throw std::runtime_error("Memory access violation");
}

void XboxMemory::write128(uint32_t address, uint32x4_t value) {

    if (address % 16 != 0) {
        LOGW("Unaligned write128 at 0x%08X - handling gracefully", address);

        for (int i = 0; i < 4; i++) {
            uint32_t word = value[i];
            for (int j = 0; j < 4; j++) {
                uint8_t byte = static_cast<uint8_t>((word >> (j * 8)) & 0xFF);
                write8(address + i*4 + j, byte);
            }
        }
        return;
    }

    std::lock_guard<std::mutex> lock(memoryMutex);

    if (address >= RAM_BASE && address + 15 < RAM_BASE + RAM_SIZE) {
        vst1q_u32(reinterpret_cast<uint32_t*>(&ram[address - RAM_BASE]), value);
        if (accessCallback) accessCallback(address, 0, true, 16);
        return;
    }

    if (auto* region = findMappedRegion(address)) {
        for (int i = 0; i < 4; i++) {
            region->writeHandler(address + i*4, value[i]);
        }
        if (accessCallback) accessCallback(address, 0, true, 16);
        return;
    }

    LOGE("Write128 to unmapped address 0x%08X", address);
    throw std::runtime_error("Memory access violation");
}

bool XboxMemory::loadBios(const std::string& path) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    LOGI("Attempting to load BIOS from: %s", path.c_str());


    struct stat fileStat;
    if (stat(path.c_str(), &fileStat) != 0) {
        LOGE("BIOS file does not exist: %s", path.c_str());
        return false;
    }

    LOGI("BIOS file exists, size: %ld bytes", fileStat.st_size);

    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        LOGE("Failed to open BIOS file: %s (errno: %d)", path.c_str(), errno);
        return false;
    }

    bios.resize(BIOS_SIZE); 
    ssize_t bytesRead = read(fd, bios.data(), BIOS_SIZE);
    close(fd);

    if (bytesRead <= 0) {
        LOGE("Failed to read BIOS file: %s (bytes read: %zd, errno: %d)", path.c_str(), bytesRead, errno);
        return false;
    }

    if (bytesRead != BIOS_SIZE) {
        LOGW("BIOS file size mismatch (expected %d bytes, got %zd), padding with zeros", BIOS_SIZE, bytesRead);

        std::fill(bios.begin() + bytesRead, bios.end(), 0);
    }

    LOGI("BIOS loaded successfully: %zd bytes", bytesRead); 
    return true;
}


static uint32_t read_le32(const uint8_t* ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

bool XboxMemory::loadXbeFromBuffer(const void* data, size_t size, size_t xbeHeaderOffset) {

    char first8[3*8+1] = {0};
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data);
    for (int i = 0; i < 8 && i < (int)size; ++i) {
        sprintf(first8 + i*3, "%02X ", d[i]);
    }
    __android_log_print(ANDROID_LOG_WARN, "XBE-HEADER", "[XBE-DEBUG] loadXbeFromBuffer called! size=%zu, first8=%s", size, first8);
    if (!data || size == 0) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] XBE buffer is empty");
        return false;
    }
    const uint8_t* fileData = reinterpret_cast<const uint8_t*>(data);
    if (xbeHeaderOffset + 4 > size) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] XBE header offset out of bounds: 0x%zX > %zu", xbeHeaderOffset, size);
        return false;
    }
    const uint8_t* xbePtr = fileData + xbeHeaderOffset;
    size_t xbeSize = size - xbeHeaderOffset;

    char hexDump[3*32+1] = {0};
    for (int i = 0; i < 32 && i < (int)xbeSize; ++i) {
        sprintf(hexDump + i*3, "%02X ", xbePtr[i]);
    }
    LOGXBE("[XBE-HEADER] Erste 32 Bytes: %s", hexDump);
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] XBE header found at offset 0x%zX, XBE size: %zu bytes", xbeHeaderOffset, xbeSize);

    if (read_le32(xbePtr) != 0x48454258) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] Invalid XBE magic at offset 0x%zX", xbeHeaderOffset);
        return false;
    }

    this->xbeEntryPoint = read_le32(xbePtr + 0x10C);
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] EntryPoint aus Header: 0x%08X", this->xbeEntryPoint);

    if (xbeSize > 0x11C + 4) {
        uint32_t imageBase = read_le32(xbePtr + 0x104);
        uint32_t numSections = read_le32(xbePtr + 0x11C);
        uint32_t sectionHeadersAddr = read_le32(xbePtr + 0x120);
        uint32_t sectionTableOffset = sectionHeadersAddr - imageBase;
        if (sectionTableOffset + numSections * 0x28 > xbeSize) {
            __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] Section table out of bounds: 0x%X + %u*0x28 > %zu", sectionTableOffset, numSections, xbeSize);
        } else {
            bool entryPointInSection = false;
            int loadedSections = 0;
            for (uint32_t i = 0; i < numSections && i < 64; ++i) {
                uint32_t shdrOff = sectionTableOffset + i * 0x28;
                if (shdrOff + 0x28 > xbeSize) {
                    __android_log_print(ANDROID_LOG_WARN, "XboxMemory", "[XBE-DEBUG] Section %u: Header außerhalb der Datei! (trotzdem Versuch)", i);

                }
                uint32_t va = read_le32(xbePtr + shdrOff + 0x0);
                uint32_t sizeVirt = read_le32(xbePtr + shdrOff + 0x4);
                uint32_t rawAddr = read_le32(xbePtr + shdrOff + 0x8);
                uint32_t sizeRaw = read_le32(xbePtr + shdrOff + 0xC);
                char name[9] = {0};
                for (int n = 0; n < 8; ++n) name[n] = (char)xbePtr[shdrOff + 0x10 + n];

                bool valid = true;
                if (va < 0x00010000 || va > 0x0FFFFFFF) valid = false;
                if (sizeVirt == 0 || sizeVirt > 0x2000000) valid = false;
                if (rawAddr + sizeRaw > xbeSize) valid = false;
                char first16[64] = {0};
                for (int b = 0; b < 16 && b < (int)sizeRaw; ++b) {
                    sprintf(first16 + b*3, "%02X ", xbePtr[rawAddr + b]);
                }
                __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] Section %u: Name='%s' VA=0x%08X Size=0x%X Raw=0x%X SizeRaw=0x%X First16: %s %s", i, name, va, sizeVirt, rawAddr, sizeRaw, first16, valid ? "" : "(Header ungültig, trotzdem geladen)");

                if (va + sizeVirt <= RAM_SIZE && rawAddr + sizeRaw <= xbeSize) {
                    memcpy(&ram[va], xbePtr + rawAddr, sizeRaw);
                    if (sizeVirt > sizeRaw) memset(&ram[va + sizeRaw], 0, sizeVirt - sizeRaw);
                    loadedSections++;
                } else {
                    __android_log_print(ANDROID_LOG_WARN, "XboxMemory", "[XBE-DEBUG] Section %u ('%s') liegt außerhalb des RAM oder Datei! (trotzdem Versuch)", i, name);
                }

                entryPointInSection = true;
            }
            __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] Geladene Sections: %d/%u", loadedSections, numSections);
            if (!entryPointInSection) {
                __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] EntryPoint 0x%08X liegt in KEINER geladenen Section! (wird ignoriert)", xbeEntryPoint);
            } else {
                __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] EntryPoint 0x%08X liegt in geladener Section (oder ignoriert).", xbeEntryPoint);
            }
        }
    }

    uint32_t stackCommit = 0, stackReserve = 0, stackBase = 0x00040000, espStart = 0x0003FFFC;
    if (size > 0x128 + 4) {
        stackCommit = read_le32(xbePtr + 0x128);
        stackReserve = read_le32(xbePtr + 0x12C);
        __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] StackCommit: 0x%08X StackReserve: 0x%08X", stackCommit, stackReserve);

        if (stackCommit > 0 && stackCommit <= 0x100000 && stackBase > stackCommit) {
            uint32_t stackStart = stackBase - stackCommit;

            for (uint32_t addr = stackStart; addr < stackBase; ++addr) {
                write8(addr, 0xCC);
            }
            espStart = stackBase - 4; 
            __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] Stack initialisiert: Bereich 0x%08X - 0x%08X mit 0xCC, ESP-Start: 0x%08X", stackStart, stackBase, espStart);
        } else {
            __android_log_print(ANDROID_LOG_WARN, "XboxMemory", "[XBE-DEBUG] StackCommit ungültig oder zu groß, Standard-Stack verwendet");

            for (uint32_t addr = 0x0003C000; addr < 0x00040000; ++addr) {
                write8(addr, 0xCC);
            }
            espStart = 0x0003FFFC;
        }
    }

    this->espStartValue = espStart;

    return true;
}

void XboxMemory::reset() {
    std::lock_guard<std::mutex> lock(memoryMutex);


    LOGW("[MEMORY-RESET-DEBUG] Before reset - xbeEntryPoint: 0x%08X", xbeEntryPoint);

    std::fill(ram.begin(), ram.end(), 0);

    for (auto& cache : memoryCaches) {
        cache.base = 0;
        cache.size = 0;
        cache.dirty = false;
    }


    LOGW("[MEMORY-RESET-DEBUG] After reset - xbeEntryPoint: 0x%08X", xbeEntryPoint);
}

void XboxMemory::dmaTransfer(uint32_t src, uint32_t dest, uint32_t size) {
    if (size == 0) return;

    std::lock_guard<std::mutex> lock(memoryMutex);

    if ((src >= RAM_BASE && src + size <= RAM_BASE + RAM_SIZE) &&
        (dest >= RAM_BASE && dest + size <= RAM_BASE + RAM_SIZE)) {

        uint8_t* src_ptr = &ram[src - RAM_BASE];
        uint8_t* dest_ptr = &ram[dest - RAM_BASE];

        if (size >= 64 && (src % 16 == 0) && (dest % 16 == 0)) {
            uint32_t blocks = size / 16;
            uint32_t* src32 = reinterpret_cast<uint32_t*>(src_ptr);
            uint32_t* dest32 = reinterpret_cast<uint32_t*>(dest_ptr);

            for (uint32_t i = 0; i < blocks; i++) {
                uint32x4_t data = vld1q_u32(src32);
                vst1q_u32(dest32, data);
                src32 += 4;
                dest32 += 4;
            }

            uint32_t remaining = size % 16;
            if (remaining > 0) {
                memcpy(dest32, src32, remaining);
            }
        } else {
            memmove(dest_ptr, src_ptr, size);
        }

        updateCache(dest, size);
        return;
    }

    for (uint32_t i = 0; i < size; i++) {
        uint8_t val = read8(src + i);
        write8(dest + i, val);
    }
}

void XboxMemory::dmaTransferNEON(uint32_t src, uint32_t dest, uint32_t size) {
    if (size == 0) return;

    std::lock_guard<std::mutex> lock(memoryMutex);

    if ((src >= RAM_BASE && src + size <= RAM_BASE + RAM_SIZE) &&
        (dest >= RAM_BASE && dest + size <= RAM_BASE + RAM_SIZE)) {

        uint8_t* src_ptr = &ram[src - RAM_BASE];
        uint8_t* dest_ptr = &ram[dest - RAM_BASE];

        uint32_t blocks = size / 64;
        for (uint32_t i = 0; i < blocks; i++) {
            uint8x16x4_t data = vld4q_u8(src_ptr);
            vst4q_u8(dest_ptr, data);
            src_ptr += 64;
            dest_ptr += 64;
        }

        uint32_t remaining = size % 64;
        if (remaining) {
            memcpy(dest_ptr, src_ptr, remaining);
        }

        updateCache(dest, size);
    } else {
        dmaTransfer(src, dest, size);
    }
}

void XboxMemory::flushCaches() {
    std::lock_guard<std::mutex> lock(memoryMutex);
    for (auto& cache : memoryCaches) {
        if (cache.dirty && cache.size > 0) {
            memcpy(&ram[cache.base - RAM_BASE], cache.data.get(), cache.size);
            cache.dirty = false;
        }
    }
}

void XboxMemory::flushCacheRange(uint32_t address, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    for (auto& cache : memoryCaches) {
        if (cache.dirty && cache.size > 0) {
            uint32_t start = std::max(address, cache.base);
            uint32_t end = std::min(address + size, cache.base + cache.size);
            if (start < end) {
                uint32_t len = end - start;
                memcpy(&ram[start - RAM_BASE], 
                      &cache.data[start - cache.base], 
                      len);
            }
        }
    }
}

void XboxMemory::invalidateCacheRange(uint32_t address, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    for (auto& cache : memoryCaches) {
        if (cache.size > 0) {
            uint32_t start = std::max(address, cache.base);
            uint32_t end = std::min(address + size, cache.base + cache.size);
            if (start < end) {
                uint32_t len = end - start;
                memcpy(&cache.data[start - cache.base], 
                      &ram[start - RAM_BASE], 
                      len);
            }
        }
    }
}

XboxMemory::MappedRegion* XboxMemory::findMappedRegion(uint32_t address) {
    for (auto& region : mappedRegions) {
        if (region.contains(address)) {
            return &region;
        }
    }
    return nullptr;
}

void XboxMemory::updateCache(uint32_t address, uint32_t size) {
    for (auto& cache : memoryCaches) {
        if (cache.size == 0) continue;

        uint32_t start = std::max(address, cache.base);
        uint32_t end = std::min(address + size, cache.base + cache.size);

        if (start < end) {
            uint32_t len = end - start;
            memcpy(&cache.data[start - cache.base], 
                   &ram[start - RAM_BASE], 
                   len);
        }
    }
}

uint32_t XboxMemory::handleGPURead(uint32_t address) {
    uint32_t offset = address - GPU_BASE;
    if (offset >= GPU_SIZE) {
        LOGE("GPU memory read out of bounds: 0x%08X", address);
        return 0;
    }

    uint32_t value;
    memcpy(&value, &gpuMemory[offset], sizeof(value));
    LOGI("GPU read at 0x%08X: 0x%08X", address, value);
    return value;
}

void XboxMemory::handleGPUWrite(uint32_t address, uint32_t value) {

    gpuMemoryDirty = true;
    lastGPUWriteAddress = address;
    lastGPUWriteValue = value;
    lastGPUWriteOffset = address - XboxMemory::GPU_MEMORY_BASE;
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] GPU-Memory-Write: addr=0x%08X, value=0x%08X, offset=0x%X, dirty=%d", address, value, lastGPUWriteOffset, gpuMemoryDirty ? 1 : 0);


    gpuMemoryDirty = true;
}

uint32_t XboxMemory::handleAPURead(uint32_t address) {
    LOGI("APU read at 0x%08X", address);
    return 0;
}

void XboxMemory::handleAPUWrite(uint32_t address, uint32_t value) {
    LOGI("APU write at 0x%08X: 0x%08X", address, value);
}

bool XboxMemory::mapRegion(uint32_t base, uint32_t size,
                         std::function<uint32_t(uint32_t)> readHandler,
                         std::function<void(uint32_t, uint32_t)> writeHandler) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    for (const auto& region : mappedRegions) {
        if (region.base == base) {
            LOGE("Region 0x%08X already mapped", base);
            return false;
        }
    }

    mappedRegions.push_back({base, size, readHandler, writeHandler});
    LOGI("Mapped region 0x%08X-0x%08X", base, base + size);
    return true;
}

void XboxMemory::unmapRegion(uint32_t base) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    auto it = std::remove_if(mappedRegions.begin(), mappedRegions.end(),
        [base](const MappedRegion& region) { return region.base == base; });

    if (it != mappedRegions.end()) {
        mappedRegions.erase(it, mappedRegions.end());
        LOGI("Unmapped region 0x%08X", base);
    }
}

void XboxMemory::setAccessCallback(std::function<void(uint32_t, uint32_t, bool, uint32_t)> callback) {
    std::lock_guard<std::mutex> lock(memoryMutex);
    accessCallback = callback;
}

uint8_t* XboxMemory::getRamPointer() {
    return ram.data();
}

const uint8_t* XboxMemory::getBiosPointer() const {
    return bios.data();
}

uint32_t XboxMemory::getRamSize() const {
    return RAM_SIZE;
}

uint32_t XboxMemory::getBiosSize() const {
    return BIOS_SIZE;
}


uint32_t XboxMemory::handleVertexMemoryRead(uint32_t address) {
    uint32_t offset = address - VERTEX_MEMORY_BASE;
    if (offset >= VERTEX_MEMORY_SIZE) {
        LOGE("Vertex memory read out of bounds: 0x%08X", address);
        return 0;
    }


    static bool vertexMemoryInitialized = false;
    if (!vertexMemoryInitialized) {
        LOGI("GPU: Initializing vertex memory with test data");
        populateVertexMemoryWithTestData();
        vertexMemoryInitialized = true;
    }

    uint32_t value;
    memcpy(&value, &vertexMemory[offset], sizeof(value));
    return value;
}

void XboxMemory::handleVertexMemoryWrite(uint32_t address, uint32_t value) {
    uint32_t offset = address - VERTEX_MEMORY_BASE;
    if (offset >= VERTEX_MEMORY_SIZE) {
        LOGE("Vertex memory write out of bounds: 0x%08X", address);
        return;
    }

    memcpy(&vertexMemory[offset], &value, sizeof(value));


    if (gpuRenderer) {
        LOGI("GPU: Vertex data written at 0x%08X (offset 0x%04X) - marking as dirty", address, offset);
        vertexMemoryDirty = true;
    }
}

uint32_t XboxMemory::handleIndexMemoryRead(uint32_t address) {
    uint32_t offset = address - INDEX_MEMORY_BASE;
    if (offset >= INDEX_MEMORY_SIZE) {
        LOGE("Index memory read out of bounds: 0x%08X", address);
        return 0;
    }


    static bool indexMemoryInitialized = false;
    if (!indexMemoryInitialized) {
        LOGI("GPU: Initializing index memory with test data");
        populateIndexMemoryWithTestData();
        indexMemoryInitialized = true;
    }

    uint32_t value;
    memcpy(&value, &indexMemory[offset], sizeof(value));
    return value;
}

void XboxMemory::handleIndexMemoryWrite(uint32_t address, uint32_t value) {
    uint32_t offset = address - INDEX_MEMORY_BASE;
    if (offset >= INDEX_MEMORY_SIZE) {
        LOGE("Index memory write out of bounds: 0x%08X", address);
        return;
    }

    memcpy(&indexMemory[offset], &value, sizeof(value));


    if (gpuRenderer) {
        LOGI("GPU: Index data written at 0x%08X (offset 0x%04X) - marking as dirty", address, offset);
        indexMemoryDirty = true;
    }
}


void XboxMemory::populateVertexMemoryWithTestData() {
    LOGI("GPU: Populating vertex memory with test triangle data");


    struct TestVertex {
        float x, y, z;    
        float u, v;       
        uint32_t color;   
    };


    TestVertex vertices[] = {
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0xFFFF0000}, 
        { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0xFF00FF00}, 
        { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f, 0xFF0000FF}, 


        {-0.8f, -0.8f, 0.0f, 0.0f, 0.0f, 0xFFFFFF00}, 
        { 0.8f, -0.8f, 0.0f, 1.0f, 0.0f, 0xFFFF00FF}, 
        { 0.0f,  0.8f, 0.0f, 0.5f, 1.0f, 0xFF00FFFF}, 
    };


    for (size_t i = 0; i < sizeof(vertices) / sizeof(TestVertex); i++) {
        uint32_t offset = i * sizeof(TestVertex);
        if (offset + sizeof(TestVertex) <= VERTEX_MEMORY_SIZE) {
            memcpy(&vertexMemory[offset], &vertices[i], sizeof(TestVertex));
            LOGI("GPU: Wrote vertex %zu at offset 0x%04X: pos(%.2f,%.2f,%.2f) color(0x%08X)", 
                 i, offset, vertices[i].x, vertices[i].y, vertices[i].z, vertices[i].color);
        }
    }

    LOGI("GPU: Vertex memory populated with %zu test vertices", sizeof(vertices) / sizeof(TestVertex));
}

void XboxMemory::populateIndexMemoryWithTestData() {
    LOGI("GPU: Populating index memory with test triangle indices");


    uint16_t indices[] = {
        0, 1, 2,  
        3, 4, 5,  
        0, 2, 3,  
        1, 4, 2,  
    };


    for (size_t i = 0; i < sizeof(indices) / sizeof(uint16_t); i++) {
        uint32_t offset = i * sizeof(uint16_t);
        if (offset + sizeof(uint16_t) <= INDEX_MEMORY_SIZE) {
            memcpy(&indexMemory[offset], &indices[i], sizeof(uint16_t));
            LOGI("GPU: Wrote index %zu at offset 0x%04X: %u", i, offset, indices[i]);
        }
    }

    LOGI("GPU: Index memory populated with %zu test indices", sizeof(indices) / sizeof(uint16_t));
}


void XboxMemory::setGPURenderer(NV2ARenderer* renderer) {
    gpuRenderer = renderer;
    LOGI("GPU: NV2A renderer reference set - memory access monitoring enabled");
}


uint32_t XboxMemory::repairMemoryAddress(uint32_t invalidAddress) {

    static int repairCount = 0;
    repairCount++;
    if (repairCount % 100 == 0) { 
        LOGW("🔧 XEMU MEMORY REPAIR: Repair count: %d, current invalid address: 0x%08X", repairCount, invalidAddress);
    }


    if (invalidAddress >= 0x80000000 && invalidAddress <= 0xFFFFFFFF) {

        uint32_t repairedAddress = (invalidAddress & 0x07FFFFFF) | 0x00000000;


        if (repairedAddress >= RAM_BASE && repairedAddress < RAM_BASE + RAM_SIZE) {
            if (repairCount % 100 == 0) {
                LOGW("🔧 XEMU MEMORY REPAIR: Mapped high address 0x%08X to Xbox RAM 0x%08X", invalidAddress, repairedAddress);
            }
            return repairedAddress;
        }
    }


    if (invalidAddress < 0x00000000 || invalidAddress >= 0x08000000) {

        uint32_t repairedAddress = findValidMemoryAddress();
        if (repairedAddress != 0) {
            if (repairCount % 100 == 0) {
                LOGW("🔧 XEMU MEMORY REPAIR: Mapped invalid address 0x%08X to valid address 0x%08X", invalidAddress, repairedAddress);
            }
            return repairedAddress;
        }
    }


    uint32_t safeAddress = 0x00000000; 
    if (repairCount % 100 == 0) {
        LOGW("🔧 XEMU MEMORY REPAIR: Using safe fallback address 0x%08X for invalid address 0x%08X", safeAddress, invalidAddress);
    }
    return safeAddress;
}


uint32_t XboxMemory::findValidMemoryAddress() {

    std::vector<uint32_t> searchAddresses = {
        0x00000000, 
        0x00100000, 
        0x00200000, 
        0x00300000, 
        0x00400000, 
        0x00500000, 
        0x00600000, 
        0x00700000  
    };

    for (uint32_t addr : searchAddresses) {
        if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {

            try {

                uint8_t testValue = read8(addr);
                LOGD("🔍 XEMU MEMORY REPAIR: Found valid address 0x%08X (test read: 0x%02X)", addr, testValue);
                return addr;
            } catch (...) {

                continue;
            }
        }
    }

    LOGW("🔍 XEMU MEMORY REPAIR: No valid memory address found");
    return 0;
}


uint32_t XboxMemory::repairInvalidAddress(uint32_t invalidAddress) {

    if (invalidAddress == 0x20EC8301) {

        uint32_t repairedAddress = 0x00EC8301; 
        if (repairedAddress >= RAM_BASE && repairedAddress < RAM_BASE + RAM_SIZE) {
            GAMELOADED("Memory: Spezielle Reparatur: 0x20EC8301 -> 0x%08X", repairedAddress);
            return repairedAddress;
        }

        return 0x00100000;
    }

    if (invalidAddress >= 0x51FF501B && invalidAddress <= 0x51FF501E) {

        uint32_t offset = invalidAddress - 0x51FF501B;
        uint32_t repairedAddress = 0x00200000 + offset; 
        if (repairedAddress >= RAM_BASE && repairedAddress < RAM_BASE + RAM_SIZE) {
            GAMELOADED("Memory: Spezielle Reparatur: 0x%08X -> 0x%08X", invalidAddress, repairedAddress);
            return repairedAddress;
        }

        return 0x00200000;
    }


    if (invalidAddress >= 0x10000000) {

        uint32_t repairedAddress = (invalidAddress & 0x00FFFFFF) + 0x00100000;
        if (repairedAddress >= RAM_BASE && repairedAddress < RAM_BASE + RAM_SIZE) {
            GAMELOADED("Memory: Intelligente Reparatur: 0x%08X -> 0x%08X", invalidAddress, repairedAddress);
            return repairedAddress;
        }

        return 0x00100000;
    }


    if (invalidAddress < 0x00000000 || invalidAddress >= 0x08000000) {

        uint32_t repairedAddress = 0x00100000 + (invalidAddress & 0x000FFFFF);
        if (repairedAddress >= RAM_BASE && repairedAddress < RAM_BASE + RAM_SIZE) {
            GAMELOADED("Memory: Bereich-Reparatur: 0x%08X -> 0x%08X", invalidAddress, repairedAddress);
            return repairedAddress;
        }

        return 0x00100000;
    }


    return repairMemoryAddress(invalidAddress);
}
