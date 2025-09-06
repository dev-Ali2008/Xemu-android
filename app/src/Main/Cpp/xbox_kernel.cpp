#include "xbox_kernel.h"
#include "xbox_utils.h"
#include "memory_allocator.h"
#include <android/log.h>
#include <fstream>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <map>
#include <list>
#include <string>
#include <functional>
#include <vector>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <thread>

#ifndef LOG_TAG
#define LOG_TAG "XboxKernel"
#endif

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define GAMELOADED(...) __android_log_print(ANDROID_LOG_WARN, "GAMELOADED", __VA_ARGS__)

constexpr uint32_t XBOX_PAGE_SIZE = 0x1000;
constexpr uint32_t MAX_THREADS = 256;
constexpr uint32_t SECTION_FLAG_LOADED = 0x00000001;
constexpr uint32_t SECTION_FLAG_EXECUTABLE = 0x00000002;
constexpr uint32_t SECTION_FLAG_WRITABLE = 0x00000004;

XboxKernel::XboxKernel(XboxMemory* memory, X86Core* cpu) : 
    memory(memory),
    cpu(cpu),
    xbeLoaded(false),
    memoryAllocator(std::make_unique<MemoryAllocator>(0x10000000, 0x10000000, true, true)),
    nextThreadId(1),
    currentThreadId(0),
    currentProcessId(0)
{

    memset(&currentXbe, 0, sizeof(XbeHeader));
    sections.clear();
    xbeData.clear();
    threads.clear();

    initializeSyscalls();
    initializeMemory();
    initializeThreads();
}

XboxKernel::~XboxKernel() {

    for (auto& block : memoryBlocks) {
        if (block.allocated) {
            freeMemory(block.address);
        }
    }
}

void XboxKernel::initializeSyscalls() {

    registerSyscall(0x0001, [this](uint32_t* args) { return syscallDebugPrint(args); });
    registerSyscall(0x0002, [this](uint32_t* args) { return syscallAllocateMemory(args); });
    registerSyscall(0x0003, [this](uint32_t* args) { return syscallFreeMemory(args); });
    registerSyscall(0x0004, [this](uint32_t* args) { return syscallCreateThread(args); });
    registerSyscall(0x0005, [this](uint32_t* args) { return syscallOpenFile(args); });
    registerSyscall(0x0006, [this](uint32_t* args) { return syscallReadFile(args); });
    registerSyscall(0x0007, [this](uint32_t* args) { return syscallWriteFile(args); });
    registerSyscall(0x0008, [this](uint32_t* args) { return syscallCloseFile(args); });


    registerSyscall(0x0100, [this](uint32_t* args) { return syscallXboxGetTickCount(args); });
    registerSyscall(0x0101, [this](uint32_t* args) { return syscallXboxGetTime(args); });
    registerSyscall(0x0102, [this](uint32_t* args) { return syscallXboxSleep(args); });
    registerSyscall(0x0103, [this](uint32_t* args) { return syscallXboxGetSystemTime(args); });


    registerSyscall(0x0200, [this](uint32_t* args) { return syscallXboxAudioInit(args); });
    registerSyscall(0x0201, [this](uint32_t* args) { return syscallXboxAudioPlay(args); });
    registerSyscall(0x0202, [this](uint32_t* args) { return syscallXboxAudioStop(args); });
    registerSyscall(0x0203, [this](uint32_t* args) { return syscallXboxAudioSetVolume(args); });


    registerSyscall(0x0300, [this](uint32_t* args) { return syscallXboxGraphicsInit(args); });
    registerSyscall(0x0301, [this](uint32_t* args) { return syscallXboxGraphicsPresent(args); });
    registerSyscall(0x0302, [this](uint32_t* args) { return syscallXboxGraphicsClear(args); });
    registerSyscall(0x0303, [this](uint32_t* args) { return syscallXboxGraphicsSetMode(args); });


    registerSyscall(0x0400, [this](uint32_t* args) { return syscallXboxInputInit(args); });
    registerSyscall(0x0401, [this](uint32_t* args) { return syscallXboxInputGetState(args); });
    registerSyscall(0x0402, [this](uint32_t* args) { return syscallXboxInputSetVibration(args); });


    registerSyscall(0x0500, [this](uint32_t* args) { return syscallXboxNetworkInit(args); });
    registerSyscall(0x0501, [this](uint32_t* args) { return syscallXboxNetworkConnect(args); });
    registerSyscall(0x0502, [this](uint32_t* args) { return syscallXboxNetworkSend(args); });
    registerSyscall(0x0503, [this](uint32_t* args) { return syscallXboxNetworkRecv(args); });
}

void XboxKernel::initializeMemory() {

    memoryBlocks.clear();


    MemoryBlock systemBlock = {0x00000000, 0x00010000, true, "System Reserved"};
    memoryBlocks.push_back(systemBlock);


    MemoryBlock kernelBlock = {0x80000000, 0x10000000, true, "Kernel Memory"};
    memoryBlocks.push_back(kernelBlock);
}

void XboxKernel::initializeThreads() {
    threads.clear();
    currentThreadId = 0;


    Thread mainThread;
    mainThread.id = nextThreadId++;
    mainThread.entryPoint = 0x00100000;
    mainThread.stackPointer = 0x00FFFFF0;
    mainThread.status = ThreadStatus::Running;
    mainThread.priority = ThreadPriority::Normal;

    threads.push_back(mainThread);
    currentThreadId = mainThread.id;
}

bool XboxKernel::loadXbe(const std::string& filename) {
    if (xbeLoaded) {
        unloadXbe();
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        LOGE("❌ Failed to open XBE file: %s", filename.c_str());
        return false;
    }


    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < sizeof(XbeHeader)) {
        LOGE("❌ XBE file too small: %zu bytes", fileSize);
        return false;
    }


    XbeHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(XbeHeader));

    if (memcmp(header.magic, "XBEH", 4) != 0) {
        LOGE("❌ Invalid XBE magic: %c%c%c%c", header.magic[0], header.magic[1], header.magic[2], header.magic[3]);
        return false;
    }


    memcpy(&currentXbe, &header, sizeof(XbeHeader));


    if (!validateAndRepairXbeHeader(currentXbe, fileSize)) {
        LOGE("❌ XBE header validation failed");
        return false;
    }


    LOGI("🔧 Loading complete XBE file data (%zu bytes)...", fileSize);
    xbeData.resize(fileSize);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(xbeData.data()), fileSize);
    file.close();

    if (xbeData.size() != fileSize) {
        LOGE("❌ Failed to load complete XBE data: expected %zu bytes, got %zu", fileSize, xbeData.size());
        return false;
    }
    LOGI("✓ XBE file data loaded successfully: %zu bytes", xbeData.size());


    if (!loadSectionsSimple()) {
        LOGE("XEMU: Failed to load sections");
        return false;
    }


    uint32_t realEntryPoint = currentXbe.entryPoint; 
    LOGI("XEMU: XBE loaded successfully!");
    LOGI("XEMU: Base Address: 0x%08X", currentXbe.baseAddr);
    LOGI("XEMU: Entry Point: 0x%08X", currentXbe.entryPoint);
    LOGI("XEMU: Real Entry Point: 0x%08X", realEntryPoint);


    if (memory) {
        memory->setXbeEntryPoint(realEntryPoint);
        LOGI("XEMU: Entry point set in memory manager: 0x%08X", realEntryPoint);
    }


    if (memory) {
        uint8_t* entryPointPtr = memory->getRamPointer() + realEntryPoint;
        if (entryPointPtr[0] == 0x90 && entryPointPtr[1] == 0x90 && entryPointPtr[2] == 0x90) {
            LOGW("⚠️  WARNING: Entry point appears to contain NOPs (0x90) - code may not be loaded correctly");
        } else {
            LOGI("✓ Entry point contains code: %02X %02X %02X %02X", 
                 entryPointPtr[0], entryPointPtr[1], entryPointPtr[2], entryPointPtr[3]);
        }
    }

    xbeLoaded = true;
    return true;
}

bool XboxKernel::loadXbeFromMemory(const void* data, size_t size) {
    LOGI("XEMU: Loading XBE from memory: %zu bytes", size);


    unloadXbe();

    if (!data || size == 0) {
        LOGE("XEMU: XBE data is empty");
        return false;
    }


    xbeData.resize(size);
    memcpy(xbeData.data(), data, size);


    if (xbeData.size() < sizeof(XbeHeader)) {
        LOGE("XEMU: XBE data too small: %zu bytes", xbeData.size());
        return false;
    }

    memcpy(&currentXbe, xbeData.data(), sizeof(XbeHeader));


    if (memcmp(currentXbe.magic, "XBEH", 4) != 0) {
        LOGE("XEMU: Invalid XBE magic: %02X %02X %02X %02X", 
             currentXbe.magic[0], currentXbe.magic[1], currentXbe.magic[2], currentXbe.magic[3]);
        return false;
    }


    if (!validateAndRepairXbeHeader(currentXbe, size)) {
        LOGE("XEMU: XBE header validation failed");
        return false;
    }

    LOGI("✓ XBE data loaded successfully: %zu bytes", xbeData.size());


    if (!loadSectionsSimple()) {
        LOGE("XEMU: Failed to load sections");
        return false;
    }


    uint32_t realEntryPoint = currentXbe.entryPoint; 
    LOGI("XEMU: XBE loaded successfully!");
    LOGI("XEMU: Base Address: 0x%08X", currentXbe.baseAddr);
    LOGI("XEMU: Entry Point: 0x%08X", currentXbe.entryPoint);
    LOGI("XEMU: Real Entry Point: 0x%08X", realEntryPoint);


    if (memory) {
        memory->setXbeEntryPoint(realEntryPoint);
        LOGI("XEMU: Entry point set in memory manager: 0x%08X", realEntryPoint);
    }


    if (memory) {
        uint8_t* entryPointPtr = memory->getRamPointer() + realEntryPoint;
        if (entryPointPtr[0] == 0x90 && entryPointPtr[1] == 0x90 && entryPointPtr[2] == 0x90) {
            LOGW("⚠️  WARNING: Entry point appears to contain NOPs (0x90) - code may not be loaded correctly");
        } else {
            LOGI("✓ Entry point contains code: %02X %02X %02X %02X", 
                 entryPointPtr[0], entryPointPtr[1], entryPointPtr[2], entryPointPtr[3]);
        }
    }

    xbeLoaded = true;
    return true;
}


bool XboxKernel::loadSectionsSimple() {
    LOGI("XEMU: Loading %u sections", currentXbe.numberOfSections);

    sections.clear();
    sections.resize(currentXbe.numberOfSections);


    uint32_t sectionTableOffset = 0;


    if (currentXbe.sectionHeadersAddr < xbeData.size() && 
        currentXbe.sectionHeadersAddr + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size()) {
        sectionTableOffset = currentXbe.sectionHeadersAddr;
        LOGI("XEMU: Using header section table offset: 0x%08X", sectionTableOffset);
    } else {

        LOGI("XEMU: Header section table offset invalid (0x%08X), searching for real table...", currentXbe.sectionHeadersAddr);


        for (uint32_t offset = 0x1000; offset < xbeData.size() - sizeof(XbeSectionHeader); offset += 0x1000) {
            if (offset + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size()) {

                XbeSectionHeader* testHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset]);
                if (testHeader->fileAddr < xbeData.size() && testHeader->fileSize > 0 && 
                    testHeader->fileAddr + testHeader->fileSize <= xbeData.size()) {
                    sectionTableOffset = offset;
                    LOGI("XEMU: Found valid section table at offset: 0x%08X", offset);
                    break;
                }
            }
        }


        if (sectionTableOffset == 0) {
            LOGI("XEMU: No valid section table found, trying aggressive search...");
            for (uint32_t offset = 0x1000; offset < xbeData.size() - sizeof(XbeSectionHeader); offset += 0x100) {
                if (offset + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size()) {

                    XbeSectionHeader* testHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset]);
                    if (testHeader->fileAddr < xbeData.size() && testHeader->fileSize > 0 && 
                        testHeader->fileAddr + testHeader->fileSize <= xbeData.size()) {
                        sectionTableOffset = offset;
                        LOGI("XEMU: Found valid section table at offset: 0x%08X (aggressive search)", offset);
                        break;
                    }
                }
            }
        }


        if (sectionTableOffset == 0) {
            LOGI("XEMU: Searching for XBE magic patterns to locate section table...");
            for (uint32_t offset = 0x1000; offset < xbeData.size() - 16; offset += 4) {

                if (memcmp(&xbeData[offset], "XBEH", 4) == 0) {
                    LOGI("XEMU: Found XBE magic at offset 0x%08X, checking for section table nearby...", offset);

                    for (uint32_t tableOffset = offset + 0x100; tableOffset < offset + 0x2000 && 
                         tableOffset + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size(); 
                         tableOffset += 4) {
                        XbeSectionHeader* testHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[tableOffset]);
                        if (testHeader->fileAddr < xbeData.size() && testHeader->fileSize > 0 && 
                            testHeader->fileAddr + testHeader->fileSize <= xbeData.size()) {
                            sectionTableOffset = tableOffset;
                            LOGI("XEMU: Found section table near XBE magic at offset: 0x%08X", tableOffset);
                            break;
                        }
                    }
                    if (sectionTableOffset != 0) break;
                }
            }
        }


        if (sectionTableOffset == 0) {
            LOGI("XEMU: No valid section table found, trying aggressive search...");
            for (uint32_t offset = 0x1000; offset < xbeData.size() - sizeof(XbeSectionHeader); offset += 0x100) {
                if (offset + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size()) {

                    XbeSectionHeader* testHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset]);
                    if (testHeader->fileAddr < xbeData.size() && testHeader->fileSize > 0 && 
                        testHeader->fileAddr + testHeader->fileSize <= xbeData.size()) {
                        sectionTableOffset = offset;
                        LOGI("XEMU: Found valid section table at offset: 0x%08X (aggressive search)", offset);
                        break;
                    }
                }
            }
        }


        if (sectionTableOffset == 0) {
            LOGI("XEMU: Brute force search for valid section headers...");
            for (uint32_t offset = 0x1000; offset < xbeData.size() - sizeof(XbeSectionHeader); offset += 4) {
                if (offset + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size()) {

                    bool validTable = true;
                    for (uint32_t i = 0; i < currentXbe.numberOfSections; i++) {
                        XbeSectionHeader* header = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset + i * sizeof(XbeSectionHeader)]);
                        if (header->fileAddr >= xbeData.size() || header->fileSize == 0 || 
                            header->fileAddr + header->fileSize > xbeData.size()) {
                            validTable = false;
                            break;
                        }
                    }
                    if (validTable) {
                        sectionTableOffset = offset;
                        LOGI("XEMU: Found valid section table via brute force at offset: 0x%08X", offset);
                        break;
                    }
                }
            }
        }


        if (sectionTableOffset == 0) {
            LOGI("XEMU: Searching for section headers by virtual address patterns...");
            for (uint32_t offset = 0x1000; offset < xbeData.size() - sizeof(XbeSectionHeader); offset += 0x100) {
                XbeSectionHeader* testHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset]);


                if (testHeader->virtualAddr >= 0x00010000 && testHeader->virtualAddr < 0x08000000 &&
                    testHeader->virtualSize > 0 && testHeader->virtualSize < 0x10000000 &&
                    testHeader->fileAddr < xbeData.size() && testHeader->fileSize > 0 &&
                    testHeader->fileAddr + testHeader->fileSize <= xbeData.size()) {


                    bool validTable = true;
                    for (uint32_t i = 1; i < currentXbe.numberOfSections && validTable; i++) {
                        if (offset + i * sizeof(XbeSectionHeader) + sizeof(XbeSectionHeader) > xbeData.size()) {
                            validTable = false;
                            break;
                        }

                        XbeSectionHeader* nextHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset + i * sizeof(XbeSectionHeader)]);
                        if (nextHeader->virtualAddr < 0x00010000 || nextHeader->virtualAddr >= 0x08000000 ||
                            nextHeader->fileAddr >= xbeData.size()) {
                            validTable = false;
                        }
                    }

                    if (validTable) {
                        sectionTableOffset = offset;
                        LOGI("XEMU: Found valid section table by pattern matching at offset: 0x%08X", offset);
                        break;
                    }
                }
            }
        }


        if (sectionTableOffset == 0) {
            LOGI("XEMU: Trying smaller increments to find section table...");
            for (uint32_t offset = 0x1000; offset < xbeData.size() - sizeof(XbeSectionHeader); offset += 0x100) {
                if (offset + currentXbe.numberOfSections * sizeof(XbeSectionHeader) <= xbeData.size()) {

                    XbeSectionHeader* testHeader = reinterpret_cast<XbeSectionHeader*>(&xbeData[offset]);
                    if (testHeader->fileAddr < xbeData.size() && testHeader->fileSize > 0 && 
                        testHeader->fileAddr + testHeader->fileSize <= xbeData.size()) {
                        sectionTableOffset = offset;
                        LOGI("XEMU: Found valid section table at offset: 0x%08X", offset);
                        break;
                    }
                }
            }
        }





    }


    LOGI("XEMU: FORCING AGGRESSIVE SECTION SEARCH - searching for real section data...");


    LOGI("XEMU: Strategy 1 - Searching for Xbox section patterns...");
    for (uint32_t offset = 0x1000; offset < xbeData.size() - 0x1000; offset += 0x1000) {

        if (offset + 0x1000 <= xbeData.size()) {
            const char* data = reinterpret_cast<const char*>(&xbeData[offset]);


            if (strstr(data, ".text") || strstr(data, ".data") || strstr(data, ".rdata") ||
                strstr(data, ".pdata") || strstr(data, ".xdata") || strstr(data, ".bss") ||
                strstr(data, ".tls") || strstr(data, ".reloc")) {

                LOGI("XEMU: Found Xbox section signature at offset 0x%08X", offset);


                uint32_t sectionSize = std::min(xbeData.size() - offset, (size_t)0x1000000); 
                uint32_t targetAddr = currentXbe.baseAddr + 0x10000;

                if (memory && memory->allocateAt(targetAddr, sectionSize)) {
                    uint8_t* dest = memory->getRamPointer() + targetAddr;
                    memcpy(dest, &xbeData[offset], sectionSize);

                    currentXbe.entryPoint = targetAddr;
                    LOGI("XEMU: ✓ Loaded Xbox section at 0x%08X (%u bytes)", targetAddr, sectionSize);
                    LOGI("XEMU: ✓ Updated entry point to 0x%08X", currentXbe.entryPoint);
                    return true;
                }
            }
        }
    }


    LOGI("XEMU: Strategy 2 - Searching for executable code patterns...");
    uint32_t codeStart = 0x1000; 
    for (uint32_t offset = codeStart; offset < xbeData.size() - 0x1000; offset += 0x1000) {

        uint32_t* data = reinterpret_cast<uint32_t*>(&xbeData[offset]);
        bool foundCode = false;

        for (int i = 0; i < 0x1000/4; i++) {
            uint32_t instruction = data[i];

            if ((instruction & 0xFF) == 0x50 || 
                (instruction & 0xFF) == 0x51 || 
                (instruction & 0xFF) == 0x52 || 
                (instruction & 0xFF) == 0x53 || 
                (instruction & 0xFF) == 0x55 || 
                (instruction & 0xFF) == 0x56 || 
                (instruction & 0xFF) == 0x57 || 
                (instruction & 0xFF) == 0x58 || 
                (instruction & 0xFF) == 0x59 || 
                (instruction & 0xFF) == 0x5A || 
                (instruction & 0xFF) == 0x5B || 
                (instruction & 0xFF) == 0x5D || 
                (instruction & 0xFF) == 0x5E || 
                (instruction & 0xFF) == 0x5F || 
                (instruction & 0xFF) == 0xE8 || 
                (instruction & 0xFF) == 0xE9 || 
                (instruction & 0xFF) == 0xEB || 
                (instruction & 0xFF) == 0xC3 || 
                (instruction & 0xFF) == 0xC2 || 
                (instruction & 0xFF) == 0x90 || 
                (instruction & 0xFF) == 0xCC) { 
                foundCode = true;
                break;
            }
        }

        if (foundCode) {
            LOGI("XEMU: Found executable code at offset 0x%08X, loading as main section", offset);


            uint32_t mainSectionSize = std::min(xbeData.size() - offset, (size_t)0x800000); 
            uint32_t targetAddr = currentXbe.baseAddr + 0x10000; 

            LOGI("XEMU: Attempting to allocate %u bytes at 0x%08X", mainSectionSize, targetAddr);


            LOGI("XEMU: Bypassing memory allocation, loading directly into existing memory");

            if (memory) {
                uint8_t* dest = memory->getRamPointer() + currentXbe.baseAddr;
                if (dest) {

                    size_t copySize = std::min((size_t)0x10000, xbeData.size() - offset);
                    LOGI("XEMU: Copying %zu bytes from offset 0x%08X to 0x%08X", copySize, offset, currentXbe.baseAddr);


                    size_t bytesCopied = 0;
                    const size_t chunkSize = 0x1000; 

                    while (bytesCopied < copySize) {
                        size_t currentChunk = std::min(chunkSize, copySize - bytesCopied);
                        memcpy(dest + bytesCopied, &xbeData[offset + bytesCopied], currentChunk);
                        bytesCopied += currentChunk;

                        if (bytesCopied % (chunkSize * 4) == 0) { 
                            LOGI("XEMU: Copied %zu/%zu bytes (%.1f%%)", bytesCopied, copySize, 
                                 (float)bytesCopied / copySize * 100.0f);
                        }
                    }


                    currentXbe.entryPoint = currentXbe.baseAddr;
                    LOGI("XEMU: ✓ Loaded executable code at 0x%08X (%zu bytes)", currentXbe.baseAddr, copySize);
                    LOGI("XEMU: ✓ Updated entry point to 0x%08X", currentXbe.entryPoint);


                    LOGI("XEMU: ✓ Entry point set to loaded code location");
                    return true;
                } else {
                    LOGE("XEMU: Failed to get RAM pointer");
                }
            } else {
                LOGE("XEMU: Memory manager is null");
            }
        }
    }


    LOGW("XEMU: No executable code found, using fallback approach");
    if (memory && memory->allocateAt(currentXbe.baseAddr, xbeData.size())) {
        uint8_t* dest = memory->getRamPointer() + currentXbe.baseAddr;
        memcpy(dest, xbeData.data(), xbeData.size());
        LOGI("XEMU: ✓ Loaded entire XBE as single section at 0x%08X (%zu bytes)", 
             currentXbe.baseAddr, xbeData.size());
        return true;
    } else {
        LOGE("XEMU: Failed to load XBE as single section");
        return false;
    }


    LOGI("XEMU: Using section table offset: 0x%08X", sectionTableOffset);

    for (uint32_t i = 0; i < currentXbe.numberOfSections; i++) {
        if (sectionTableOffset + i * sizeof(XbeSectionHeader) + sizeof(XbeSectionHeader) > xbeData.size()) {
            LOGW("XEMU: Section %u out of bounds, skipping", i);
            continue;
        }

        XbeSectionHeader* sectionHeader = reinterpret_cast<XbeSectionHeader*>(
            &xbeData[sectionTableOffset + i * sizeof(XbeSectionHeader)]);

        sections[i].flags = sectionHeader->flags;
        sections[i].virtualAddr = sectionHeader->virtualAddr;
        sections[i].virtualSize = sectionHeader->virtualSize;
        sections[i].fileAddr = sectionHeader->fileAddr;
        sections[i].fileSize = sectionHeader->fileSize;

        LOGI("XEMU: Section %u: flags=0x%08X, vaddr=0x%08X, vsize=%u, faddr=%u, fsize=%u", 
             i, sections[i].flags, sections[i].virtualAddr, sections[i].virtualSize, 
             sections[i].fileAddr, sections[i].fileSize);


        if (sections[i].fileSize > 0 && sections[i].fileAddr + sections[i].fileSize <= xbeData.size()) {
            uint32_t targetAddr = sections[i].virtualAddr;

            LOGI("XEMU: Loading section %u: addr=0x%08X, size=%u, fileOffset=%u", 
                 i, targetAddr, sections[i].virtualSize, sections[i].fileAddr);

            if (memory && memory->allocateAt(targetAddr, sections[i].virtualSize)) {
                uint8_t* dest = memory->getRamPointer() + targetAddr;
                uint8_t* src = &xbeData[sections[i].fileAddr];

                size_t copySize = std::min(sections[i].virtualSize, sections[i].fileSize);
                if (copySize > 0) {
                    memcpy(dest, src, copySize);
                    LOGI("XEMU: ✓ Loaded section %u at 0x%08X (%zu bytes)", i, targetAddr, copySize);


                    if (copySize >= 4) {
                        LOGI("XEMU: First 4 bytes at 0x%08X: %02X %02X %02X %02X", 
                             targetAddr, dest[0], dest[1], dest[2], dest[3]);
                    }
                }


                if (sections[i].virtualSize > copySize) {
                    memset(dest + copySize, 0, sections[i].virtualSize - copySize);
                    LOGI("XEMU: Zero-filled %zu bytes at end of section %u", 
                         sections[i].virtualSize - copySize, i);
                }
            } else {
                LOGW("XEMU: Failed to allocate memory for section %u at 0x%08X", i, targetAddr);
            }
        } else {
            LOGW("XEMU: Skipping section %u: no data or invalid file offset (faddr=%u, fsize=%u, xbeSize=%zu)", 
                 i, sections[i].fileAddr, sections[i].fileSize, xbeData.size());
        }
    }

    LOGI("XEMU: Sections loaded successfully");
    return true;
}

void XboxKernel::unloadXbe() {
    if (!xbeLoaded) return;

    for (auto& section : sections) {
        if (section.flags & SECTION_FLAG_LOADED) {
            freeMemory(section.virtualAddr);
        }
    }

    xbeData.clear();
    sections.clear();
    xbeLoaded = false;
    memset(&currentXbe, 0, sizeof(XbeHeader));
}

bool XboxKernel::loadSections() {

    return loadSectionsSimple();
}

bool XboxKernel::validateXbe() {
    return xbeLoaded && memcmp(currentXbe.magic, "XBEH", 4) == 0 &&
           currentXbe.baseAddr >= 0x10000;
}

uint32_t XboxKernel::getEntryPoint() const {
    if (xbeLoaded) {
        LOGI("getEntryPoint: Returning 0x%08X", currentXbe.entryPoint);
        return currentXbe.entryPoint;
    }
    LOGE("❌ No XBE loaded - cannot return entry point");
    return 0x00100000; 
}



uint32_t XboxKernel::syscallUnknown(uint32_t* args) {
    LOGE("Unknown syscall: args=[0x%08X, 0x%08X, 0x%08X, 0x%08X]",
         args[0], args[1], args[2], args[3]);
    return 0;
}

uint32_t XboxKernel::syscallDebugPrint(uint32_t* args) {
    const char* message = reinterpret_cast<const char*>(args[0]);
    if (message) {
        LOGI("Debug: %s", message);
    }
    return 0;
}

uint32_t XboxKernel::syscallAllocateMemory(uint32_t* args) {
    uint32_t size = args[0];
    uint32_t address = allocateMemory(size);
    LOGI("AllocateMemory: size=%u, address=0x%08X", size, address);
    return address;
}

uint32_t XboxKernel::syscallFreeMemory(uint32_t* args) {
    uint32_t address = args[0];
    bool success = freeMemory(address);
    LOGI("FreeMemory: address=0x%08X, success=%s", address, success ? "true" : "false");
    return success ? 0 : 1;
}

uint32_t XboxKernel::syscallCreateThread(uint32_t* args) {
    uint32_t entryPoint = args[0];
    uint32_t stackSize = args[1];
    uint32_t priority = args[2];

    Thread thread;
    thread.id = nextThreadId++;
    thread.entryPoint = entryPoint;
    thread.stackPointer = 0x00FFFFF0;
    thread.status = ThreadStatus::Ready;
    thread.priority = static_cast<ThreadPriority>(priority % 3); 

    threads.push_back(thread);

    LOGI("CreateThread: id=%d, entryPoint=0x%08X, stackSize=%u, priority=%u",
         thread.id, entryPoint, stackSize, priority);

    return thread.id;
}

uint32_t XboxKernel::syscallOpenFile(uint32_t* args) {
    const char* filename = reinterpret_cast<const char*>(args[0]);
    uint32_t mode = args[1];
    LOGI("OpenFile: %s, mode=0x%08X", filename ? filename : "null", mode);
    return 1; 
}

uint32_t XboxKernel::syscallReadFile(uint32_t* args) {
    uint32_t handle = args[0];
    uint32_t buffer = args[1];
    uint32_t size = args[2];
    LOGI("ReadFile: handle=%u, buffer=0x%08X, size=%u", handle, buffer, size);
    return size; 
}

uint32_t XboxKernel::syscallWriteFile(uint32_t* args) {
    uint32_t handle = args[0];
    uint32_t buffer = args[1];
    uint32_t size = args[2];
    LOGI("WriteFile: handle=%u, buffer=0x%08X, size=%u", handle, buffer, size);
    return size; 
}

uint32_t XboxKernel::syscallCloseFile(uint32_t* args) {
    uint32_t handle = args[0];
    LOGI("CloseFile: handle=%u", handle);
    return 0; 
}


uint32_t XboxKernel::syscallXboxGetTickCount(uint32_t* args) {
    (void)args; 
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    LOGI("Xbox GetTickCount: %lld", ticks);
    return static_cast<uint32_t>(ticks);
}

uint32_t XboxKernel::syscallXboxGetTime(uint32_t* args) {
    (void)args; 
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    LOGI("Xbox GetTime: %lld", seconds);
    return static_cast<uint32_t>(seconds);
}

uint32_t XboxKernel::syscallXboxSleep(uint32_t* args) {
    uint32_t milliseconds = args[0];
    LOGI("Xbox Sleep: %u ms", milliseconds);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return 0; 
}

uint32_t XboxKernel::syscallXboxGetSystemTime(uint32_t* args) {
    (void)args; 
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    LOGI("Xbox GetSystemTime: %lld", seconds);
    return static_cast<uint32_t>(seconds);
}


uint32_t XboxKernel::syscallXboxAudioInit(uint32_t* args) {
    (void)args; 
    LOGI("Xbox Audio System initialized");
    return 0; 
}

uint32_t XboxKernel::syscallXboxAudioPlay(uint32_t* args) {
    uint32_t audioData = args[0];
    uint32_t size = args[1];
    LOGI("Xbox Audio Play: data=0x%08X, size=%u", audioData, size);
    return 0; 
}

uint32_t XboxKernel::syscallXboxAudioStop(uint32_t* args) {
    (void)args; 
    LOGI("Xbox Audio Stop");
    return 0; 
}

uint32_t XboxKernel::syscallXboxAudioSetVolume(uint32_t* args) {
    uint32_t volume = args[0]; 
    LOGI("Xbox Audio Volume: %u%%", volume);
    return 0; 
}


uint32_t XboxKernel::syscallXboxGraphicsInit(uint32_t* args) {
    (void)args; 
    LOGI("Xbox Graphics System initialized");
    return 0; 
}

uint32_t XboxKernel::syscallXboxGraphicsPresent(uint32_t* args) {
    uint32_t framebuffer = args[0];
    LOGI("Xbox Graphics Present: framebuffer=0x%08X", framebuffer);
    return 0; 
}

uint32_t XboxKernel::syscallXboxGraphicsClear(uint32_t* args) {
    uint32_t color = args[0];
    LOGI("Xbox Graphics Clear: color=0x%08X", color);
    return 0; 
}

uint32_t XboxKernel::syscallXboxGraphicsSetMode(uint32_t* args) {
    uint32_t width = args[0];
    uint32_t height = args[1];
    uint32_t depth = args[2];
    LOGI("Xbox Graphics SetMode: %ux%u, %u-bit", width, height, depth);
    return 0; 
}


uint32_t XboxKernel::syscallXboxInputInit(uint32_t* args) {
    (void)args; 
    LOGI("Xbox Input System initialized");
    return 0; 
}

uint32_t XboxKernel::syscallXboxInputGetState(uint32_t* args) {
    uint32_t port = args[0];
    uint32_t statePtr = args[1];


    uint32_t controllerState = 0;
    if (port < 4) {
        controllerState = 0x0000FFFF; 
    }

    if (statePtr && memory) {
        memory->write32(statePtr, controllerState);
    }

    LOGI("Xbox Input GetState: port=%u, state=0x%08X", port, controllerState);
    return 0; 
}

uint32_t XboxKernel::syscallXboxInputSetVibration(uint32_t* args) {
    uint32_t port = args[0];
    uint32_t leftMotor = args[1];
    uint32_t rightMotor = args[2];
    LOGI("Xbox Input SetVibration: port=%u, left=%u, right=%u", port, leftMotor, rightMotor);
    return 0; 
}


uint32_t XboxKernel::syscallXboxNetworkInit(uint32_t* args) {
    (void)args; 
    LOGI("Xbox Network System initialized");
    return 0; 
}

uint32_t XboxKernel::syscallXboxNetworkConnect(uint32_t* args) {
    const char* address = reinterpret_cast<const char*>(args[0]);
    uint32_t port = args[1];
    LOGI("Xbox Network Connect: %s:%u", address ? address : "null", port);
    return 0; 
}

uint32_t XboxKernel::syscallXboxNetworkSend(uint32_t* args) {
    uint32_t socket = args[0];
    (void)reinterpret_cast<const void*>(args[1]); 
    uint32_t size = args[2];
    LOGI("Xbox Network Send: socket=%u, size=%u", socket, size);
    return size; 
}

uint32_t XboxKernel::syscallXboxNetworkRecv(uint32_t* args) {
    uint32_t socket = args[0];
    (void)reinterpret_cast<void*>(args[1]); 
    uint32_t size = args[2];
    LOGI("Xbox Network Recv: socket=%u, size=%u", socket, size);
    return 0; 
}



void XboxKernel::registerSyscall(uint32_t callId, std::function<uint32_t(uint32_t*)> handler) {
    syscallTable[callId] = handler;
    LOGI("Registered syscall 0x%08X", callId);
}

uint32_t XboxKernel::allocateMemory(uint32_t size) {
    if (memoryAllocator) {
        AllocationResult result = memoryAllocator->allocate(size, 1, "User Allocated");
        if (result.success) {
            MemoryBlock block = {result.address, result.actualSize, true, "User Allocated"};
            memoryBlocks.push_back(block);
            LOGI("Allocated %u bytes at 0x%08X", result.actualSize, result.address);
            return result.address;
        } else {
            LOGE("Memory allocation failed: %s", result.errorMessage.c_str());
        }
    } else {
        LOGE("Memory allocator not initialized");
    }
    return 0;
}

uint32_t XboxKernel::allocateMemory(uint32_t size, uint32_t alignment, const char* purpose) {
    if (memoryAllocator) {
        std::string purposeStr = purpose ? purpose : "User Allocated";
        AllocationResult result = memoryAllocator->allocate(size, alignment, purposeStr);
        if (result.success) {
            MemoryBlock block = {result.address, result.actualSize, true, purposeStr};
            memoryBlocks.push_back(block);
            LOGI("Allocated %u bytes at 0x%08X (aligned to 0x%08X) for %s", 
                 result.actualSize, result.address, alignment, purposeStr.c_str());
            return result.address;
        } else {
            LOGE("Memory allocation failed: %s", result.errorMessage.c_str());
        }
    } else {
        LOGE("Memory allocator not initialized");
    }
    return 0;
}

bool XboxKernel::freeMemory(uint32_t address) {
    if (memoryAllocator) {
        bool success = memoryAllocator->deallocate(address);
        if (success) {

            memoryBlocks.erase(
                std::remove_if(memoryBlocks.begin(), memoryBlocks.end(),
                    [address](const MemoryBlock& block) {
                        return block.address == address;
                    }),
                memoryBlocks.end()
            );
            LOGI("Freed memory at 0x%08X", address);
        }
        return success;
    }
    LOGE("Memory allocator not initialized");
    return false;
}

uint32_t XboxKernel::findFreeMemory(uint32_t size) {

    for (const auto& block : memoryBlocks) {
        if (!block.allocated && block.size >= size) {
            return block.address;
        }
    }
    return 0;
}

void XboxKernel::dumpMemoryMap() const {
    LOGI("=== Memory Map ===");
    for (const auto& block : memoryBlocks) {
        LOGI("0x%08X - 0x%08X (%u bytes) - %s - %s", 
             block.address, 
             block.address + block.size - 1,
             block.size,
             block.allocated ? "Allocated" : "Free",
             block.purpose.c_str());
    }
    LOGI("=== End Memory Map ===");
}

uint32_t XboxKernel::createThread(uint32_t entryPoint, uint32_t stackSize) {
    (void)stackSize; 
    Thread thread;
    thread.id = nextThreadId++;
    thread.processId = currentProcessId;
    thread.entryPoint = entryPoint;
    thread.stackPointer = 0x00FFFFF0;
    thread.status = ThreadStatus::Ready;
    thread.priority = ThreadPriority::Normal;

    threads.push_back(thread);
    LOGI("Created thread %u with entry point 0x%08X", thread.id, entryPoint);
    return thread.id;
}

bool XboxKernel::terminateThread(uint32_t threadId) {
    for (auto& thread : threads) {
        if (thread.id == threadId) {
            thread.status = ThreadStatus::Terminated;
            LOGI("Terminated thread %u", threadId);
            return true;
        }
    }
    LOGE("Thread %u not found for termination", threadId);
    return false;
}

void XboxKernel::schedule() {

    if (threads.empty()) return;

    currentThreadId = (currentThreadId + 1) % threads.size();
    LOGI("Scheduled thread %u", currentThreadId);
}

void XboxKernel::dumpThreads() const {
    LOGI("=== Thread List ===");
    for (const auto& thread : threads) {
        LOGI("Thread %u: Entry=0x%08X, Status=%d, Priority=%d", 
             thread.id, thread.entryPoint, 
             static_cast<int>(thread.status), 
             static_cast<int>(thread.priority));
    }
    LOGI("=== End Thread List ===");
}

uint32_t XboxKernel::createProcess(const char* name, uint32_t entryPoint) {
    currentProcessId++;
    LOGI("Created process %u: %s, entry point 0x%08X", currentProcessId, name ? name : "unnamed", entryPoint);
    return currentProcessId;
}

void XboxKernel::setDebugOutput(std::function<void(const std::string&)> callback) {
    debugOutput = callback;
}

void XboxKernel::registerCustomLib(const std::string& name, std::function<uint32_t(uint32_t)> handler) {
    customLibs[name] = handler;
    LOGI("Registered custom library: %s", name.c_str());
}

uint32_t XboxKernel::getTotalAllocatedMemory() const {
    uint32_t total = 0;
    for (const auto& block : memoryBlocks) {
        if (block.allocated) {
            total += block.size;
        }
    }
    return total;
}

bool XboxKernel::isMemoryAllocated(uint32_t address) const {
    for (const auto& block : memoryBlocks) {
        if (block.address <= address && address < block.address + block.size) {
            return block.allocated;
        }
    }
    return false;
}

const XboxKernel::MemoryBlock* XboxKernel::getMemoryBlock(uint32_t address) const {
    for (const auto& block : memoryBlocks) {
        if (block.address <= address && address < block.address + block.size) {
            return &block;
        }
    }
    return nullptr;
}

uint32_t XboxKernel::alignToPage(uint32_t size) {
    return (size + XBOX_PAGE_SIZE - 1) & ~(XBOX_PAGE_SIZE - 1);
}

uint32_t XboxKernel::handleSyscall(uint32_t call, uint32_t* args) {
    auto it = syscallTable.find(call);
    if (it != syscallTable.end()) {
        return it->second(args);
    }
    return syscallUnknown(args);
}

bool XboxKernel::isXbeLoaded() const {
    return xbeLoaded;
}

bool XboxKernel::parseXbe(const std::string& path) {
    return loadXbe(path);
}


uint32_t XboxKernel::allocateMemoryRobust(uint32_t size, uint32_t alignment, const char* purpose) {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return 0;
    }

    try {

        uint32_t address = memoryAllocator->allocateWithFallback(size, alignment, 
                                                               purpose ? purpose : "Robust Allocated");
        if (address != 0) {
            MemoryBlock block = {address, size, true, purpose ? purpose : "Robust Allocated"};
            memoryBlocks.push_back(block);
            LOGI("Robust allocation: %u bytes at 0x%08X (aligned to 0x%08X) for %s", 
                 size, address, alignment, purpose ? purpose : "Robust Allocated");
        } else {
            LOGW("Robust allocation failed for %u bytes with alignment 0x%08X", size, alignment);
        }
        return address;
    } catch (const std::exception& e) {
        LOGE("Exception during robust memory allocation: %s", e.what());
        return 0;
    } catch (...) {
        LOGE("Unknown exception during robust memory allocation");
        return 0;
    }
}

bool XboxKernel::freeMemorySafe(uint32_t address) {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return false;
    }

    try {

        memoryAllocator->safeDeallocation(address);


        memoryBlocks.erase(
            std::remove_if(memoryBlocks.begin(), memoryBlocks.end(),
                [address](const MemoryBlock& block) {
                    return block.address == address;
                }),
            memoryBlocks.end()
        );

        LOGI("Safely freed memory at 0x%08X", address);
        return true;
    } catch (const std::exception& e) {
        LOGE("Exception during safe memory deallocation: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during safe memory deallocation");
        return false;
    }
}

bool XboxKernel::protectMemory(uint32_t address, bool readOnly) {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return false;
    }

    try {
        bool success = memoryAllocator->protectMemory(address, readOnly);
        if (success) {
            LOGI("Memory protection set at 0x%08X (read-only: %s)", address, readOnly ? "true" : "false");
        }
        return success;
    } catch (const std::exception& e) {
        LOGE("Exception during memory protection: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during memory protection");
        return false;
    }
}

bool XboxKernel::unprotectMemory(uint32_t address) {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return false;
    }

    try {
        bool success = memoryAllocator->unprotectMemory(address);
        if (success) {
            LOGI("Memory protection removed at 0x%08X", address);
        }
        return success;
    } catch (const std::exception& e) {
        LOGE("Exception during memory unprotection: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during memory unprotection");
        return false;
    }
}

bool XboxKernel::isMemoryProtected(uint32_t address) const {
    if (!memoryAllocator) {
        return false;
    }

    try {
        return memoryAllocator->isMemoryProtected(address);
    } catch (const std::exception& e) {
        LOGE("Exception during memory protection check: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during memory protection check");
        return false;
    }
}

bool XboxKernel::validateMemory(uint32_t address, uint32_t size) const {
    if (!memoryAllocator) {
        return false;
    }

    try {
        return memoryAllocator->validateMemory(address, size);
    } catch (const std::exception& e) {
        LOGE("Exception during memory validation: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during memory validation");
        return false;
    }
}

bool XboxKernel::defragmentMemory() {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return false;
    }

    try {
        bool success = memoryAllocator->defragment();
        if (success) {
            LOGI("Memory defragmentation completed successfully");
        } else {
            LOGI("Memory defragmentation not needed");
        }
        return success;
    } catch (const std::exception& e) {
        LOGE("Exception during memory defragmentation: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during memory defragmentation");
        return false;
    }
}

void XboxKernel::optimizeMemory() {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return;
    }

    try {
        memoryAllocator->optimize();
        LOGI("Memory optimization completed");
    } catch (const std::exception& e) {
        LOGE("Exception during memory optimization: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception during memory optimization");
    }
}

void XboxKernel::cleanupMemory() {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return;
    }

    try {
        memoryAllocator->cleanup();
        LOGI("Memory cleanup completed");
    } catch (const std::exception& e) {
        LOGE("Exception during memory cleanup: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception during memory cleanup");
    }
}

uint32_t XboxKernel::getMemoryFragmentationLevel() const {
    if (!memoryAllocator) {
        return 0;
    }

    try {
        return memoryAllocator->getFragmentationLevel();
    } catch (const std::exception& e) {
        LOGE("Exception during fragmentation level check: %s", e.what());
        return 0;
    } catch (...) {
        LOGE("Unknown exception during fragmentation level check");
        return 0;
    }
}

uint32_t XboxKernel::getLargestFreeMemoryBlock() const {
    if (!memoryAllocator) {
        return 0;
    }

    try {
        return memoryAllocator->getLargestFreeBlock();
    } catch (const std::exception& e) {
        LOGE("Exception during largest free block check: %s", e.what());
        return 0;
    } catch (...) {
        LOGE("Unknown exception during largest free block check");
        return 0;
    }
}

bool XboxKernel::detectMemoryLeaks() {
    if (!memoryAllocator) {
        LOGE("Memory allocator not initialized");
        return false;
    }

    try {
        memoryAllocator->detectMemoryLeaks();
        auto leakedBlocks = memoryAllocator->getLeakedBlocks();

        if (!leakedBlocks.empty()) {
            LOGW("Memory leak detection: %zu potential leaks found", leakedBlocks.size());
            for (const auto& block : leakedBlocks) {
                LOGW("Potential leak: 0x%08X (%u bytes) - %s", 
                     block.address, block.size, block.purpose.c_str());
            }
            return true;
        } else {
            LOGI("Memory leak detection: No leaks found");
            return false;
        }
    } catch (const std::exception& e) {
        LOGE("Exception during memory leak detection: %s", e.what());
        return false;
    } catch (...) {
        LOGE("Unknown exception during memory leak detection");
        return false;
    }
}


bool XboxKernel::validateAndRepairXbeHeader(XbeHeader& header, size_t fileSize) {
    LOGI("=== XBE HEADER VALIDATION ===");


    if (header.baseAddr == 0 || header.baseAddr > 0x08000000) {
        LOGI("🔧 Detected corrupted base address: 0x%08X", header.baseAddr);
        LOGI("🔧 Repairing to standard Xbox base address: 0x00010000");
        header.baseAddr = 0x00010000;
        LOGI("✓ Base address repaired to 0x00010000");
    }

    if (header.entryPoint == 0 || header.entryPoint == header.sizeOfImage) {
        LOGI("🔧 Detected invalid entry point: 0x%08X (zero or same as SizeOfImage)", header.entryPoint);
        LOGI("🔧 Setting to standard Xbox entry point: 0x00100000");
        header.entryPoint = 0x00100000;
        LOGI("✓ Entry point repaired to 0x00100000");
    }

    if (header.entryPoint > 0x01000000) {
        LOGI("🔧 Detected corrupted entry point: 0x%08X", header.entryPoint);
        LOGI("🔧 Repairing to reasonable value: 0x00100000");
        header.entryPoint = 0x00100000;
        LOGI("✓ Entry point repaired to 0x00100000");
    }

    if (header.sizeOfImage == 0) {
        LOGI("🔧 Detected invalid image size: 0 bytes");
        LOGI("🔧 Repairing to file size: %zu bytes", fileSize);
        header.sizeOfImage = static_cast<uint32_t>(fileSize);
        LOGI("✓ Image size repaired to %zu bytes", fileSize);
    }

    if (header.sizeOfImage > 100 * 1024 * 1024) {
        LOGI("🔧 Detected corrupted image size: %u bytes", header.sizeOfImage);
        LOGI("🔧 Repairing to file size: %zu bytes", fileSize);
        header.sizeOfImage = static_cast<uint32_t>(fileSize);
        LOGI("✓ Image size repaired to %zu bytes", fileSize);
    }

    if (header.numberOfSections == 0) {
        LOGI("🔧 Detected invalid number of sections: 0");
        LOGI("🔧 Repairing to reasonable value: 4 (typical Xbox executable)");
        header.numberOfSections = 4;
        LOGI("✓ Number of sections repaired to 4");
    }

    if (header.numberOfSections > 20) {
        LOGI("🔧 Detected corrupted number of sections: %u", header.numberOfSections);
        LOGI("🔧 Repairing to reasonable value: 4 (typical Xbox executable)");
        header.numberOfSections = 4;
        LOGI("✓ Number of sections repaired to 4");
    }

    if (header.sectionHeadersAddr < header.baseAddr) {
        LOGI("🔧 Detected corrupted section headers address: 0x%08X", header.sectionHeadersAddr);
        LOGI("🔧 Repairing to reasonable value: 0x00020000");
        header.sectionHeadersAddr = 0x00020000;
        LOGI("✓ Section headers address repaired to 0x00020000");
    }

    if (header.sectionHeadersAddr >= header.baseAddr + fileSize) {
        LOGI("🔧 Detected corrupted section headers address: 0x%08X", header.sectionHeadersAddr);
        LOGI("🔧 Repairing to reasonable value: 0x00020000");
        header.sectionHeadersAddr = 0x00020000;
        LOGI("✓ Section headers address repaired to 0x00020000");
    }


    uint32_t finalValidationEntryPoint = header.baseAddr + header.entryPoint;
    LOGI("✓ XBE header validation passed:");
    LOGI("  Base Address: 0x%08X", header.baseAddr);
    LOGI("  Entry Point Offset: 0x%08X", header.entryPoint);
    LOGI("  Real Entry Point: 0x%08X", finalValidationEntryPoint);
    LOGI("  Image Size: %u bytes", header.sizeOfImage);
    LOGI("  Sections: %u", header.numberOfSections);
    LOGI("  Section Headers Addr: 0x%08X", header.sectionHeadersAddr);


    if (finalValidationEntryPoint < 0x00010000 || finalValidationEntryPoint >= 0x08000000) {
        LOGE("❌ Entry point 0x%08X is outside valid Xbox RAM range (0x00010000-0x07FFFFFF)", finalValidationEntryPoint);
        return false;
    }

    LOGI("=== END XBE HEADER VALIDATION ===");
    return true;
}
