#include "memory_allocator.h"
#include <fstream>
#include <android/log.h>
#include <algorithm>
#include <cstring>

#define LOG_TAG "MemoryAllocator"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

MemoryAllocator::MemoryAllocator(uint32_t baseAddress, uint32_t size)
    : baseAddress(baseAddress), totalSize(size), allocatedSize(0) {
    LOGI("Memory Allocator initialized: base=0x%08X, size=%u bytes", baseAddress, size);


    blocks.push_back({baseAddress, size, false, "free"});
}

MemoryAllocator::~MemoryAllocator() {
    LOGI("Memory Allocator destroyed. Total allocated: %u/%u bytes", allocatedSize, totalSize);
}

uint32_t MemoryAllocator::allocate(uint32_t size, uint32_t alignment, const std::string& purpose) {
    if (size == 0) {
        LOGE("Cannot allocate 0 bytes");
        return 0;
    }


    auto bestBlock = blocks.end();
    uint32_t bestWaste = UINT32_MAX;

    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        if (!it->allocated) {
            uint32_t alignedAddress = (it->address + alignment - 1) & ~(alignment - 1);
            uint32_t waste = alignedAddress - it->address;

            if (alignedAddress + size <= it->address + it->size) {
                if (waste < bestWaste) {
                    bestBlock = it;
                    bestWaste = waste;
                }
            }
        }
    }

    if (bestBlock == blocks.end()) {
        LOGE("No suitable free block found for allocation of %u bytes", size);
        return 0;
    }

    uint32_t alignedAddress = (bestBlock->address + alignment - 1) & ~(alignment - 1);
    uint32_t waste = alignedAddress - bestBlock->address;


    std::vector<MemoryBlock> newBlocks;


    if (waste > 0) {
        newBlocks.push_back({bestBlock->address, waste, false, "waste"});
    }


    newBlocks.push_back({alignedAddress, size, true, purpose});


    uint32_t remainingSize = bestBlock->size - waste - size;
    if (remainingSize > 0) {
        newBlocks.push_back({alignedAddress + size, remainingSize, false, "free"});
    }


    *bestBlock = newBlocks[0];
    blocks.insert(bestBlock + 1, newBlocks.begin() + 1, newBlocks.end());

    allocatedSize += size;

    LOGD("Allocated %u bytes at 0x%08X (purpose: %s)", size, alignedAddress, purpose.c_str());
    return alignedAddress;
}

bool MemoryAllocator::deallocate(uint32_t address) {
    auto it = std::find_if(blocks.begin(), blocks.end(),
                          [address](const MemoryBlock& block) {
                              return block.address == address && block.allocated;
                          });

    if (it == blocks.end()) {
        LOGE("Cannot deallocate: no allocated block found at 0x%08X", address);
        return false;
    }

    allocatedSize -= it->size;
    it->allocated = false;
    it->purpose = "freed";

    LOGD("Deallocated %u bytes at 0x%08X", it->size, address);


    mergeAdjacentBlocks();

    return true;
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

uint32_t MemoryAllocator::getTotalSize() const {
    return totalSize;
}

uint32_t MemoryAllocator::getAllocatedSize() const {
    return allocatedSize;
}

uint32_t MemoryAllocator::getFreeSize() const {
    return totalSize - allocatedSize;
}

void MemoryAllocator::dumpMemoryMap() const {
    LOGI("=== Memory Map ===");
    LOGI("Total: %u bytes, Allocated: %u bytes, Free: %u bytes", 
         totalSize, allocatedSize, getFreeSize());

    for (const auto& block : blocks) {
        LOGI("0x%08X - 0x%08X (%u bytes) [%s] %s", 
             block.address, 
             block.address + block.size - 1,
             block.size,
             block.allocated ? "ALLOC" : "FREE",
             block.purpose.c_str());
    }
    LOGI("==================");
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

int main() {
    const std::string filePath = "/data/user/0/com.xanite/files/XboxDashboard/xboxdash.xbe";
    const size_t expectedSize = 1961984;

    std::vector<uint8_t> buffer;

    if (readFileFully(filePath, buffer, expectedSize)) {
        LOGI("File read successfully with size: %zu bytes", buffer.size());
    } else {
        LOGE("Failed to read the file properly.");
    }

    return 0;
}
