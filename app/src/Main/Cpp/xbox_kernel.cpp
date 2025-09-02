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
    memoryAllocator(std::make_unique<MemoryAllocator>(0x10000000, 0x10000000)),
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

    syscallNames[0x0001] = "DebugPrint";
    syscallNames[0x0002] = "AllocateMemory";
    syscallNames[0x0003] = "FreeMemory";
    syscallNames[0x0004] = "CreateThread";
    syscallNames[0x0005] = "OpenFile";
    syscallNames[0x0006] = "ReadFile";
    syscallNames[0x0007] = "WriteFile";
    syscallNames[0x0008] = "CloseFile";


    syscallNames[0x0100] = "XboxGetTickCount";
    syscallNames[0x0101] = "XboxGetTime";
    syscallNames[0x0102] = "XboxSleep";
    syscallNames[0x0103] = "XboxGetSystemTime";
    syscallNames[0x0200] = "XboxAudioInit";
    syscallNames[0x0201] = "XboxAudioPlay";
    syscallNames[0x0202] = "XboxAudioStop";
    syscallNames[0x0203] = "XboxAudioSetVolume";
    syscallNames[0x0300] = "XboxGraphicsInit";
    syscallNames[0x0301] = "XboxGraphicsPresent";
    syscallNames[0x0302] = "XboxGraphicsClear";
    syscallNames[0x0303] = "XboxGraphicsSetMode";
    syscallNames[0x0400] = "XboxInputInit";
    syscallNames[0x0401] = "XboxInputGetState";
    syscallNames[0x0402] = "XboxInputSetVibration";
    syscallNames[0x0500] = "XboxNetworkInit";
    syscallNames[0x0501] = "XboxNetworkConnect";
    syscallNames[0x0502] = "XboxNetworkSend";
    syscallNames[0x0503] = "XboxNetworkRecv";
}

void XboxKernel::initializeMemory() {

    MemoryBlock reservedBlock;
    reservedBlock.address = 0x00010000;
    reservedBlock.size = 0x10000;
    reservedBlock.allocated = false;
    reservedBlock.purpose = "Reserved";
    memoryBlocks.push_back(reservedBlock);

    MemoryBlock mainMemoryBlock;
    mainMemoryBlock.address = 0x10000000;
    mainMemoryBlock.size = 0x10000000;
    mainMemoryBlock.allocated = false;
    mainMemoryBlock.purpose = "Main Memory";
    memoryBlocks.push_back(mainMemoryBlock);

    MemoryBlock highMemoryBlock;
    highMemoryBlock.address = 0xE0000000;
    highMemoryBlock.size = 0x4000000;
    highMemoryBlock.allocated = false;
    highMemoryBlock.purpose = "High Memory";
    memoryBlocks.push_back(highMemoryBlock);
}

void XboxKernel::initializeThreads() {

    Thread mainThread;
    mainThread.id = nextThreadId++;
    mainThread.processId = 0;
    mainThread.entryPoint = 0;
    mainThread.stackPointer = 0;
    mainThread.status = ThreadStatus::Running;
    mainThread.priority = ThreadPriority::Normal;

    threads.push_back(mainThread);
    currentThreadId = mainThread.id;
}

bool XboxKernel::loadXbe(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOGE("Failed to open XBE file: %s", path.c_str());
        return false;
    }

    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    file.read(reinterpret_cast<char*>(&currentXbe), sizeof(XbeHeader));
    if (file.gcount() != sizeof(XbeHeader)) {
        LOGE("Failed to read XBE header: expected %zu, got %zd", sizeof(XbeHeader), file.gcount());
        return false;
    }

    if (memcmp(currentXbe.magic, "XBEH", 4) != 0) {
        LOGE("=== XBE-LADE-FEHLER ===");
        GAMELOADED("XBE: Ungültiger XBE Magic - erwartet 'XBEH', erhalten '%c%c%c%c'", 
             currentXbe.magic[0], currentXbe.magic[1], currentXbe.magic[2], currentXbe.magic[3]);
        GAMELOADED("XBE: Datei: %s", path.c_str());
        GAMELOADED("XBE: Dateigröße: %zu Bytes", fileSize);
        GAMELOADED("XBE: Das deutet auf eine beschädigte oder ungültige XBE-Datei hin");
        GAMELOADED("=== ENDE XBE-LADE-FEHLER ===");
        return false;
    }


    LOGE("XBE Header: EntryPoint=0x%08X, BaseAddr=0x%08X, SizeOfImage=%u, Sections=%u", currentXbe.entryPoint, currentXbe.baseAddr, currentXbe.sizeOfImage, currentXbe.numberOfSections);


    GAMELOADED("=== XBE-HEADER-ANALYSE ===");
    GAMELOADED("XBE: Dateipfad: %s", path.c_str());
    GAMELOADED("XBE: Dateigröße: %zu Bytes", fileSize);
    GAMELOADED("XBE: Magic: %c%c%c%c", currentXbe.magic[0], currentXbe.magic[1], currentXbe.magic[2], currentXbe.magic[3]);
    GAMELOADED("XBE: EntryPoint (Roh): 0x%08X", currentXbe.entryPoint);
    GAMELOADED("XBE: BaseAddr (Roh): 0x%08X", currentXbe.baseAddr);
    GAMELOADED("XBE: SizeOfImage (Roh): %u Bytes", currentXbe.sizeOfImage);
    GAMELOADED("XBE: NumberOfSections (Roh): %u", currentXbe.numberOfSections);
    GAMELOADED("XBE: SectionHeadersAddr: 0x%08X", currentXbe.sectionHeadersAddr);
    GAMELOADED("XBE: InitFlags: 0x%08X", currentXbe.initFlags);
    GAMELOADED("XBE: StackCommit: 0x%08X", currentXbe.stackCommit);
    GAMELOADED("XBE: HeapReserve: 0x%08X", currentXbe.heapReserve);
    GAMELOADED("XBE: HeapCommit: 0x%08X", currentXbe.heapCommit);
    GAMELOADED("XBE: PeBaseAddr: 0x%08X", currentXbe.peBaseAddr);
    GAMELOADED("XBE: PeSize: %u", currentXbe.peSize);
    GAMELOADED("XBE: PeChecksum: 0x%08X", currentXbe.peChecksum);
    GAMELOADED("XBE: PeTimestamp: 0x%08X", currentXbe.peTimestamp);
    GAMELOADED("XBE: DebugPathnameAddr: 0x%08X", currentXbe.debugPathnameAddr);
    GAMELOADED("XBE: DebugFilenameAddr: 0x%08X", currentXbe.debugFilenameAddr);
    GAMELOADED("XBE: DebugUnicodeFilenameAddr: 0x%08X", currentXbe.debugUnicodeFilenameAddr);
    GAMELOADED("XBE: KernelThunkAddr: 0x%08X", currentXbe.kernelThunkAddr);
    GAMELOADED("XBE: NonKernelImportDirAddr: 0x%08X", currentXbe.nonKernelImportDirAddr);
    GAMELOADED("XBE: LibraryVersionsAddr: 0x%08X", currentXbe.libraryVersionsAddr);
    GAMELOADED("XBE: LibraryVersionsNum: %u", currentXbe.libraryVersionsNum);
    GAMELOADED("XBE: KernelLibraryVersionAddr: 0x%08X", currentXbe.kernelLibraryVersionAddr);
    GAMELOADED("XBE: XapiLibraryVersionAddr: 0x%08X", currentXbe.xapiLibraryVersionAddr);
    GAMELOADED("XBE: LogoBitmapAddr: 0x%08X", currentXbe.logoBitmapAddr);
    GAMELOADED("XBE: LogoBitmapSize: %u", currentXbe.logoBitmapSize);
    GAMELOADED("=== ENDE XBE-HEADER-ANALYSE ===");


    bool headerSeverelyCorrupted = false;


    if (currentXbe.baseAddr > 0x10000000 || currentXbe.sizeOfImage > 100 * 1024 * 1024 || 
        currentXbe.numberOfSections > 100 || currentXbe.entryPoint > 0x10000000) {
        LOGW("⚠ XBE header appears severely corrupted - using fallback loading method");
        headerSeverelyCorrupted = true;
    }


    xbeData.resize(fileSize);
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(xbeData.data()), fileSize);
    if (file.gcount() != fileSize) {
        LOGW("Partial read of XBE file: expected %zu, got %zd. Padding with zeros.", fileSize, file.gcount());
        std::fill(xbeData.begin() + file.gcount(), xbeData.end(), 0);
    }

    if (headerSeverelyCorrupted) {

        LOGI("=== FALLBACK XBE LOADING FOR CORRUPTED HEADER ===");


        LOGI("=== XEMU-STYLE HEADER REPAIR ===");


        uint32_t detectedEntryPoint = 0x00010000; 
        bool foundValidCode = false;


        uint32_t minHeaderSize = 0x1000; 


        for (uint32_t offset = 0x00010000 + minHeaderSize; offset < 0x00020000 && offset < fileSize; offset += 4) {
            if (offset + 16 >= fileSize) break;

            uint8_t bytes[16];
            memcpy(bytes, xbeData.data() + offset, 16);


            if (bytes[0] == 0x68 || bytes[0] == 0x8B || bytes[0] == 0x89 || bytes[0] == 0xE8) {
                detectedEntryPoint = offset;
                foundValidCode = true;
                LOGE("SECTION_DEBUG: Found valid x86 code at offset 0x%08X (opcode: 0x%02X)", offset, bytes[0]);
                break;
            }
        }

        if (!foundValidCode) {

            detectedEntryPoint = 0x00011000;
            LOGE("SECTION_DEBUG: No valid x86 code found, using fallback entry point: 0x%08X", detectedEntryPoint);
        }


        currentXbe.baseAddr = 0x00010000;
        currentXbe.entryPoint = detectedEntryPoint - 0x00010000; 
        currentXbe.sizeOfImage = std::min(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(50 * 1024 * 1024));
        currentXbe.numberOfSections = 1; 

        LOGI("XEMU: Repaired header - EntryPoint: 0x%08X, BaseAddr: 0x%08X", 
             currentXbe.entryPoint, currentXbe.baseAddr);


        sections.clear();
        sections.resize(1);

        sections[0].flags = SECTION_FLAG_LOADED | SECTION_FLAG_EXECUTABLE;
        sections[0].virtualAddr = 0x00010000;
        sections[0].virtualSize = std::min(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(50 * 1024 * 1024));
        sections[0].fileAddr = 0;
        sections[0].fileSize = fileSize;
        sections[0].name = "FALLBACK_SECTION";


        uint32_t allocSize = alignToPage(sections[0].virtualSize);
        uint32_t address = allocateMemory(allocSize, XBOX_PAGE_SIZE, "XBE Fallback Section");

        if (address == 0) {
            LOGE("Failed to allocate memory for fallback section");
            return false;
        }

        sections[0].virtualAddr = address;
        uint8_t* dest = memory->getRamPointer() + address;
        uint8_t* src = xbeData.data();
        size_t copySize = std::min(static_cast<size_t>(sections[0].virtualSize), static_cast<size_t>(fileSize));

        memcpy(dest, src, copySize);


        if (sections[0].virtualSize > copySize) {
            memset(dest + copySize, 0, sections[0].virtualSize - copySize);
        }

        LOGI("Fallback XBE loaded: VA=0x%08X, Size=%zu bytes", address, copySize);


        uint32_t realEntryPoint = currentXbe.baseAddr + currentXbe.entryPoint;
        if (memory) {
            uint8_t entryBytes[16] = {0};
            for (int i = 0; i < 16 && realEntryPoint + i < 0x08000000; i++) {
                entryBytes[i] = memory->read8(realEntryPoint + i);
            }

            LOGE("Fallback Entry Point Bytes:");
            for (int i = 0; i < 16; i++) {
                LOGE("  Entry+%2d: 0x%02X", i, entryBytes[i]);
            }
        }

    } else {

        bool headerNeedsFix = false;


        if (currentXbe.baseAddr != 0x00010000) {
            LOGW("⚠ Invalid BaseAddr 0x%08X - fixing to standard Xbox base 0x00010000", currentXbe.baseAddr);
            currentXbe.baseAddr = 0x00010000;
            headerNeedsFix = true;
        }


        uint32_t realEntry = currentXbe.baseAddr + currentXbe.entryPoint;
        if (realEntry < 0x00000000 || realEntry >= 0x08000000) {
            LOGW("⚠ Invalid EntryPoint calculation: BaseAddr(0x%08X) + EntryPoint(0x%08X) = 0x%08X", 
                 currentXbe.baseAddr, currentXbe.entryPoint, realEntry);


            if (currentXbe.entryPoint >= 0x08000000) {
                LOGW("⚠ EntryPoint 0x%08X too high - fixing to valid offset 0x00010000", currentXbe.entryPoint);
                currentXbe.entryPoint = 0x00010000;
            } else if (currentXbe.entryPoint < 0x00100000) {
                LOGW("⚠ EntryPoint 0x%08X too low - fixing to valid offset 0x00100000", currentXbe.entryPoint);
                currentXbe.entryPoint = 0x00100000;
            }

            headerNeedsFix = true;
        }


        if (currentXbe.sizeOfImage > 100 * 1024 * 1024 || currentXbe.sizeOfImage == 0) {
            LOGW("⚠ Invalid SizeOfImage %u - fixing to reasonable size", currentXbe.sizeOfImage);
            currentXbe.sizeOfImage = std::min(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(50 * 1024 * 1024));
            headerNeedsFix = true;
        }


        if (currentXbe.numberOfSections > 20 || currentXbe.numberOfSections == 0) {
            LOGW("⚠ Invalid NumSections %u - fixing to reasonable count", currentXbe.numberOfSections);
            currentXbe.numberOfSections = 4; 
            headerNeedsFix = true;
        }

        if (headerNeedsFix) {
            LOGI("✓ XBE header values corrected for Xbox compatibility");
            LOGI("  Corrected Values:");
            LOGI("    EntryPoint: 0x%08X", currentXbe.entryPoint);
            LOGI("    BaseAddr: 0x%08X", currentXbe.baseAddr);
            LOGI("    SizeOfImage: %u bytes", currentXbe.sizeOfImage);
            LOGI("    NumSections: %u", currentXbe.numberOfSections);


            realEntry = currentXbe.baseAddr + currentXbe.entryPoint;
            LOGI("  Corrected RealEntry: 0x%08X", realEntry);
        } else {
            LOGI("✓ XBE header values are valid, no corrections needed");
            LOGI("  RealEntry: 0x%08X", realEntry);
        }

        loadSections();
    }

    LOGI("XBE Loaded: %zu KB", fileSize / 1024);


    LOGE("=== FINALE XBE-LADE-ANALYSE ===");
    LOGE("XBE: File Size: %zu bytes", fileSize);
    LOGE("XBE: Number of Sections: %u", currentXbe.numberOfSections);
    LOGE("XBE: Base Address: 0x%08X", currentXbe.baseAddr);
    LOGE("XBE: Entry Point (Raw): 0x%08X", currentXbe.entryPoint);
    LOGE("XBE: Real Entry Point: 0x%08X", currentXbe.baseAddr + currentXbe.entryPoint);
    LOGE("XBE: Size of Image: %u bytes", currentXbe.sizeOfImage);


    uint32_t realEntryPoint = currentXbe.baseAddr + currentXbe.entryPoint;
    bool entryPointFound = false;

    for (uint32_t i = 0; i < sections.size(); i++) {
        if (sections[i].flags & SECTION_FLAG_LOADED) {
            if (sections[i].virtualAddr <= realEntryPoint && 
                realEntryPoint < sections[i].virtualAddr + sections[i].virtualSize) {
                LOGE("XBE: ✓ Entry Point (0x%08X) liegt in Sektion %d: '%s'", realEntryPoint, i, sections[i].name.c_str());
                entryPointFound = true;
                break;
            }
        }
    }

    if (!entryPointFound) {
        LOGE("XBE: ✗ Entry Point (0x%08X) liegt in KEINER geladenen Sektion!", realEntryPoint);
        LOGE("XBE: ✗ Das ist der Grund für das Problem!");
    }


    if (memory) {
        uint8_t entryBytes[16] = {0};
        for (int i = 0; i < 16 && realEntryPoint + i < 0x08000000; i++) {
            entryBytes[i] = memory->read8(realEntryPoint + i);
        }

        LOGE("XBE: Entry Point Bytes (aus Memory):");
        for (int i = 0; i < 16; i++) {
            LOGE("XBE:   Entry+%2d: 0x%02X", i, entryBytes[i]);
        }


        bool validInstructions = false;
        if (entryBytes[0] == 0x55 || entryBytes[0] == 0x8B || entryBytes[0] == 0x89 || 
            entryBytes[0] == 0x90 || entryBytes[0] == 0xE8 || entryBytes[0] == 0xE9 ||
            entryBytes[0] == 0x50 || entryBytes[0] == 0x51 || entryBytes[0] == 0x52 || entryBytes[0] == 0x53) {
            validInstructions = true;
        }

        if (validInstructions) {
            LOGE("XBE: ✓ Entry Point enthält gültige x86-Instruktionen");
        } else {
            LOGE("XBE: ✗ Entry Point enthält KEINE gültigen x86-Instruktionen!");
            LOGE("XBE: ✗ Das ist ein weiterer Grund für das Problem!");
        }
    }

    LOGE("=== ENDE FINALE XBE-LADE-ANALYSE ===");

    xbeLoaded = true;
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

bool XboxKernel::isXbeLoaded() const {
    return xbeLoaded;
}

uint32_t XboxKernel::getEntryPoint() const {

    LOGW("[KERNEL-DEBUG] getEntryPoint called - xbeLoaded: %s", xbeLoaded ? "TRUE" : "FALSE");


    if (xbeLoaded) {

        uint32_t correctedEntryPoint = currentXbe.baseAddr + currentXbe.entryPoint;

        LOGW("[KERNEL-DEBUG] currentXbe.baseAddr: 0x%08X", currentXbe.baseAddr);
        LOGW("[KERNEL-DEBUG] currentXbe.entryPoint: 0x%08X", currentXbe.entryPoint);
        LOGW("[KERNEL-DEBUG] calculated correctedEntryPoint: 0x%08X", correctedEntryPoint);


        if (correctedEntryPoint >= 0x00000000 && correctedEntryPoint < 0x08000000) {
            LOGW("[KERNEL-DEBUG] Returning valid entry point: 0x%08X", correctedEntryPoint);
            return correctedEntryPoint;
        } else {

            LOGW("⚠ getEntryPoint: Calculated entry point 0x%08X is invalid, returning safe default", correctedEntryPoint);
            return 0x00100000; 
        }
    }


    LOGW("[KERNEL-DEBUG] No XBE loaded, returning default: 0x%08X", 0x00100000);
    return 0x00100000;
}

void XboxKernel::loadSections() {
    if (!xbeLoaded) return;

    sections.clear();
    sections.resize(currentXbe.numberOfSections);


    if (currentXbe.sectionHeadersAddr < currentXbe.baseAddr) {
        LOGE("Invalid section headers address: 0x%08X < 0x%08X", currentXbe.sectionHeadersAddr, currentXbe.baseAddr);
        return;
    }

    uint32_t sectionTableOffset = currentXbe.sectionHeadersAddr - currentXbe.baseAddr;
    if (sectionTableOffset >= xbeData.size()) {
        LOGE("Section headers address out of bounds: 0x%08X >= %zu", sectionTableOffset, xbeData.size());
        return;
    }

    uint8_t* sectionTable = xbeData.data() + sectionTableOffset;

    for (uint32_t i = 0; i < currentXbe.numberOfSections; i++) {

        uint32_t sectionHeaderOffset = i * sizeof(XbeSectionHeader);
        if (sectionTableOffset + sectionHeaderOffset + sizeof(XbeSectionHeader) > xbeData.size()) {
            LOGE("Section header %d out of bounds", i);
            break;
        }

        XbeSectionHeader* sectionHeader = reinterpret_cast<XbeSectionHeader*>(sectionTable + sectionHeaderOffset);


        sections[i].flags = 0;
        sections[i].virtualAddr = 0;
        sections[i].virtualSize = 0;
        sections[i].fileAddr = 0;
        sections[i].fileSize = 0;
        sections[i].name = ""; 


        if (sectionHeader) {
            sections[i].flags = sectionHeader->flags;
            sections[i].virtualAddr = sectionHeader->virtualAddr;
            sections[i].virtualSize = sectionHeader->virtualSize;
            sections[i].fileAddr = sectionHeader->fileAddr;
            sections[i].fileSize = sectionHeader->fileSize;
        }


        if (sectionHeader && sectionHeader->nameAddr != 0 && sectionHeader->nameAddr >= currentXbe.baseAddr) {
            uint32_t nameOffset = sectionHeader->nameAddr - currentXbe.baseAddr;
            if (nameOffset < xbeData.size() && nameOffset + 32 <= xbeData.size()) {
                const char* namePtr = reinterpret_cast<const char*>(xbeData.data() + nameOffset);
                if (namePtr) {

                    std::string name;
                    for (int j = 0; j < 32 && nameOffset + j < xbeData.size(); j++) {
                        char c = namePtr[j];
                        if (c == 0) break;
                        if (c >= 32 && c < 127) { 
                            name += c;
                        } else {
                            break; 
                        }
                    }
                    sections[i].name = name;
                } else {
                    sections[i].name = "";
                }
            } else {
                sections[i].name = "";
            }
        } else {
            sections[i].name = "";
        }

        LOGI("Section %d: flags=0x%08X, VA=0x%08X, size=%u, fileAddr=0x%08X, fileSize=%u, name='%s'",
             i, sections[i].flags, sections[i].virtualAddr, sections[i].virtualSize, 
             sections[i].fileAddr, sections[i].fileSize, sections[i].name.c_str());


        LOGE("=== SEKTION %d DEBUG ===", i);
        LOGE("SEKTION: Name: '%s'", sections[i].name.c_str());
        LOGE("SEKTION: Flags: 0x%08X", sections[i].flags);
        LOGE("SEKTION: Virtual Address: 0x%08X", sections[i].virtualAddr);
        LOGE("SEKTION: Virtual Size: %u bytes", sections[i].virtualSize);
        LOGE("SEKTION: File Address: 0x%08X", sections[i].fileAddr);
        LOGE("SEKTION: File Size: %u bytes", sections[i].fileSize);
        LOGE("SEKTION: Loaded Flag: %s", (sections[i].flags & SECTION_FLAG_LOADED) ? "JA" : "NEIN");
        LOGE("SEKTION: Executable Flag: %s", (sections[i].flags & SECTION_FLAG_EXECUTABLE) ? "JA" : "NEIN");
        LOGE("SEKTION: Writable Flag: %s", (sections[i].flags & SECTION_FLAG_WRITABLE) ? "JA" : "NEIN");
        LOGE("=== ENDE SEKTION %d DEBUG ===", i);

        if (sections[i].flags & SECTION_FLAG_LOADED) {

            if (sections[i].fileAddr >= xbeData.size()) {
                LOGE("Section %d file address out of bounds: 0x%08X >= %zu", i, sections[i].fileAddr, xbeData.size());
                continue;
            }

            uint32_t allocSize = alignToPage(sections[i].virtualSize);
            uint32_t address = allocateMemory(allocSize, XBOX_PAGE_SIZE, "XBE Section");

            if (address == 0) {
                LOGE("Failed to allocate memory for section %d at 0x%08X", i, sections[i].virtualAddr);
                continue;
            }

            sections[i].virtualAddr = address;

            uint8_t* dest = memory->getRamPointer() + address;

            uint8_t* src = xbeData.data() + sections[i].fileAddr;
            size_t copySize = std::min(sections[i].virtualSize, sections[i].fileSize);


            if (sections[i].fileAddr + copySize > xbeData.size()) {
                copySize = xbeData.size() - sections[i].fileAddr;
                LOGE("Section %d copy size adjusted to %zu due to bounds", i, copySize);
            }

            if (copySize > 0) {
                memcpy(dest, src, copySize);
            }

            if (sections[i].virtualSize > copySize) {
                memset(dest + copySize, 0, sections[i].virtualSize - copySize);
            }

            LOGI("Loaded section %d: VA=0x%08X, Size=%d bytes, Flags=0x%08X",
                 i, address, sections[i].virtualSize, sections[i].flags);


            LOGE("=== SEKTION %d LADE-ERFOLG ===", i);
            LOGE("SEKTION: Name: '%s'", sections[i].name.c_str());
            LOGE("SEKTION: Allocated Address: 0x%08X", address);
            LOGE("SEKTION: Virtual Size: %u bytes", sections[i].virtualSize);
            LOGE("SEKTION: File Size: %u bytes", sections[i].fileSize);
            LOGE("SEKTION: Copy Size: %zu bytes", copySize);
            LOGE("SEKTION: Source Address: 0x%p", src);
            LOGE("SEKTION: Destination Address: 0x%p", dest);


            LOGE("SEKTION: Erste 16 Bytes der geladenen Sektion:");
            for (uint32_t j = 0; j < 16 && j < sections[i].virtualSize; j++) {
                LOGE("SEKTION:   Byte %2d: 0x%02X", j, dest[j]);
            }


            uint32_t realEntryPoint = currentXbe.baseAddr + currentXbe.entryPoint;
            if (address <= realEntryPoint && realEntryPoint < address + sections[i].virtualSize) {
                LOGE("SEKTION: ✓ Diese Sektion enthält den Entry Point (0x%08X)", realEntryPoint);
                uint32_t offsetInSection = realEntryPoint - address;
                LOGE("SEKTION: Entry Point Offset in Sektion: 0x%08X", offsetInSection);

                if (offsetInSection < sections[i].virtualSize) {
                    LOGE("SEKTION: Entry Point Bytes:");
                    for (int j = 0; j < 8 && offsetInSection + j < sections[i].virtualSize; j++) {
                        LOGE("SEKTION:   Entry+%d: 0x%02X", j, dest[offsetInSection + j]);
                    }
                }
            } else {
                LOGE("SEKTION: ✗ Diese Sektion enthält NICHT den Entry Point (0x%08X)", realEntryPoint);
            }

            LOGE("=== ENDE SEKTION %d LADE-ERFOLG ===", i);
        }
    }
}

uint32_t XboxKernel::handleSyscall(uint32_t call, uint32_t* args) {

    auto nameIt = syscallNames.find(call);
    const char* callName = (nameIt != syscallNames.end()) ? nameIt->second.c_str() : "Unknown";

    LOGI("Syscall %s (0x%04X) Args: 0x%08X, 0x%08X, 0x%08X, 0x%08X", 
         callName, call, args[0], args[1], args[2], args[3]);

    auto handler = syscallTable.find(call);
    if (handler != syscallTable.end()) {
        return handler->second(args);
    }

    LOGE("Unknown syscall: 0x%04X", call);
    return syscallUnknown(args);
}

void XboxKernel::registerSyscall(uint32_t callId, std::function<uint32_t(uint32_t*)> handler) {
    syscallTable[callId] = handler;
}

void XboxKernel::setDebugOutput(std::function<void(const std::string&)> callback) {
    debugOutput = callback;
}

uint32_t XboxKernel::allocateMemory(uint32_t size, uint32_t alignment, const char* purpose) {

    if (alignment == 0) alignment = XBOX_PAGE_SIZE;
    uint32_t addr = memoryAllocator->allocate(size, alignment, purpose);

    if (addr != 0) {
        MemoryBlock block;
        block.address = addr;
        block.size = size;
        block.allocated = true;
        block.purpose = purpose;
        memoryBlocks.push_back(block);
        return addr;
    }

    size = alignToPage(size);
    alignment = std::max(alignment, XBOX_PAGE_SIZE);

    for (auto it = memoryBlocks.begin(); it != memoryBlocks.end(); ++it) {
        if (!it->allocated && it->size >= size) {
            uint32_t alignedAddr = (it->address + alignment - 1) & ~(alignment - 1);
            uint32_t endAddr = alignedAddr + size;
            uint32_t blockEnd = it->address + it->size;

            if (endAddr <= blockEnd) {

                if (alignedAddr > it->address) {
                    MemoryBlock before;
                    before.address = it->address;
                    before.size = alignedAddr - it->address;
                    before.allocated = false;
                    before.purpose = "Free";
                    memoryBlocks.insert(it, before);
                }

                if (endAddr < blockEnd) {
                    MemoryBlock after;
                    after.address = endAddr;
                    after.size = blockEnd - endAddr;
                    after.allocated = false;
                    after.purpose = "Free";
                    memoryBlocks.insert(std::next(it), after);
                }

                it->address = alignedAddr;
                it->size = size;
                it->allocated = true;
                it->purpose = purpose;

                return alignedAddr;
            }
        }
    }

    LOGE("Memory allocation failed: size=0x%X, align=0x%X, purpose=%s", size, alignment, purpose);
    return 0;
}

uint32_t XboxKernel::allocateMemory(uint32_t size) {
    return allocateMemory(size, XBOX_PAGE_SIZE, "User Allocation");
}

bool XboxKernel::freeMemory(uint32_t address) {

    if (address >= 0x10000000 && address < 0x20000000) {
        memoryAllocator->deallocate(address);

        for (auto it = memoryBlocks.begin(); it != memoryBlocks.end(); ++it) {
            if (it->address == address && it->allocated) {
                memoryBlocks.erase(it);
                return true;
            }
        }
        return true;
    }

    for (auto it = memoryBlocks.begin(); it != memoryBlocks.end(); ++it) {
        if (it->address == address && it->allocated) {
            it->allocated = false;
            it->purpose = "Freed";

            if (it != memoryBlocks.begin()) {
                auto prev = std::prev(it);
                if (!prev->allocated && prev->address + prev->size == address) {
                    prev->size += it->size;
                    memoryBlocks.erase(it);
                    it = prev;
                }
            }

            auto next = std::next(it);
            if (next != memoryBlocks.end() && !next->allocated && 
                it->address + it->size == next->address) {
                it->size += next->size;
                memoryBlocks.erase(next);
            }

            return true;
        }
    }

    LOGE("Memory free failed: address 0x%08X not found", address);
    return false;
}

uint32_t XboxKernel::createThread(uint32_t entryPoint, uint32_t stackSize) {
    if (threads.size() >= MAX_THREADS) {
        LOGE("Thread limit reached (%d)", MAX_THREADS);
        return 0;
    }

    uint32_t stackAddr = allocateMemory(stackSize, XBOX_PAGE_SIZE, "Thread Stack");
    if (!stackAddr) {
        LOGE("Failed to allocate thread stack");
        return 0;
    }

    Thread thread;
    thread.id = nextThreadId++;
    thread.processId = currentProcessId;
    thread.entryPoint = entryPoint;
    thread.stackPointer = stackAddr + stackSize - 4; 
    thread.status = ThreadStatus::Ready;
    thread.priority = ThreadPriority::Normal;

    threads.push_back(thread);
    return thread.id;
}

uint32_t XboxKernel::alignToPage(uint32_t size) {
    return (size + (XBOX_PAGE_SIZE - 1)) & ~(XBOX_PAGE_SIZE - 1);
}

uint32_t XboxKernel::syscallUnknown(uint32_t* args) {
    LOGE("Unknown syscall: args=[0x%08X, 0x%08X, 0x%08X, 0x%08X]",
         args[0], args[1], args[2], args[3]);
    return 0xFFFFFFFF;
}

uint32_t XboxKernel::syscallDebugPrint(uint32_t* args) {
    const char* message = reinterpret_cast<const char*>(args[0]);
    if (debugOutput) debugOutput(message);
    LOGI("Xbox Debug: %s", message);
    return 0;
}

uint32_t XboxKernel::syscallAllocateMemory(uint32_t* args) {
    uint32_t size = args[0];
    uint32_t alignment = args[1] ? args[1] : XBOX_PAGE_SIZE;
    return allocateMemory(size, alignment, "Syscall Allocation");
}

uint32_t XboxKernel::syscallFreeMemory(uint32_t* args) {
    return freeMemory(args[0]) ? 0 : 0xFFFFFFFF;
}

uint32_t XboxKernel::syscallCreateThread(uint32_t* args) {
    uint32_t entry = args[0];
    uint32_t stack = args[1];
    uint32_t priority = args[2] & 0xF;

    if (threads.size() >= MAX_THREADS) {
        LOGE("Thread limit reached");
        return 0;
    }

    Thread thread;
    thread.id = nextThreadId++;
    thread.processId = currentProcessId;
    thread.entryPoint = entry;
    thread.stackPointer = stack;
    thread.status = ThreadStatus::Ready;
    thread.priority = static_cast<ThreadPriority>(priority);

    threads.push_back(thread);
    return thread.id;
}

uint32_t XboxKernel::syscallOpenFile(uint32_t* args) {
    const char* path = reinterpret_cast<const char*>(args[0]);
    int mode = args[1];

    int fd = open(path, mode);
    return (fd >= 0) ? fd : 0xFFFFFFFF;
}

uint32_t XboxKernel::syscallReadFile(uint32_t* args) {
    int fd = args[0];
    void* buffer = reinterpret_cast<void*>(args[1]);
    size_t size = args[2];

    ssize_t result = read(fd, buffer, size);
    return (result >= 0) ? result : 0xFFFFFFFF;
}

uint32_t XboxKernel::syscallWriteFile(uint32_t* args) {
    int fd = args[0];
    const void* buffer = reinterpret_cast<const void*>(args[1]);
    size_t size = args[2];

    ssize_t result = write(fd, buffer, size);
    return (result >= 0) ? result : 0xFFFFFFFF;
}

uint32_t XboxKernel::syscallCloseFile(uint32_t* args) {
    int fd = args[0];
    return (close(fd) == 0) ? 0 : 0xFFFFFFFF;
}

uint32_t XboxKernel::createProcess(const char* name, uint32_t entryPoint) {
    uint32_t pid = ++currentProcessId;
    uint32_t tid = createThread(entryPoint, 0x10000);
    if (!tid) return 0;

    LOGI("Created process %s (PID: %d) with main thread %d", name, pid, tid);
    return pid;
}

bool XboxKernel::terminateThread(uint32_t threadId) {
    for (auto& thread : threads) {
        if (thread.id == threadId) {

            if (thread.stackPointer) {
                freeMemory(thread.stackPointer - 0x10000 + 4);
            }

            thread.status = ThreadStatus::Terminated;

            if (threadId == currentThreadId) {
                schedule();
            }
            return true;
        }
    }
    return false;
}

void XboxKernel::schedule() {

    for (auto& thread : threads) {
        if (thread.status == ThreadStatus::Ready) {

            for (auto& t : threads) {
                if (t.id == currentThreadId) {
                    t.status = ThreadStatus::Ready;
                    break;
                }
            }

            thread.status = ThreadStatus::Running;
            currentThreadId = thread.id;
            break;
        }
    }
}

void XboxKernel::dumpMemoryMap() const {
    if (!debugOutput) return;

    std::string output = "Memory Map:\n";
    for (const auto& block : memoryBlocks) {
        char line[128];
        snprintf(line, sizeof(line), "0x%08X-0x%08X (%6u KB) [%s] %s\n",
                block.address,
                block.address + block.size - 1,
                block.size / 1024,
                block.allocated ? "ALLOC" : "FREE ",
                block.purpose.c_str());
        output += line;
    }
    debugOutput(output);
}

void XboxKernel::dumpThreads() const {
    if (!debugOutput) return;

    std::string output = "Thread List:\n";
    for (const auto& thread : threads) {
        const char* status = "";
        switch (thread.status) {
            case ThreadStatus::Ready: status = "Ready"; break;
            case ThreadStatus::Running: status = "Running"; break;
            case ThreadStatus::Terminated: status = "Terminated"; break;
        }

        char line[128];
        snprintf(line, sizeof(line), "%4d: EP=0x%08X, SP=0x%08X, %s, Prio=%d\n",
                thread.id,
                thread.entryPoint,
                thread.stackPointer,
                status,
                static_cast<int>(thread.priority));
        output += line;
    }
    debugOutput(output);
}

bool XboxKernel::validateXbe() {
    return xbeLoaded && 
           memcmp(currentXbe.magic, "XBEH", 4) == 0 &&
           currentXbe.baseAddr >= 0x10000;
}


uint32_t XboxKernel::syscallXboxGetTickCount(uint32_t* args) {
    (void)args; 
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    return static_cast<uint32_t>(ticks.count());
}

uint32_t XboxKernel::syscallXboxGetTime(uint32_t* args) {
    (void)args; 
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    return static_cast<uint32_t>(seconds.count());
}

uint32_t XboxKernel::syscallXboxSleep(uint32_t* args) {
    uint32_t milliseconds = args[0];
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return 0;
}

uint32_t XboxKernel::syscallXboxGetSystemTime(uint32_t* args) {
    (void)args; 
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
    return static_cast<uint32_t>(microseconds.count());
}


uint32_t XboxKernel::syscallXboxAudioInit(uint32_t* args) {
    (void)args; 
    LOGI("Xbox Audio System initialized");
    return 0; 
}

uint32_t XboxKernel::syscallXboxAudioPlay(uint32_t* args) {
    uint32_t audioBuffer = args[0];
    uint32_t bufferSize = args[1];
    LOGI("Xbox Audio Play: buffer=0x%08X, size=%u", audioBuffer, bufferSize);
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
