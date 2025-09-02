#include "xbox_memory.h"
#include <android/log.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

#define LOG_TAG "XboxMemoryExtended"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

uint32_t XboxMemory::allocateMemory(uint32_t size, uint32_t alignment) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    uint32_t address = findFreeMemoryBlock(size, alignment);
    if (address == 0xFFFFFFFF) {
        LOGE("Failed to allocate %u bytes with alignment %u", size, alignment);
        return 0xFFFFFFFF;
    }

    MemoryBlock block(address, size);
    memoryBlocks.push_back(block);

    LOGI("Allocated %u bytes at 0x%08X", size, address);
    return address;
}

uint32_t XboxMemory::allocateProtectedMemory(uint32_t size, uint32_t alignment) {
    uint32_t address = allocateMemory(size, alignment);
    if (address != 0xFFFFFFFF) {
        protectMemory(address, size, false);
    }
    return address;
}

void XboxMemory::freeMemory(uint32_t address) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    auto it = std::find_if(memoryBlocks.begin(), memoryBlocks.end(),
                          [address](const MemoryBlock& block) {
                              return block.address == address && block.allocated;
                          });

    if (it != memoryBlocks.end()) {
        it->allocated = false;
        LOGI("Freed memory at 0x%08X", address);
        mergeFreeBlocks();
    } else {
        LOGW("Attempted to free unallocated memory at 0x%08X", address);
    }
}

bool XboxMemory::isMemoryAllocated(uint32_t address, uint32_t size) const {
    std::lock_guard<std::mutex> lock(memoryMutex);

    for (const auto& block : memoryBlocks) {
        if (block.allocated && block.address <= address && 
            (block.address + block.size) >= (address + size)) {
            return true;
        }
    }
    return false;
}


bool XboxMemory::protectMemory(uint32_t address, uint32_t size, bool readOnly) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    if (!isMemoryAllocated(address, size)) {
        LOGE("Cannot protect unallocated memory at 0x%08X", address);
        return false;
    }

    ProtectionRegion region(address, size, readOnly);
    protectionRegions.push_back(region);

    LOGI("Protected memory 0x%08X-0x%08X (%s)", address, address + size - 1, 
         readOnly ? "read-only" : "read-write");
    return true;
}

bool XboxMemory::unprotectMemory(uint32_t address, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    auto it = std::remove_if(protectionRegions.begin(), protectionRegions.end(),
                            [address, size](const ProtectionRegion& region) {
                                return region.address == address && region.size == size;
                            });

    if (it != protectionRegions.end()) {
        protectionRegions.erase(it, protectionRegions.end());
        LOGI("Unprotected memory 0x%08X-0x%08X", address, address + size - 1);
        return true;
    }

    LOGW("Attempted to unprotect non-protected memory at 0x%08X", address);
    return false;
}

bool XboxMemory::isMemoryProtected(uint32_t address) const {
    return isAddressProtected(address);
}

bool XboxMemory::isMemoryReadOnly(uint32_t address) const {
    return isAddressReadOnly(address);
}


uint32_t XboxMemory::mapVirtualMemory(uint32_t virtualAddress, uint32_t physicalAddress, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);


    virtualAddress &= VM_PAGE_MASK;
    physicalAddress &= VM_PAGE_MASK;
    size = (size + VM_PAGE_SIZE - 1) & VM_PAGE_MASK;

    VirtualMapping mapping(virtualAddress, physicalAddress, size);
    virtualMappings.push_back(mapping);

    LOGI("Mapped virtual 0x%08X to physical 0x%08X (size: %u)", 
         virtualAddress, physicalAddress, size);
    return virtualAddress;
}

bool XboxMemory::unmapVirtualMemory(uint32_t virtualAddress, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    virtualAddress &= VM_PAGE_MASK;
    size = (size + VM_PAGE_SIZE - 1) & VM_PAGE_MASK;

    auto it = std::remove_if(virtualMappings.begin(), virtualMappings.end(),
                            [virtualAddress, size](const VirtualMapping& mapping) {
                                return mapping.virtualAddress == virtualAddress && mapping.size == size;
                            });

    if (it != virtualMappings.end()) {
        virtualMappings.erase(it, virtualMappings.end());
        LOGI("Unmapped virtual memory 0x%08X-0x%08X", virtualAddress, virtualAddress + size - 1);
        return true;
    }

    return false;
}

uint32_t XboxMemory::getPhysicalAddress(uint32_t virtualAddress) const {
    std::lock_guard<std::mutex> lock(memoryMutex);

    const VirtualMapping* mapping = findVirtualMapping(virtualAddress);
    if (mapping && mapping->valid) {
        uint32_t offset = virtualAddress - mapping->virtualAddress;
        return mapping->physicalAddress + offset;
    }

    return virtualAddress; 
}

uint32_t XboxMemory::getVirtualAddress(uint32_t physicalAddress) const {
    std::lock_guard<std::mutex> lock(memoryMutex);

    for (const auto& mapping : virtualMappings) {
        if (mapping.valid && physicalAddress >= mapping.physicalAddress && 
            physicalAddress < (mapping.physicalAddress + mapping.size)) {
            uint32_t offset = physicalAddress - mapping.physicalAddress;
            return mapping.virtualAddress + offset;
        }
    }

    return physicalAddress; 
}


void XboxMemory::copyMemory(uint32_t dest, uint32_t src, uint32_t size) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    if (!isValidRange(dest, size) || !isValidRange(src, size)) {
        LOGE("Invalid memory range for copy: dest=0x%08X, src=0x%08X, size=%u", dest, src, size);
        return;
    }


    if (dest < src + size && src < dest + size) {

        for (int32_t i = size - 1; i >= 0; --i) {
            ram[dest + i] = ram[src + i];
        }
    } else {

        for (uint32_t i = 0; i < size; ++i) {
            ram[dest + i] = ram[src + i];
        }
    }

    LOGI("Copied %u bytes from 0x%08X to 0x%08X", size, src, dest);
}

void XboxMemory::fillMemory(uint32_t address, uint32_t size, uint8_t value) {
    std::lock_guard<std::mutex> lock(memoryMutex);

    if (!isValidRange(address, size)) {
        LOGE("Invalid memory range for fill: address=0x%08X, size=%u", address, size);
        return;
    }

    std::fill(ram.begin() + address, ram.begin() + address + size, value);
    LOGI("Filled %u bytes at 0x%08X with value 0x%02X", size, address, value);
}

void XboxMemory::zeroMemory(uint32_t address, uint32_t size) {
    fillMemory(address, size, 0);
}


void XboxMemory::dmaTransferAsync(uint32_t src, uint32_t dest, uint32_t size, 
                                 std::function<void()> completionCallback) {

    dmaTransfer(src, dest, size);
    if (completionCallback) {
        completionCallback();
    }
}


void XboxMemory::enableAccessLogging(bool enable) {
    std::lock_guard<std::mutex> lock(accessLogMutex);
    accessLoggingEnabled = enable;
    LOGI("Memory access logging %s", enable ? "enabled" : "disabled");
}

void XboxMemory::clearAccessLog() {
    std::lock_guard<std::mutex> lock(accessLogMutex);
    accessLog.clear();
    LOGI("Memory access log cleared");
}

std::vector<std::string> XboxMemory::getAccessLog() const {
    std::lock_guard<std::mutex> lock(accessLogMutex);
    return accessLog;
}


uint32_t XboxMemory::getTotalAllocatedMemory() const {
    std::lock_guard<std::mutex> lock(memoryMutex);

    uint32_t total = 0;
    for (const auto& block : memoryBlocks) {
        if (block.allocated) {
            total += block.size;
        }
    }
    return total;
}

uint32_t XboxMemory::getFreeMemory() const {
    return RAM_SIZE - getTotalAllocatedMemory();
}

uint32_t XboxMemory::getFragmentationLevel() const {
    std::lock_guard<std::mutex> lock(memoryMutex);

    uint32_t freeBlocks = 0;
    uint32_t totalFreeSize = 0;

    for (const auto& block : memoryBlocks) {
        if (!block.allocated) {
            freeBlocks++;
            totalFreeSize += block.size;
        }
    }

    if (totalFreeSize == 0) return 0;
    return (freeBlocks * 100) / (totalFreeSize / 1024); 
}

void XboxMemory::getMemoryStats(uint32_t& total, uint32_t& used, uint32_t& free, uint32_t& fragmented) const {
    total = RAM_SIZE;
    used = getTotalAllocatedMemory();
    free = getFreeMemory();
    fragmented = getFragmentationLevel();
}


bool XboxMemory::isValidAddress(uint32_t address) const {
    return address < RAM_SIZE || 
           (address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) ||
           (address >= GPU_BASE && address < GPU_BASE + GPU_SIZE) ||
           (address >= APU_BASE && address < APU_BASE + APU_SIZE) ||
           (address >= PCI_BASE && address < PCI_BASE + PCI_SIZE);
}

bool XboxMemory::isValidRange(uint32_t address, uint32_t size) const {
    return address + size > address && 
           isValidAddress(address) && 
           isValidAddress(address + size - 1);
}

bool XboxMemory::isAligned(uint32_t address, uint32_t alignment) const {
    return (address & (alignment - 1)) == 0;
}


uint32_t XboxMemory::read32Protected(uint32_t address) {

    if (isAddressProtected(address)) {
        if (isAddressReadOnly(address)) {
            LOGE("Read access to read-only memory at 0x%08X", address);
            return 0;
        }
    }


    if (accessLoggingEnabled) {
        logAccess(address, 0, false);
    }


    return XboxMemory::read32(address);
}

void XboxMemory::write32Protected(uint32_t address, uint32_t value) {

    if (isAddressProtected(address)) {
        if (isAddressReadOnly(address)) {
            LOGE("Write access to read-only memory at 0x%08X", address);
            return;
        }
    }


    if (accessLoggingEnabled) {
        logAccess(address, value, true);
    }


    XboxMemory::write32(address, value);
}


uint32_t XboxMemory::findFreeMemoryBlock(uint32_t size, uint32_t alignment) {

    uint32_t currentAddress = 0;

    for (const auto& block : memoryBlocks) {
        if (!block.allocated) {
            uint32_t alignedAddress = (currentAddress + alignment - 1) & ~(alignment - 1);
            if (alignedAddress + size <= block.address) {
                return alignedAddress;
            }
        }
        currentAddress = block.address + block.size;
    }


    uint32_t alignedAddress = (currentAddress + alignment - 1) & ~(alignment - 1);
    if (alignedAddress + size <= RAM_SIZE) {
        return alignedAddress;
    }

    return 0xFFFFFFFF; 
}

void XboxMemory::mergeFreeBlocks() {

    std::sort(memoryBlocks.begin(), memoryBlocks.end(),
              [](const MemoryBlock& a, const MemoryBlock& b) {
                  return a.address < b.address;
              });


    for (size_t i = 0; i < memoryBlocks.size() - 1; ++i) {
        if (!memoryBlocks[i].allocated && !memoryBlocks[i + 1].allocated) {
            memoryBlocks[i].size += memoryBlocks[i + 1].size;
            memoryBlocks.erase(memoryBlocks.begin() + i + 1);
            --i; 
        }
    }
}

void XboxMemory::defragmentMemory() {
    std::lock_guard<std::mutex> lock(memoryMutex);


    std::sort(memoryBlocks.begin(), memoryBlocks.end(),
              [](const MemoryBlock& a, const MemoryBlock& b) {
                  return a.address < b.address;
              });

    uint32_t currentAddress = 0;

    for (auto& block : memoryBlocks) {
        if (block.allocated) {
            if (block.address != currentAddress) {

                copyMemory(currentAddress, block.address, block.size);
                block.address = currentAddress;
            }
            currentAddress += block.size;
        }
    }

    mergeFreeBlocks();
    LOGI("Memory defragmentation completed");
}

bool XboxMemory::isAddressProtected(uint32_t address) const {
    for (const auto& region : protectionRegions) {
        if (region.contains(address)) {
            return true;
        }
    }
    return false;
}

bool XboxMemory::isAddressReadOnly(uint32_t address) const {
    for (const auto& region : protectionRegions) {
        if (region.contains(address)) {
            return region.readOnly;
        }
    }
    return false;
}

XboxMemory::VirtualMapping* XboxMemory::findVirtualMapping(uint32_t virtualAddress) {
    for (auto& mapping : virtualMappings) {
        if (mapping.valid && virtualAddress >= mapping.virtualAddress && 
            virtualAddress < (mapping.virtualAddress + mapping.size)) {
            return &mapping;
        }
    }
    return nullptr;
}

const XboxMemory::VirtualMapping* XboxMemory::findVirtualMapping(uint32_t virtualAddress) const {
    for (const auto& mapping : virtualMappings) {
        if (mapping.valid && virtualAddress >= mapping.virtualAddress && 
            virtualAddress < (mapping.virtualAddress + mapping.size)) {
            return &mapping;
        }
    }
    return nullptr;
}


uint32_t XboxMemory::handlePCIRead(uint32_t address) {

    uint32_t offset = address - PCI_BASE;


    if (offset < 0x1000) {

        return 0x00000000; 
    } else if (offset < 0x2000) {

        return 0x00000000;
    } else if (offset < 0x3000) {

        return 0x00000000;
    }

    return 0x00000000;
}

void XboxMemory::handlePCIWrite(uint32_t address, uint32_t value) {

    uint32_t offset = address - PCI_BASE;


    if (offset < 0x1000) {

        LOGI("PCI config write: offset=0x%04X, value=0x%08X", offset, value);
    } else if (offset < 0x2000) {

        LOGI("Network adapter write: offset=0x%04X, value=0x%08X", offset, value);
    } else if (offset < 0x3000) {

        LOGI("USB controller write: offset=0x%04X, value=0x%08X", offset, value);
    }
}


void XboxMemory::logAccess(uint32_t address, uint32_t value, bool isWrite) {
    std::lock_guard<std::mutex> lock(accessLogMutex);

    if (accessLog.size() >= 1000) {
        accessLog.erase(accessLog.begin()); 
    }

    accessLog.push_back(formatAccessLog(address, value, isWrite));
}

std::string XboxMemory::formatAccessLog(uint32_t address, uint32_t value, bool isWrite) const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(8) << address
        << " " << (isWrite ? "W" : "R") << " "
        << std::setw(8) << value;
    return oss.str();
}
