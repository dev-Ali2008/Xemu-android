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
#include <fstream>

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


    LOGI("Memory: Starting simplified memory mapping...");

    mapRegion(GPU_BASE, GPU_SIZE,
        [this](uint32_t addr) { return handleGPURead(addr); },
        [this](uint32_t addr, uint32_t val) { handleGPUWrite(addr, val); });
    LOGI("Memory: GPU region mapped");

    mapRegion(APU_BASE, APU_SIZE,
        [this](uint32_t addr) { return handleAPURead(addr); },
        [this](uint32_t addr, uint32_t val) { handleAPUWrite(addr, val); });
    LOGI("Memory: APU region mapped");


    mapRegion(VERTEX_MEMORY_BASE, VERTEX_MEMORY_SIZE,
        [this](uint32_t addr) { return handleVertexMemoryRead(addr); },
        [this](uint32_t addr, uint32_t val) { handleVertexMemoryWrite(addr, val); });
    LOGI("Memory: Vertex memory region mapped");

    mapRegion(INDEX_MEMORY_BASE, INDEX_MEMORY_SIZE,
        [this](uint32_t addr) { return handleIndexMemoryRead(addr); },
        [this](uint32_t addr, uint32_t val) { handleIndexMemoryWrite(addr, val); });
    LOGI("Memory: Index memory region mapped");


    std::fill(ram.begin(), ram.end(), 0x00);
    std::fill(bios.begin(), bios.end(), 0x00);
    std::fill(vertexMemory.begin(), vertexMemory.end(), 0x00);
    std::fill(indexMemory.begin(), indexMemory.end(), 0x00);
    std::fill(gpuMemory.begin(), gpuMemory.end(), 0x00);


    xbeEntryPoint = 0x00100000;
    LOGI("Memory: Entry point initialized to 0x%08X", xbeEntryPoint);


    LOGI("Memory: Initializing stack area...");


    if (0x0003C000 >= RAM_BASE && 0x00040000 <= RAM_BASE + RAM_SIZE) {
        uint32_t stackStart = 0x0003C000 - RAM_BASE;
        uint32_t stackSize = 0x00040000 - 0x0003C000;
        std::fill(ram.begin() + stackStart, ram.begin() + stackStart + stackSize, 0x00);
        LOGI("Memory: Stack area initialized directly in RAM");
    } else {
        LOGW("Memory: Stack area outside RAM range, skipping initialization");
    }

    LOGI("Memory: Xbox memory regions initialized successfully");
    LOGI("Memory: Constructor completed - ready for use");
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
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            return region.readHandler(address) & 0xFF;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        return ram[address - RAM_BASE];
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) {
        return bios[address - BIOS_BASE];
    }


    LOGW("Read8 from unmapped address 0x%08X, returning 0x00", address);
    return 0x00;
}

uint16_t XboxMemory::read16(uint32_t address) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (address & 1) {
        LOGW("Unaligned read16 at 0x%08X", address);

        uint32_t repairedAddress = repairMemoryAddress(address);
        if (repairedAddress != address) {
            LOGI("Repaired address 0x%08X -> 0x%08X", address, repairedAddress);
            return read16(repairedAddress);
        }
    }


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            return region.readHandler(address) & 0xFFFF;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 1) {
        uint16_t value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 1) {
        uint16_t value;
        memcpy(&value, &bios[address - BIOS_BASE], sizeof(value));
        return value;
    }


    return 0x0000; 
}

uint32_t XboxMemory::read32(uint32_t address) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (address & 3) {
        static int unalignedCount = 0;
        if (++unalignedCount % 1000 == 0) { 
            LOGW("Unaligned read32 at 0x%08X (count: %d)", address, unalignedCount);
        }

        uint32_t repairedAddress = repairMemoryAddress(address);
        if (repairedAddress != address) {
            LOGI("Repaired address 0x%08X -> 0x%08X", address, repairedAddress);
            return read32(repairedAddress);
        }
    }


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            return region.readHandler(address);
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        uint32_t value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 3) {
        uint32_t value;
        memcpy(&value, &bios[address - BIOS_BASE], sizeof(value));
        return value;
    }


    return 0x00000000; 
}

uint64_t XboxMemory::read64(uint32_t address) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (address & 7) {
        LOGW("Unaligned read64 at 0x%08X", address);

        uint32_t repairedAddress = repairMemoryAddress(address);
        if (repairedAddress != address) {
            LOGI("Repaired address 0x%08X -> 0x%08X", address, repairedAddress);
            return read64(repairedAddress);
        }
    }


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            uint32_t low = region.readHandler(address);
            uint32_t high = region.readHandler(address + 4);
            return ((uint64_t)high << 32) | low;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 7) {
        uint64_t value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 7) {
        uint64_t value;
        memcpy(&value, &bios[address - BIOS_BASE], sizeof(value));
        return value;
    }


    return 0x0000000000000000; 
}

float XboxMemory::readFloat(uint32_t address) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            uint32_t value = region.readHandler(address);
            float result;
            memcpy(&result, &value, sizeof(result));
            return result;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        float value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 3) {
        float value;
        memcpy(&value, &bios[address - BIOS_BASE], sizeof(value));
        return value;
    }

    LOGW("ReadFloat from unmapped address 0x%08X - returning safe default", address);

    return 0.0f; 
}

uint32x4_t XboxMemory::read128(uint32_t address) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            uint32_t val0 = region.readHandler(address);
            uint32_t val1 = region.readHandler(address + 4);
            uint32_t val2 = region.readHandler(address + 8);
            uint32_t val3 = region.readHandler(address + 12);
            uint32x4_t result = {val0, val1, val2, val3};
            return result;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 15) {
        uint32x4_t value;
        memcpy(&value, &ram[address - RAM_BASE], sizeof(value));
        return value;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 15) {
        uint32x4_t value;
        memcpy(&value, &bios[address - BIOS_BASE], sizeof(value));
        return value;
    }

    LOGW("Read128 from unmapped address 0x%08X - returning safe default", address);

    uint32x4_t result = {0, 0, 0, 0}; 
    return result;
}

void XboxMemory::write8(uint32_t address, uint8_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            region.writeHandler(address, value);
            return;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE) {
        ram[address - RAM_BASE] = value;
        updateCache(address, 1);
        return;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) {
        bios[address - BIOS_BASE] = value;
        return;
    }


    return; 
}

void XboxMemory::write16(uint32_t address, uint16_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (address & 1) {
        LOGW("Unaligned write16 at 0x%08X", address);

        uint32_t repairedAddress = repairMemoryAddress(address);
        if (repairedAddress != address) {
            LOGI("Repaired address 0x%08X -> 0x%08X", address, repairedAddress);
            write16(repairedAddress, value);
            return;
        }
    }


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            region.writeHandler(address, value);
            return;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 1) {
        memcpy(&ram[address - RAM_BASE], &value, sizeof(value));
        updateCache(address, 2);
        return;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 1) {
        memcpy(&bios[address - BIOS_BASE], &value, sizeof(value));
        return;
    }


    return; 
}

void XboxMemory::write32(uint32_t address, uint32_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (address & 3) {
        LOGW("Unaligned write32 at 0x%08X", address);

        uint32_t repairedAddress = repairMemoryAddress(address);
        if (repairedAddress != address) {
            LOGI("Repaired address 0x%08X -> 0x%08X", address, repairedAddress);
            write32(repairedAddress, value);
            return;
        }
    }


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            region.writeHandler(address, value);
            return;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        memcpy(&ram[address - RAM_BASE], &value, sizeof(value));
        updateCache(address, 4);
        return;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 3) {
        memcpy(&bios[address - BIOS_BASE], &value, sizeof(value));
        return;
    }


    return; 
}

void XboxMemory::write64(uint32_t address, uint64_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (address & 7) {
        LOGW("Unaligned write64 at 0x%08X", address);

        uint32_t repairedAddress = repairMemoryAddress(address);
        if (repairedAddress != address) {
            LOGI("Repaired address 0x%08X -> 0x%08X", address, repairedAddress);
            write64(repairedAddress, value);
            return;
        }
    }


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            region.writeHandler(address, value & 0xFFFFFFFF);
            region.writeHandler(address + 4, (value >> 32) & 0xFFFFFFFF);
            return;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 7) {
        memcpy(&ram[address - RAM_BASE], &value, sizeof(value));
        updateCache(address, 8);
        return;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 7) {
        memcpy(&bios[address - BIOS_BASE], &value, sizeof(value));
        return;
    }


    return; 
}

void XboxMemory::writeFloat(uint32_t address, float value) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            uint32_t intValue;
            memcpy(&intValue, &value, sizeof(intValue));
            region.writeHandler(address, intValue);
            return;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 3) {
        memcpy(&ram[address - RAM_BASE], &value, sizeof(value));
        updateCache(address, 4);
        return;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 3) {
        memcpy(&bios[address - BIOS_BASE], &value, sizeof(value));
        return;
    }

    LOGE("WriteFloat to unmapped address 0x%08X", address);
    throw std::runtime_error("Memory access violation");
}

void XboxMemory::write128(uint32_t address, uint32x4_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if (region.contains(address)) {
            uint32_t vals[4];
            memcpy(vals, &value, sizeof(vals));
            region.writeHandler(address, vals[0]);
            region.writeHandler(address + 4, vals[1]);
            region.writeHandler(address + 8, vals[2]);
            region.writeHandler(address + 12, vals[3]);
            return;
        }
    }


    if (address >= RAM_BASE && address < RAM_BASE + RAM_SIZE - 15) {
        memcpy(&ram[address - RAM_BASE], &value, sizeof(value));
        updateCache(address, 16);
        return;
    }


    if (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE - 15) {
        memcpy(&bios[address - BIOS_BASE], &value, sizeof(value));
        return;
    }

    LOGE("Write128 to unmapped address 0x%08X", address);
    throw std::runtime_error("Memory access violation");
}

bool XboxMemory::loadBios(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        LOGE("Failed to open BIOS file: %s", filename.c_str());
        return false;
    }

    file.read(reinterpret_cast<char*>(bios.data()), BIOS_SIZE);
    size_t bytesRead = file.gcount();

    if (bytesRead < BIOS_SIZE) {

        std::fill(bios.begin() + bytesRead, bios.end(), 0);
    }

    LOGI("BIOS loaded successfully: %zd bytes", bytesRead); 
    return true;
}


static uint32_t read_le32(const uint8_t* ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

bool XboxMemory::loadXbeFromBuffer(const void* data, size_t size, size_t xbeHeaderOffset) {




    LOGI("[XBE-DEBUG] loadXbeFromBuffer called - HEADER PARSING ONLY (no section loading)");
    LOGI("[XBE-DEBUG] size=%zu bytes", size);

    if (!data || size == 0) {
        LOGE("[XBE-DEBUG] XBE buffer is empty");
        return false;
    }
    const uint8_t* fileData = reinterpret_cast<const uint8_t*>(data);



    const uint8_t* xbePtr = fileData;  
    size_t xbeSize = size;              

    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] XBE Header wird direkt am Anfang gesucht (Offset 0)");

    char hexDump[3*32+1] = {0};
    for (int i = 0; i < 32 && i < (int)xbeSize; ++i) {
        sprintf(hexDump + i*3, "%02X ", xbePtr[i]);
    }
    LOGXBE("[XBE-HEADER] Erste 32 Bytes: %s", hexDump);
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] XBE header found at offset 0x%zX, XBE size: %zu bytes", xbeHeaderOffset, xbeSize);

    uint32_t magic = read_le32(xbePtr);
    if (magic != 0x48454258) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] FATAL ERROR - Invalid XBE magic: 0x%08X (expected 0x48454258)", magic);


        bool magicFound = false;
        for (uint32_t offset = 0; offset < xbeSize - 4; offset++) {
            if (read_le32(xbePtr + offset) == 0x48454258) {
                __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] Echter XBE-Magic gefunden bei Offset 0x%08X", offset);
                xbePtr = xbePtr + offset; 
                magicFound = true;
                break;
            }
        }

        if (!magicFound) {
            __android_log_print(ANDROID_LOG_ERROR, "XboxMemory", "[XBE-DEBUG] Kein gültiger XBE-Magic gefunden!");
        return false;
    }
    }


        uint32_t imageBase = read_le32(xbePtr + 0x104);
    uint32_t entryPointRVA = read_le32(xbePtr + 0x10C);
    uint32_t actualEntryPoint = imageBase + entryPointRVA;

    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] KORREKTE XBE-Header-Werte:");
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] Magic: 0x%08X", magic);
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] ImageBase: 0x%08X", imageBase);
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] EntryPoint RVA: 0x%08X", entryPointRVA);
    __android_log_print(ANDROID_LOG_INFO, "XboxMemory", "[XBE-DEBUG] Actual EntryPoint VA: 0x%08X", actualEntryPoint);


    this->xbeEntryPoint = actualEntryPoint;
    LOGI("[XBE-DEBUG] Entry point set to: 0x%08X", this->xbeEntryPoint);




    LOGI("[XBE-DEBUG] Skipping section loading - kernel already loaded sections correctly");
    LOGI("[XBE-DEBUG] XBE header parsing completed successfully");

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
                memcpy(dest_ptr + (blocks * 16), src_ptr + (blocks * 16), remaining);
            }
        } else {
            memcpy(dest_ptr, src_ptr, size);
        }

        updateCache(dest, size);
    } else {
        dmaTransfer(src, dest, size);
    }
}

void XboxMemory::updateCache(uint32_t address, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    for (auto& cache : memoryCaches) {
        if (cache.base <= address && address + size <= cache.base + cache.size) {
            cache.dirty = true;
            break;
        }
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
            uint32_t start = (address > cache.base) ? address : cache.base;
            uint32_t end = ((address + size) < (cache.base + cache.size)) ? (address + size) : (cache.base + cache.size);
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
        if (cache.base <= address && address + size <= cache.base + cache.size) {
            uint32_t start = std::max(address, cache.base);
            uint32_t end = std::min(address + size, cache.base + cache.size);
            uint32_t len = end - start;

            if (len > 0) {
                memcpy(&cache.data[start - cache.base], 
                      &ram[start - RAM_BASE], 
                      len);
            }
        }
    }
}

void XboxMemory::prefetchMemory(uint32_t address, uint32_t size) {

    for (uint32_t i = 0; i < size; i += 64) {
        read32(address + i);
    }
}


bool XboxMemory::mapRegion(uint32_t base, uint32_t size, 
                          std::function<uint32_t(uint32_t)> readHandler,
                          std::function<void(uint32_t, uint32_t)> writeHandler) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    for (const auto& region : mappedRegions) {
        if ((base < region.base + region.size) && (base + size > region.base)) {
            return false;
        }
    }

    mappedRegions.push_back({base, size, readHandler, writeHandler});
    LOGI("Mapped region 0x%08X-0x%08X", base, base + size);
    return true;
}

void XboxMemory::unmapRegion(uint32_t base) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    auto it = std::find_if(mappedRegions.begin(), mappedRegions.end(),
                          [base](const MappedRegion& region) {
                              return region.base == base;
                          });

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
    if (address < VERTEX_MEMORY_BASE || address >= VERTEX_MEMORY_BASE + VERTEX_MEMORY_SIZE) {
        return 0;
    }

    uint32_t offset = address - VERTEX_MEMORY_BASE;

    uint32_t value;
    memcpy(&value, &vertexMemory[offset], sizeof(value));
    return value;
}

void XboxMemory::handleVertexMemoryWrite(uint32_t address, uint32_t value) {
    if (address < VERTEX_MEMORY_BASE || address >= VERTEX_MEMORY_BASE + VERTEX_MEMORY_SIZE) {
        return;
    }

    uint32_t offset = address - VERTEX_MEMORY_BASE;
    memcpy(&vertexMemory[offset], &value, sizeof(value));


    if (gpuRenderer) {
        LOGI("GPU: Vertex data written at 0x%08X (offset 0x%04X) - marking as dirty", address, offset);
        vertexMemoryDirty = true;
    }
}

uint32_t XboxMemory::handleIndexMemoryRead(uint32_t address) {
    if (address < INDEX_MEMORY_BASE || address >= INDEX_MEMORY_BASE + INDEX_MEMORY_SIZE) {
        return 0;
    }

    uint32_t offset = address - INDEX_MEMORY_BASE;

    uint32_t value;
    memcpy(&value, &indexMemory[offset], sizeof(value));
    return value;
}

void XboxMemory::handleIndexMemoryWrite(uint32_t address, uint32_t value) {
    if (address < INDEX_MEMORY_BASE || address >= INDEX_MEMORY_BASE + INDEX_MEMORY_SIZE) {
        return;
    }

    uint32_t offset = address - INDEX_MEMORY_BASE;
    memcpy(&indexMemory[offset], &value, sizeof(value));


    if (gpuRenderer) {
        LOGI("GPU: Index data written at 0x%08X (offset 0x%04X) - marking as dirty", address, offset);
        indexMemoryDirty = true;
    }
}


void XboxMemory::setGPURenderer(NV2ARenderer* renderer) {
    gpuRenderer = renderer;
    LOGI("GPU: NV2A renderer reference set - memory access monitoring enabled");
}

uint32_t XboxMemory::repairMemoryAddress(uint32_t invalidAddress) {

    if (invalidAddress == 0) {
        LOGW("🔍 XEMU MEMORY REPAIR: Invalid address 0x00000000 - using safe default");
        return 0x00100000; 
    }

    if (invalidAddress >= 0x08000000) {
        LOGW("🔍 XEMU MEMORY REPAIR: Address 0x%08X outside RAM - clamping to 0x07FFFFFF", invalidAddress);
        return 0x07FFFFFF; 
    }

    if (invalidAddress < 0x00010000) {
        LOGW("🔍 XEMU MEMORY REPAIR: Address 0x%08X in low memory - moving to safe area", invalidAddress);
        return 0x00100000; 
    }


    return invalidAddress;
}

uint32_t XboxMemory::findValidMemoryAddress() {


    for (uint32_t addr = 0x00100000; addr < 0x07FFFFFF - 0x1000; addr += 0x1000) {
        bool conflict = false;
        for (const auto& region : mappedRegions) {
            if ((addr < region.base + region.size) && (addr + 0x1000 > region.base)) {
                conflict = true;
                break;
            }
        }
        if (!conflict) {
            LOGW("🔍 XEMU MEMORY REPAIR: Found safe address 0x%08X", addr);
            return addr;
        }
    }

    LOGW("🔍 XEMU MEMORY REPAIR: No valid memory address found");
    return 0;
}



uint32_t XboxMemory::handleGPURead(uint32_t address) {
    if (address < GPU_BASE || address >= GPU_BASE + GPU_SIZE) {
        return 0;
    }

    uint32_t offset = address - GPU_BASE;

    uint32_t value;
    memcpy(&value, &gpuMemory[offset], sizeof(value));
    LOGI("GPU read at 0x%08X: 0x%08X", address, value);
    return value;
}

void XboxMemory::handleGPUWrite(uint32_t address, uint32_t value) {
    if (address < GPU_BASE || address >= GPU_BASE + GPU_SIZE) {
        return;
    }

    uint32_t offset = address - GPU_BASE;
    memcpy(&gpuMemory[offset], &value, sizeof(value));

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
