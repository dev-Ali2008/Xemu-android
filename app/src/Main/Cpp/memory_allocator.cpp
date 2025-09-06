#include "memory_allocator.h"
#include <fstream>
#include <android/log.h>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

#define LOG_TAG "MemoryAllocator"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)


uint32_t MemoryPool::allocate() {
    for (uint32_t i = 0; i < totalBlocks; ++i) {
        if (!blockBitmap[i]) {
            blockBitmap[i] = true;
            usedBlocks++;
            return baseAddress + (i * blockSize);
        }
    }
    return 0; 
}

bool MemoryPool::deallocate(uint32_t address) {
    if (!isAddressInPool(address)) {
        return false;
    }

    uint32_t blockIndex = (address - baseAddress) / blockSize;
    if (blockIndex < totalBlocks && blockBitmap[blockIndex]) {
        blockBitmap[blockIndex] = false;
        usedBlocks--;
        return true;
    }
    return false;
}

bool MemoryPool::isAddressInPool(uint32_t address) const {
    return address >= baseAddress && 
           address < baseAddress + (totalBlocks * blockSize);
}

uint32_t MemoryPool::getFreeBlockCount() const {
    return totalBlocks - usedBlocks;
}


MemoryAllocator::MemoryAllocator(uint32_t baseAddress, uint32_t size, 
                                 bool enablePools, bool enableProtection)
    : baseAddress(baseAddress), totalSize(size), allocatedSize(0),
      memoryPools(), poolAllocationCount(0), allocatorMutex(), isLocked(false),
      enableMemoryPools(enablePools), enableMemoryProtection(enableProtection), 
      verboseLogging(false), stats(), protectedBlocks(), leakedBlocks(), lastLeakCheck(0) {


    stats.totalSize = size;
    stats.freeSize = size;


    if (size == 0) {
        throw MemoryAllocationException("Invalid memory size: 0");
    }

    if (baseAddress == 0) {
        throw MemoryAllocationException("Invalid base address: 0");
    }

    LOGI("Memory Allocator initialized: base=0x%08X, size=%u bytes", baseAddress, size);


    blocks.push_back(MemoryBlock(baseAddress, size, false, "free"));


    if (enableMemoryPools) {
        initializePools();
    }


    if (enableMemoryProtection) {

        protectMemory(baseAddress, true); 
    }
}

MemoryAllocator::~MemoryAllocator() {
    try {
        cleanup();
        LOGI("Memory Allocator destroyed. Total allocated: %u/%u bytes", allocatedSize, totalSize);
    } catch (...) {
        LOGE("Exception during MemoryAllocator destruction");
    }
}

void MemoryAllocator::initializePools() {

    const uint32_t poolSizes[] = {16, 32, 64, 128, 256, 512, 1024};
    const uint32_t poolCounts[] = {1000, 500, 250, 125, 62, 31, 15};

    uint32_t currentAddress = baseAddress + totalSize - 1024; 

    for (size_t i = 0; i < sizeof(poolSizes) / sizeof(poolSizes[0]); ++i) {
        uint32_t poolSize = poolSizes[i];
        uint32_t poolCount = poolCounts[i];
        uint32_t poolTotalSize = poolSize * poolCount;

        if (currentAddress >= baseAddress + poolTotalSize) {
            auto pool = std::make_unique<MemoryPool>(poolSize, poolCount, currentAddress - poolTotalSize);
            memoryPools.push_back(std::move(pool));
            currentAddress -= poolTotalSize;


            totalSize -= poolTotalSize;
            stats.totalSize = totalSize;
            stats.freeSize = totalSize;
        }
    }

    LOGI("Initialized %zu memory pools", memoryPools.size());
}

AllocationResult MemoryAllocator::allocate(uint32_t size, uint32_t alignment, const std::string& purpose) {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    AllocationResult result;
    result.allocationTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    try {

        if (!isValidSize(size)) {
            result.errorMessage = "Invalid size: " + std::to_string(size);
            stats.failedAllocations++;
            return result;
        }

        if (!isValidAlignment(alignment)) {
            result.errorMessage = "Invalid alignment: " + std::to_string(alignment);
            stats.failedAllocations++;
            return result;
        }


        if (enableMemoryPools && size <= 1024) {
            uint32_t poolAddress = allocateFromPool(size, purpose);
            if (poolAddress != 0) {
                result.address = poolAddress;
                result.success = true;
                result.actualSize = size;
                result.alignment = alignment;
                stats.poolAllocations++;
                return result;
            }
        }


        uint32_t address = findBestFitBlock(size, alignment);
        if (address == 0) {

            if (defragment()) {
                address = findBestFitBlock(size, alignment);
            }

            if (address == 0) {
                result.errorMessage = "No suitable free block found for allocation of " + 
                                   std::to_string(size) + " bytes";
                stats.failedAllocations++;
                return result;
            }
        }


        MemoryBlock newBlock(address, size, true, purpose);
        newBlock.allocationTime = result.allocationTime;
        newBlock.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());
        newBlock.checksum = calculateChecksum(newBlock);


        auto it = std::find_if(blocks.begin(), blocks.end(),
                              [address](const MemoryBlock& block) {
                                  return block.address == address && !block.allocated;
                              });

        if (it != blocks.end()) {

            *it = newBlock;
            allocatedSize += size;
            stats.allocatedSize = allocatedSize;
            stats.freeSize = totalSize - allocatedSize;
            stats.heapAllocations++;


            stats.totalAllocationTime += result.allocationTime;

            result.address = address;
            result.success = true;
            result.actualSize = size;
            result.alignment = alignment;

            if (verboseLogging) {
                logInfo("Allocated " + std::to_string(size) + " bytes at 0x" + 
                       std::to_string(address) + " (purpose: " + purpose + ")");
            }
        } else {
            result.errorMessage = "Failed to insert memory block";
            stats.failedAllocations++;
        }

    } catch (const std::exception& e) {
        result.errorMessage = "Exception during allocation: " + std::string(e.what());
        stats.failedAllocations++;
        LOGE("Exception during memory allocation: %s", e.what());
    } catch (...) {
        result.errorMessage = "Unknown exception during allocation";
        stats.failedAllocations++;
        LOGE("Unknown exception during memory allocation");
    }

    return result;
}

uint32_t MemoryAllocator::allocateWithFallback(uint32_t size, uint32_t alignment, const std::string& purpose) {

    AllocationResult result = allocate(size, alignment, purpose);
    if (result.success) {
        return result.address;
    }


    uint32_t newAlignment = nextPowerOfTwo(alignment);
    if (newAlignment != alignment) {
        result = allocate(size, newAlignment, purpose);
        if (result.success) {
            LOGW("Allocation succeeded with increased alignment: %u -> %u", alignment, newAlignment);
            return result.address;
        }
    }


    if (purpose.find("buffer") != std::string::npos || purpose.find("temp") != std::string::npos) {
        uint32_t reducedSize = size / 2;
        if (reducedSize > 0) {
            result = allocate(reducedSize, alignment, purpose + "_reduced");
            if (result.success) {
                LOGW("Allocation succeeded with reduced size: %u -> %u", size, reducedSize);
                return result.address;
            }
        }
    }

    LOGE("All allocation attempts failed for size %u, alignment %u", size, alignment);
    return 0;
}

uint32_t MemoryAllocator::allocateFromPool(uint32_t size, const std::string& purpose) {
    for (auto& pool : memoryPools) {
        if (size <= pool->blockSize) {
            uint32_t address = pool->allocate();
            if (address != 0) {

                MemoryBlock block(address, pool->blockSize, true, purpose);
                block.allocationTime = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                block.threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());


                blocks.push_back(block);
                allocatedSize += pool->blockSize;
                poolAllocationCount++;

                if (verboseLogging) {
                    logInfo("Pool allocation: " + std::to_string(pool->blockSize) + 
                           " bytes at 0x" + std::to_string(address));
                }

                return address;
            }
        }
    }
    return 0;
}

bool MemoryAllocator::deallocate(uint32_t address) {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    try {

        for (auto& pool : memoryPools) {
            if (pool->isAddressInPool(address)) {
                if (pool->deallocate(address)) {

                    auto it = std::find_if(blocks.begin(), blocks.end(),
                                          [address](const MemoryBlock& block) {
                                              return block.address == address && block.allocated;
                                          });
                    if (it != blocks.end()) {
                        allocatedSize -= it->size;
                        blocks.erase(it);
                        poolAllocationCount--;

                        if (verboseLogging) {
                            logInfo("Pool deallocation: 0x" + std::to_string(address));
                        }
                        return true;
                    }
                }
                return false;
            }
        }


        auto it = std::find_if(blocks.begin(), blocks.end(),
                              [address](const MemoryBlock& block) {
                                  return block.address == address && block.allocated;
                              });

        if (it == blocks.end()) {
            logWarning("Cannot deallocate: no allocated block found at 0x" + std::to_string(address));
            return false;
        }


        if (it->isProtected) {
            logWarning("Cannot deallocate protected memory at 0x" + std::to_string(address));
            return false;
        }


        if (!verifyChecksum(*it)) {
            logError("Memory corruption detected at 0x" + std::to_string(address));
            return false;
        }

        allocatedSize -= it->size;
        it->allocated = false;
        it->purpose = "freed";
        it->isProtected = false;
        it->checksum = 0;


        stats.allocatedSize = allocatedSize;
        stats.freeSize = totalSize - allocatedSize;

        if (verboseLogging) {
            logInfo("Deallocated " + std::to_string(it->size) + " bytes at 0x" + std::to_string(address));
        }


        mergeAdjacentBlocks();

        return true;

    } catch (const std::exception& e) {
        logError("Exception during deallocation: " + std::string(e.what()));
        return false;
    }
}

bool MemoryAllocator::protectMemory(uint32_t address, bool readOnly) {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    auto it = std::find_if(blocks.begin(), blocks.end(),
                          [address](const MemoryBlock& block) {
                              return block.address == address && block.allocated;
                          });

    if (it == blocks.end()) {
        logWarning("Cannot protect memory: no allocated block found at 0x" + std::to_string(address));
        return false;
    }

    it->isProtected = true;
    protectedBlocks[address] = readOnly;

    if (verboseLogging) {
        logInfo("Memory protected at 0x" + std::to_string(address) + 
               " (read-only: " + std::string(readOnly ? "true" : "false") + ")");
    }

    return true;
}

bool MemoryAllocator::defragment() {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    try {
        bool fragmented = false;
        uint32_t fragmentationCount = 0;


        for (size_t i = 0; i < blocks.size() - 1; ++i) {
            if (!blocks[i].allocated && !blocks[i + 1].allocated) {
                fragmented = true;
                fragmentationCount++;
            }
        }

        if (!fragmented) {
            return false; 
        }


        compactMemory();


        stats.fragmentationCount = fragmentationCount;

        if (verboseLogging) {
            logInfo("Defragmentation completed. Fragmentation level: " + std::to_string(fragmentationCount));
        }

        return true;

    } catch (const std::exception& e) {
        logError("Exception during defragmentation: " + std::string(e.what()));
        return false;
    }
}

void MemoryAllocator::compactMemory() {

    std::sort(blocks.begin(), blocks.end(),
              [](const MemoryBlock& a, const MemoryBlock& b) {
                  return a.address < b.address;
              });


    for (auto it = blocks.begin(); it != blocks.end() - 1;) {
        auto next = it + 1;

        if (!it->allocated && !next->allocated) {
            it->size += next->size;
            it->purpose = "merged_free";
            blocks.erase(next);
        } else {
            ++it;
        }
    }
}

void MemoryAllocator::detectMemoryLeaks() {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    leakedBlocks.clear();
    uint64_t currentTime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    for (const auto& block : blocks) {
        if (block.allocated) {

            if (currentTime - block.allocationTime > 1000000) { 
                leakedBlocks.push_back(block);
            }
        }
    }

    lastLeakCheck = currentTime;

    if (!leakedBlocks.empty()) {
        logWarning("Memory leak detection: " + std::to_string(leakedBlocks.size()) + " potential leaks found");
    }
}

MemoryStats MemoryAllocator::getStats() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    MemoryStats currentStats = stats;
    currentStats.freeSize = totalSize - allocatedSize;


    uint32_t fragmentationLevel = 0;
    for (size_t i = 0; i < blocks.size() - 1; ++i) {
        if (!blocks[i].allocated && !blocks[i + 1].allocated) {
            fragmentationLevel++;
        }
    }
    currentStats.fragmentationCount = fragmentationLevel;

    return currentStats;
}

void MemoryAllocator::dumpMemoryMap() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    LOGI("=== Memory Map ===");
    LOGI("Total: %u bytes, Allocated: %u bytes, Free: %u bytes", 
         totalSize, allocatedSize, getFreeSize());

    for (const auto& block : blocks) {
        LOGI("0x%08X - 0x%08X (%u bytes) [%s] %s %s", 
             block.address, 
             block.address + block.size - 1,
             block.size,
             block.allocated ? "ALLOC" : "FREE",
             block.purpose.c_str(),
             block.isProtected ? "[PROTECTED]" : "");
    }


    if (enableMemoryPools) {
        LOGI("=== Memory Pools ===");
        for (size_t i = 0; i < memoryPools.size(); ++i) {
            const auto& pool = memoryPools[i];
            LOGI("Pool %zu: %u bytes blocks, %u/%u used (%.1f%% efficiency)", 
                 i, pool->blockSize, pool->usedBlocks, pool->totalBlocks,
                 (float)pool->usedBlocks / pool->totalBlocks * 100.0f);
        }
    }

    LOGI("==================");
}

void MemoryAllocator::dumpDetailedStats() const {
    MemoryStats currentStats = getStats();

    LOGI("=== Detailed Memory Statistics ===");
    LOGI("Total Memory: %u bytes", currentStats.totalSize);
    LOGI("Allocated Memory: %u bytes (%.1f%%)", 
         currentStats.allocatedSize, 
         (float)currentStats.allocatedSize / currentStats.totalSize * 100.0f);
    LOGI("Free Memory: %u bytes", currentStats.freeSize);
    LOGI("Fragmentation Level: %u", currentStats.fragmentationCount);
    LOGI("Pool Allocations: %u", currentStats.poolAllocations);
    LOGI("Heap Allocations: %u", currentStats.heapAllocations);
    LOGI("Failed Allocations: %u", currentStats.failedAllocations);
    if (currentStats.poolAllocations + currentStats.heapAllocations > 0) {
        LOGI("Average Allocation Time: %.2f μs", 
             static_cast<double>(currentStats.totalAllocationTime) / (currentStats.poolAllocations + currentStats.heapAllocations));
    } else {
        LOGI("Average Allocation Time: N/A (no allocations)");
    }
    LOGI("==================================");
}

void MemoryAllocator::cleanup() {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    try {

        detectMemoryLeaks();


        blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                   [](const MemoryBlock& block) {
                                       return !block.allocated;
                                   }), blocks.end());


        resetStats();

        if (verboseLogging) {
            logInfo("Memory cleanup completed");
        }

    } catch (const std::exception& e) {
        logError("Exception during cleanup: " + std::string(e.what()));
    }
}

void MemoryAllocator::optimize() {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    try {

        defragment();


        optimizePools();


        validateAllBlocks();

        if (verboseLogging) {
            logInfo("Memory optimization completed");
        }

    } catch (const std::exception& e) {
        logError("Exception during optimization: " + std::string(e.what()));
    }
}


uint32_t MemoryAllocator::findBestFitBlock(uint32_t size, uint32_t alignment) {
    uint32_t bestAddress = 0;
    uint32_t bestWaste = UINT32_MAX;

    for (const auto& block : blocks) {
        if (!block.allocated) {
            uint32_t alignedAddress = (block.address + alignment - 1) & ~(alignment - 1);
            uint32_t waste = alignedAddress - block.address;

            if (alignedAddress + size <= block.address + block.size) {
                if (waste < bestWaste) {
                    bestAddress = alignedAddress;
                    bestWaste = waste;
                }
            }
        }
    }

    return bestAddress;
}

void MemoryAllocator::mergeAdjacentBlocks() {
    for (auto it = blocks.begin(); it != blocks.end() - 1;) {
        auto next = it + 1;

        if (!it->allocated && !next->allocated) {
            it->size += next->size;
            it->purpose = "merged_free";
            blocks.erase(next);
        } else {
            ++it;
        }
    }
}

uint32_t MemoryAllocator::calculateChecksum(const MemoryBlock& block) const {
    uint32_t checksum = 0;
    checksum ^= block.address;
    checksum ^= block.size;
    checksum ^= static_cast<uint32_t>(block.allocated);
    checksum ^= std::hash<std::string>{}(block.purpose);
    return checksum;
}

bool MemoryAllocator::verifyChecksum(const MemoryBlock& block) const {
    return block.checksum == calculateChecksum(block);
}

bool MemoryAllocator::isValidAddress(uint32_t address) const {
    return address >= baseAddress && address < baseAddress + totalSize;
}

bool MemoryAllocator::isValidSize(uint32_t size) const {
    return size > 0 && size <= totalSize;
}

bool MemoryAllocator::isValidAlignment(uint32_t alignment) const {
    return alignment > 0 && isPowerOfTwo(alignment);
}

void MemoryAllocator::logError(const std::string& message) const {
    LOGE("%s", message.c_str());
}

void MemoryAllocator::logWarning(const std::string& message) const {
    LOGW("%s", message.c_str());
}

void MemoryAllocator::logInfo(const std::string& message) const {
    LOGI("%s", message.c_str());
}


uint32_t calculateOptimalAlignment(uint32_t size) {
    if (size <= 8) return 8;
    if (size <= 16) return 16;
    if (size <= 32) return 32;
    if (size <= 64) return 64;
    if (size <= 128) return 128;
    if (size <= 256) return 256;
    if (size <= 512) return 512;
    if (size <= 1024) return 1024;
    return 4096; 
}

bool isPowerOfTwo(uint32_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

uint32_t nextPowerOfTwo(uint32_t value) {
    if (value == 0) return 1;
    value--;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value++;
    return value;
}


void MemoryAllocator::lock() {
    allocatorMutex.lock();
    isLocked = true;
}

void MemoryAllocator::unlock() {
    isLocked = false;
    allocatorMutex.unlock();
}

bool MemoryAllocator::tryLock() {
    if (allocatorMutex.try_lock()) {
        isLocked = true;
        return true;
    }
    return false;
}


uint32_t MemoryAllocator::getTotalSize() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    return totalSize;
}

uint32_t MemoryAllocator::getAllocatedSize() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    return allocatedSize;
}

uint32_t MemoryAllocator::getFreeSize() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    return totalSize - allocatedSize;
}

uint32_t MemoryAllocator::getLargestFreeBlock() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    uint32_t largestFree = 0;

    for (const auto& block : blocks) {
        if (!block.allocated && block.size > largestFree) {
            largestFree = block.size;
        }
    }

    return largestFree;
}

bool MemoryAllocator::isMemoryAllocated(uint32_t address) const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    for (const auto& block : blocks) {
        if (block.address <= address && address < block.address + block.size) {
            return block.allocated;
        }
    }
    return false;
}

const MemoryBlock* MemoryAllocator::getMemoryBlock(uint32_t address) const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    for (const auto& block : blocks) {
        if (block.address <= address && address < block.address + block.size) {
            return &block;
        }
    }
    return nullptr;
}

uint32_t MemoryAllocator::getPoolCount() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    return static_cast<uint32_t>(memoryPools.size());
}

uint32_t MemoryAllocator::getPoolEfficiency() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    if (memoryPools.empty()) return 0;

    uint32_t totalPoolBlocks = 0;
    uint32_t totalUsedBlocks = 0;

    for (const auto& pool : memoryPools) {
        totalPoolBlocks += pool->totalBlocks;
        totalUsedBlocks += pool->usedBlocks;
    }

    if (totalPoolBlocks == 0) return 0;
    return (totalUsedBlocks * 100) / totalPoolBlocks;
}


bool MemoryAllocator::unprotectMemory(uint32_t address) {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    auto it = std::find_if(blocks.begin(), blocks.end(),
                          [address](const MemoryBlock& block) {
                              return block.address == address && block.allocated;
                          });

    if (it == blocks.end()) {
        logWarning("Cannot unprotect memory: no allocated block found at 0x" + std::to_string(address));
        return false;
    }

    it->isProtected = false;
    protectedBlocks.erase(address);

    if (verboseLogging) {
        logInfo("Memory unprotected at 0x" + std::to_string(address));
    }

    return true;
}

bool MemoryAllocator::isMemoryProtected(uint32_t address) const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    auto it = std::find_if(blocks.begin(), blocks.end(),
                          [address](const MemoryBlock& block) {
                              return block.address == address && block.allocated;
                          });

    return it != blocks.end() && it->isProtected;
}


bool MemoryAllocator::validateMemory(uint32_t address, uint32_t size) const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    if (!isValidAddress(address) || !isValidSize(size)) {
        return false;
    }

    if (address + size > baseAddress + totalSize) {
        return false;
    }


    for (const auto& block : blocks) {
        if (block.allocated && 
            block.address <= address && 
            address + size <= block.address + block.size) {
            return true;
        }
    }

    return false;
}

bool MemoryAllocator::validateAllBlocks() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    for (const auto& block : blocks) {
        if (block.allocated && !verifyChecksum(block)) {
            logError("Memory corruption detected in block at 0x" + std::to_string(block.address));
            return false;
        }
    }

    return true;
}


std::vector<MemoryBlock> MemoryAllocator::getLeakedBlocks() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    return leakedBlocks;
}


void MemoryAllocator::resetStats() {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    stats = MemoryStats();
    stats.totalSize = totalSize;
    stats.freeSize = totalSize;
}


void MemoryAllocator::enableVerboseLogging(bool enable) {
    std::lock_guard<std::mutex> lock(allocatorMutex);
    verboseLogging = enable;
}


void MemoryAllocator::optimizePools() {
    for (auto& pool : memoryPools) {

        if (pool->getFreeBlockCount() == pool->totalBlocks) {

            if (verboseLogging) {
                logInfo("Pool optimization: pool at 0x" + std::to_string(pool->baseAddress) + " is empty");
            }
        }
    }
}

void MemoryAllocator::resizePools() {

    for (auto& pool : memoryPools) {
        float usageRatio = static_cast<float>(pool->usedBlocks) / pool->totalBlocks;

        if (usageRatio > 0.8f) {

            if (verboseLogging) {
                logInfo("Pool resize suggestion: pool at 0x" + std::to_string(pool->baseAddress) + 
                       " usage: " + std::to_string(usageRatio * 100) + "%");
            }
        }
    }
}


void MemoryAllocator::rollbackAllocation(const MemoryBlock& block) {
    try {

        auto it = std::find_if(blocks.begin(), blocks.end(),
                              [&block](const MemoryBlock& b) {
                                  return b.address == block.address && b.allocated;
                              });

        if (it != blocks.end()) {
            allocatedSize -= it->size;
            blocks.erase(it);
        }
    } catch (...) {

        LOGE("Rollback allocation failed");
    }
}

void MemoryAllocator::safeDeallocation(uint32_t address) {
    try {
        deallocate(address);
    } catch (...) {
        LOGE("Safe deallocation failed for address 0x%08X", address);
    }
}


uint32_t MemoryAllocator::getFragmentationLevel() const {
    std::lock_guard<std::mutex> lock(allocatorMutex);

    uint32_t fragmentationLevel = 0;
    for (size_t i = 0; i < blocks.size() - 1; ++i) {
        if (!blocks[i].allocated && !blocks[i + 1].allocated) {
            fragmentationLevel++;
        }
    }

    return fragmentationLevel;
}


bool readFileFully(const std::string& path, std::vector<uint8_t>& buffer, size_t expectedSize) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOGE("Failed to open file: %s", path.c_str());
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < expectedSize) {
        LOGE("File size (%zu) less than expected (%zu)", fileSize, expectedSize);
        return false;
    }

    buffer.resize(expectedSize);

    size_t totalRead = 0;
    while (totalRead < expectedSize) {
        file.read(reinterpret_cast<char*>(buffer.data() + totalRead), expectedSize - totalRead);
        size_t justRead = file.gcount();
        if (justRead == 0) {
            LOGE("Unexpected EOF while reading file: %s", path.c_str());
            return false;
        }
        totalRead += justRead;
    }

    LOGI("Successfully read %zu bytes from %s", totalRead, path.c_str());
    return true;
}
