#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <cassert>

struct MemoryBlock {
    uint32_t address;
    uint32_t size;
    bool allocated;
    std::string purpose;
    uint64_t allocationTime;  
    uint32_t threadId;        
    bool isProtected;         
    uint32_t checksum;        

    MemoryBlock() : address(0), size(0), allocated(false), allocationTime(0), 
                    threadId(0), isProtected(false), checksum(0) {}

    MemoryBlock(uint32_t addr, uint32_t sz, bool alloc, const std::string& purp)
        : address(addr), size(sz), allocated(alloc), purpose(purp), 
          allocationTime(0), threadId(0), isProtected(false), checksum(0) {}
};

struct MemoryPool {
    uint32_t blockSize;
    uint32_t totalBlocks;
    uint32_t usedBlocks;
    std::vector<bool> blockBitmap;
    uint32_t baseAddress;

    MemoryPool(uint32_t size, uint32_t count, uint32_t base)
        : blockSize(size), totalBlocks(count), usedBlocks(0), 
          blockBitmap(count, false), baseAddress(base) {}

    uint32_t allocate();
    bool deallocate(uint32_t address);
    bool isAddressInPool(uint32_t address) const;
    uint32_t getFreeBlockCount() const;
};


struct AllocationResult {
    uint32_t address;
    bool success;
    std::string errorMessage;
    uint32_t actualSize;
    uint32_t alignment;
    uint64_t allocationTime;

    AllocationResult() : address(0), success(false), actualSize(0), 
                         alignment(0), allocationTime(0) {}

    AllocationResult(uint32_t addr, bool succ, uint32_t size, uint32_t align)
        : address(addr), success(succ), actualSize(size), 
          alignment(align), allocationTime(0) {}
};


struct MemoryStats {
    uint32_t totalSize;
    uint32_t allocatedSize;
    uint32_t freeSize;
    uint32_t fragmentationCount;
    uint32_t poolAllocations;
    uint32_t heapAllocations;
    uint32_t failedAllocations;
    uint64_t totalAllocationTime;

    MemoryStats() : totalSize(0), allocatedSize(0), freeSize(0), 
                    fragmentationCount(0), poolAllocations(0), heapAllocations(0),
                    failedAllocations(0), totalAllocationTime(0) {}
};

class MemoryAllocator {
public:

    MemoryAllocator(uint32_t baseAddress, uint32_t size, 
                    bool enablePools = true, bool enableProtection = true);
    ~MemoryAllocator();


    AllocationResult allocate(uint32_t size, uint32_t alignment = 1, 
                            const std::string& purpose = "unknown");
    bool deallocate(uint32_t address);


    uint32_t allocateWithFallback(uint32_t size, uint32_t alignment, 
                                 const std::string& purpose);


    uint32_t allocateFromPool(uint32_t size, const std::string& purpose);
    bool deallocateFromPool(uint32_t address);


    bool protectMemory(uint32_t address, bool readOnly);
    bool unprotectMemory(uint32_t address);
    bool isMemoryProtected(uint32_t address) const;


    bool defragment();
    uint32_t getFragmentationLevel() const;

     bool validateMemory(uint32_t address, uint32_t size) const;
    bool validateAllBlocks() const;


    void detectMemoryLeaks();
    std::vector<MemoryBlock> getLeakedBlocks() const;

    MemoryStats getStats() const;
    void resetStats();

    void dumpMemoryMap() const;
    void dumpDetailedStats() const;
    void enableVerboseLogging(bool enable);

   void lock();
    void unlock();
    bool tryLock();

    void cleanup();
    void optimize();

    void safeDeallocation(uint32_t address);


    uint32_t getTotalSize() const;
    uint32_t getAllocatedSize() const;
    uint32_t getFreeSize() const;
    uint32_t getLargestFreeBlock() const;
    bool isMemoryAllocated(uint32_t address) const;
    const MemoryBlock* getMemoryBlock(uint32_t address) const;

    uint32_t getPoolCount() const;
    uint32_t getPoolEfficiency() const;

private:

    void initializePools();
    void mergeAdjacentBlocks();
    uint32_t findBestFitBlock(uint32_t size, uint32_t alignment);
    uint32_t findWorstFitBlock(uint32_t size, uint32_t alignment);
    uint32_t findFirstFitBlock(uint32_t size, uint32_t alignment);

    std::vector<MemoryBlock> blocks;
    uint32_t baseAddress;
    uint32_t totalSize;
    uint32_t allocatedSize;

    std::vector<std::unique_ptr<MemoryPool>> memoryPools;
    uint32_t poolAllocationCount;

    mutable std::mutex allocatorMutex;
    std::atomic<bool> isLocked;

    bool enableMemoryPools;
    bool enableMemoryProtection;
    bool verboseLogging;

    mutable MemoryStats stats;

    std::unordered_map<uint32_t, bool> protectedBlocks;

    std::vector<MemoryBlock> leakedBlocks;
    uint64_t lastLeakCheck;

    uint32_t calculateChecksum(const MemoryBlock& block) const;
    bool verifyChecksum(const MemoryBlock& block) const;

    void logError(const std::string& message) const;
    void logWarning(const std::string& message) const;
    void logInfo(const std::string& message) const;


    bool isValidAddress(uint32_t address) const;
    bool isValidSize(uint32_t size) const;
    bool isValidAlignment(uint32_t alignment) const;


    bool canMergeBlocks(const MemoryBlock& block1, const MemoryBlock& block2) const;
    void compactMemory();


    void optimizePools();
    void resizePools();


    void rollbackAllocation(const MemoryBlock& block);
};

bool readFileFully(const std::string& path, std::vector<uint8_t>& buffer, size_t expectedSize);
uint32_t calculateOptimalAlignment(uint32_t size);
bool isPowerOfTwo(uint32_t value);
uint32_t nextPowerOfTwo(uint32_t value);


class MemoryAllocationException : public std::exception {
private:
    std::string message;

public:
    explicit MemoryAllocationException(const std::string& msg) : message(msg) {}

    const char* what() const noexcept override {
        return message.c_str();
    }
};

class MemoryProtectionException : public std::exception {
private:
    std::string message;

public:
    explicit MemoryProtectionException(const std::string& msg) : message(msg) {}

    const char* what() const noexcept override {
        return message.c_str();
    }
};

#endif 
