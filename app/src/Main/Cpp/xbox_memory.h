#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <mutex>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <android/log.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

class NV2ARenderer;

class XboxMemory {
public:

    static constexpr uint32_t RAM_BASE = 0x00000000;
    static constexpr uint32_t RAM_SIZE = 256 * 1024 * 1024; 

    static constexpr uint32_t BIOS_BASE = 0xFF000000;
    static constexpr uint32_t BIOS_SIZE = 1 * 1024 * 1024; 

    static constexpr uint32_t GPU_BASE = 0xFD000000;
    static constexpr uint32_t GPU_SIZE = 0x01000000; 

    static constexpr uint32_t APU_BASE = 0xFE000000;
    static constexpr uint32_t APU_SIZE = 0x01000000; 


    static constexpr uint32_t VERTEX_MEMORY_BASE = 0xFC000000;
    static constexpr uint32_t VERTEX_MEMORY_SIZE = 16 * 1024 * 1024; 

    static constexpr uint32_t INDEX_MEMORY_BASE = 0xFB000000;
    static constexpr uint32_t INDEX_MEMORY_SIZE = 16 * 1024 * 1024; 

    static constexpr uint32_t PCI_BASE = 0x80000000;
    static constexpr uint32_t PCI_SIZE = 0x01000000; 


    static constexpr uint32_t PROTECTED_BASE = 0x00000000;
    static constexpr uint32_t PROTECTED_SIZE = 64 * 1024 * 1024;


    static constexpr uint32_t VM_BASE = 0x40000000;
    static constexpr uint32_t VM_SIZE = 64 * 1024 * 1024;

    static constexpr uint32_t CACHE_BLOCK_SIZE = 64 * 1024; 
    static constexpr uint32_t CACHE_BLOCK_COUNT = 8;


    static constexpr uint32_t VM_PAGE_SIZE = 4 * 1024;
    static constexpr uint32_t VM_PAGE_MASK = ~(VM_PAGE_SIZE - 1);

    static constexpr uint32_t GPU_MEMORY_BASE = 0xFD000000;
    static constexpr uint32_t FB_SIZE = 1280 * 720; 

    XboxMemory();
    ~XboxMemory();


    uint8_t read8(uint32_t address);
    uint16_t read16(uint32_t address);
    uint32_t read32(uint32_t address);
    uint64_t read64(uint32_t address);
    float readFloat(uint32_t address);
    double readDouble(uint32_t address);
#ifdef __ARM_NEON
    uint32x4_t read128(uint32_t address);
#endif

    void write8(uint32_t address, uint8_t value);
    void write16(uint32_t address, uint16_t value);
    void write32(uint32_t address, uint32_t value);
    void write64(uint32_t address, uint64_t value);
    void writeFloat(uint32_t address, float value);
    void writeDouble(uint32_t address, double value);
#ifdef __ARM_NEON
    void write128(uint32_t address, uint32x4_t value);
#endif


    bool loadBios(const std::string& path);
    void reset();
    void clear();


    uint32_t allocateMemory(uint32_t size, uint32_t alignment = 4);
    uint32_t allocateProtectedMemory(uint32_t size, uint32_t alignment = 4);
    bool allocateAt(uint32_t address, uint32_t size); 
    void freeMemory(uint32_t address);
    bool isMemoryAllocated(uint32_t address, uint32_t size) const;
    bool isAddressRangeFree(uint32_t address, uint32_t size) const; 


    bool protectMemory(uint32_t address, uint32_t size, bool readOnly);
    bool unprotectMemory(uint32_t address, uint32_t size);
    bool isMemoryProtected(uint32_t address) const;


    uint32_t repairMemoryAddress(uint32_t invalidAddress);


    uint32_t repairInvalidAddress(uint32_t invalidAddress);


    uint32_t findValidMemoryAddress();
    bool isMemoryReadOnly(uint32_t address) const;


    uint32_t mapVirtualMemory(uint32_t virtualAddress, uint32_t physicalAddress, uint32_t size);
    bool unmapVirtualMemory(uint32_t virtualAddress, uint32_t size);
    uint32_t getPhysicalAddress(uint32_t virtualAddress) const;


    bool isGameLoaded() const { return gameLoaded; }
    void setGameLoaded(bool loaded) { gameLoaded = loaded; }
    uint32_t getVirtualAddress(uint32_t physicalAddress) const;


    void copyMemory(uint32_t dest, uint32_t src, uint32_t size);
    void fillMemory(uint32_t address, uint32_t size, uint8_t value);
    void zeroMemory(uint32_t address, uint32_t size);


    void dmaTransfer(uint32_t src, uint32_t dest, uint32_t size);
    void dmaTransferNEON(uint32_t src, uint32_t dest, uint32_t size);
    void dmaTransferAsync(uint32_t src, uint32_t dest, uint32_t size, 
                         std::function<void()> completionCallback);


    bool mapRegion(uint32_t base, uint32_t size,
                 std::function<uint32_t(uint32_t)> readHandler,
                 std::function<void(uint32_t, uint32_t)> writeHandler);

    void unmapRegion(uint32_t base);
    bool isRegionMapped(uint32_t address) const;


    void setAccessCallback(std::function<void(uint32_t, uint32_t, bool, uint32_t)> callback);
    void enableAccessLogging(bool enable);
    void clearAccessLog();
    std::vector<std::string> getAccessLog() const;


    uint32_t getTotalAllocatedMemory() const;
    uint32_t getFreeMemory() const;
    uint32_t getFragmentationLevel() const;
    void getMemoryStats(uint32_t& total, uint32_t& used, uint32_t& free, uint32_t& fragmented) const;


    uint8_t* getRamPointer();
    const uint8_t* getBiosPointer() const;
    uint32_t getRamSize() const;
    uint32_t getBiosSize() const;
    size_t getSize() const { return ram.size(); }


    void flushCaches();
    void flushCacheRange(uint32_t address, uint32_t size);
    void invalidateCacheRange(uint32_t address, uint32_t size);
    void prefetchMemory(uint32_t address, uint32_t size);


    bool isValidAddress(uint32_t address) const;
    bool isValidRange(uint32_t address, uint32_t size) const;
    bool isAligned(uint32_t address, uint32_t alignment) const;


    uint32_t read32Protected(uint32_t address);
    void write32Protected(uint32_t address, uint32_t value);

    bool loadXbeFromBuffer(const void* data, size_t size, size_t xbeHeaderOffset = 0);


    void setGPURenderer(NV2ARenderer* renderer);


    uint32_t xbeEntryPoint = 0x00100000;


    uint32_t getXbeEntryPoint() const { return xbeEntryPoint; }


    void setXbeEntryPoint(uint32_t entryPoint) { xbeEntryPoint = entryPoint; }

    uint32_t espStartValue = 0x0003FFFC;

private:

    std::vector<uint8_t> ram;
    std::vector<uint8_t> bios;


    std::vector<uint8_t> vertexMemory;
    std::vector<uint8_t> indexMemory;

    mutable std::mutex memoryMutex;


    struct MemoryBlock {
        uint32_t address;
        uint32_t size;
        bool allocated;
        bool isProtected;
        bool readOnly;

        MemoryBlock() : address(0), size(0), allocated(false), isProtected(false), readOnly(false) {}
        MemoryBlock(uint32_t addr, uint32_t sz) : address(addr), size(sz), allocated(true), isProtected(false), readOnly(false) {}
    };
    std::vector<MemoryBlock> memoryBlocks;


    struct VirtualMapping {
        uint32_t virtualAddress;
        uint32_t physicalAddress;
        uint32_t size;
        bool valid;

        VirtualMapping() : virtualAddress(0), physicalAddress(0), size(0), valid(false) {}
        VirtualMapping(uint32_t virt, uint32_t phys, uint32_t sz) : virtualAddress(virt), physicalAddress(phys), size(sz), valid(true) {}
    };
    std::vector<VirtualMapping> virtualMappings;


    struct ProtectionRegion {
        uint32_t address;
        uint32_t size;
        bool readOnly;

        ProtectionRegion() : address(0), size(0), readOnly(false) {}
        ProtectionRegion(uint32_t addr, uint32_t sz, bool ro) : address(addr), size(sz), readOnly(ro) {}

        bool contains(uint32_t addr) const {
            return addr >= address && addr < (address + size);
        }
    };
    std::vector<ProtectionRegion> protectionRegions;

    struct MappedRegion {
        uint32_t base;
        uint32_t size;
        std::function<uint32_t(uint32_t)> readHandler;
        std::function<void(uint32_t, uint32_t)> writeHandler;

        bool contains(uint32_t addr) const {
            return addr >= base && addr < (base + size);
        }
    };
    std::vector<MappedRegion> mappedRegions;


    std::function<void(uint32_t, uint32_t, bool, uint32_t)> accessCallback;
    bool accessLoggingEnabled;
    std::vector<std::string> accessLog;
    mutable std::mutex accessLogMutex;

    struct MemoryCache {
        uint32_t base;
        uint32_t size;
        std::unique_ptr<uint8_t[]> data;
        bool dirty;

        MemoryCache() : base(0), size(0), data(nullptr), dirty(false) {}

        bool contains(uint32_t addr) const {
            return addr >= base && addr < base + size;
        }
    };
    std::vector<MemoryCache> memoryCaches;


    MappedRegion* findMappedRegion(uint32_t address);
    void updateCache(uint32_t address, uint32_t size);
    void allocateCacheBlock(MemoryCache& cache);


    uint32_t findFreeMemoryBlock(uint32_t size, uint32_t alignment);
    void mergeFreeBlocks();
    void defragmentMemory();


    bool isAddressProtected(uint32_t address) const;
    bool isAddressReadOnly(uint32_t address) const;


    VirtualMapping* findVirtualMapping(uint32_t virtualAddress);
    const VirtualMapping* findVirtualMapping(uint32_t virtualAddress) const;


    uint32_t handleGPURead(uint32_t address);
    void handleGPUWrite(uint32_t address, uint32_t value);
    uint32_t handleAPURead(uint32_t address);
    void handleAPUWrite(uint32_t address, uint32_t value);
    uint32_t handlePCIRead(uint32_t address);
    void handlePCIWrite(uint32_t address, uint32_t value);


    uint32_t handleVertexMemoryRead(uint32_t address);
    void handleVertexMemoryWrite(uint32_t address, uint32_t value);
    uint32_t handleIndexMemoryRead(uint32_t address);
    void handleIndexMemoryWrite(uint32_t address, uint32_t value);


    void populateVertexMemoryWithTestData();
    void populateIndexMemoryWithTestData();


    void logAccess(uint32_t address, uint32_t value, bool isWrite);
    std::string formatAccessLog(uint32_t address, uint32_t value, bool isWrite) const;


    std::vector<uint8_t> gpuMemory;


    bool gameLoaded = false;


    NV2ARenderer* gpuRenderer = nullptr;


    bool gpuMemoryDirty = false;
    bool vertexMemoryDirty = false;
    bool indexMemoryDirty = false;


    uint32_t lastGPUWriteAddress = 0;
    uint32_t lastGPUWriteValue = 0;
    uint32_t lastGPUWriteOffset = 0;
};
