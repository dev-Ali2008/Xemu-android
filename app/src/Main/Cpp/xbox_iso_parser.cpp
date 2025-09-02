#include "xbox_iso_parser.h"
#include <iomanip>
#include <vector>
#include <string>
#include <fstream>
#include <cctype>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <queue>
#include <set>
#include <zlib.h>
#include <android/log.h>
#include <functional>
#include <unordered_map>
#include <zlib.h>
#include <limits> 
#include <exception> 
#include <optional>
#include <codecvt>

namespace fs = std::filesystem;


#define LOG_TAG "XboxISOParser"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

uint16_t XboxISOParser::readU16(const uint8_t* data) {
    return data[0] | (data[1] << 8);
}

uint32_t XboxISOParser::readU32(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

uint64_t XboxISOParser::readU64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

std::string XboxISOParser::toUppercase(const std::string& str) {
    std::string res = str;
    std::transform(res.begin(), res.end(), res.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return res;
}

XboxISOParser::XboxISOParser() : sectorSize(2048), xisoOffset(0), gameDataOffset(0), debugOutput(false), strictMode(false) {

    strictMode = false;
    rootEntry.name = "Root";
    rootEntry.isDirectory = true;
    rootEntry.fullPath = "";
}

XboxISOParser::XboxISOParser(XboxISOParser&& other) noexcept 
    : isoFile(std::move(other.isoFile))
    , iso_data(std::move(other.iso_data))
    , xbe_data(std::move(other.xbe_data))
    , rootEntry(std::move(other.rootEntry))
    , pathCache(std::move(other.pathCache))
    , sectorSize(2048) 
    , xisoOffset(other.xisoOffset)
    , debugOutput(other.debugOutput) {
}

XboxISOParser& XboxISOParser::operator=(XboxISOParser&& other) noexcept {
    if (this != &other) {
        if (isoFile.is_open()) {
            isoFile.close();
        }

        isoFile = std::move(other.isoFile);
        iso_data = std::move(other.iso_data);
        xbe_data = std::move(other.xbe_data);
        rootEntry = std::move(other.rootEntry);
        pathCache = std::move(other.pathCache);

        xisoOffset = other.xisoOffset;
        debugOutput = other.debugOutput;
    }
    return *this;
}

XboxISOParser::~XboxISOParser() {
    if (isoFile.is_open()) {
        isoFile.close();
    }
}

void XboxISOParser::setDebugOutput(bool enabled) {
    debugOutput = enabled;
}

bool XboxISOParser::loadISO(const std::string& isoPath, std::string& errorMsg) {
    if (debugOutput) {
        LOGI("[XEMU HACK] ==========================================");
        LOGI("[XEMU HACK] STARTING XEMU-COMPATIBLE ISO LOADING");
        LOGI("[XEMU HACK] ==========================================");
        LOGI("[XEMU HACK] Loading ISO file: %s", isoPath.c_str());
    }

    if (isoFile.is_open()) {
        isoFile.close();
        rootEntry.children.clear();
        pathCache.clear();
        xisoOffset = 0;
    }

    isoFile.open(isoPath, std::ios::binary);
    if (!isoFile.is_open()) {
        errorMsg = "Failed to open ISO file: " + isoPath;
        if (debugOutput) {
            LOGE("[XEMU HACK] ERROR: %s", errorMsg.c_str());
        }
        return false;
    }


    isoFile.seekg(0, std::ios::end);
    std::streamsize fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    if (debugOutput) {
        LOGI("[XEMU HACK] ISO file size: %lld bytes (%.2f GB)", 
               static_cast<long long>(fileSize), 
               static_cast<double>(fileSize) / (1024.0 * 1024.0 * 1024.0));
    }

    if (fileSize < 1024 * 1024) { 
        errorMsg = "ISO file too small: " + std::to_string(fileSize) + " bytes";
        if (debugOutput) {
            LOGE("[XEMU HACK] ERROR: %s", errorMsg.c_str());
        }
        isoFile.close();
        return false;
    }


    if (static_cast<uint64_t>(fileSize) > 50ULL * 1024 * 1024 * 1024) { 
        errorMsg = "ISO file too large: " + std::to_string(fileSize) + " bytes";
        if (debugOutput) {
            LOGE("[XEMU HACK] ERROR: %s", errorMsg.c_str());
        }
        isoFile.close();
        return false;
    }


    static std::string lastLoadedISO;
    if (lastLoadedISO == isoPath) {
        if (debugOutput) {
            LOGI("[XEMU HACK] ISO already loaded, skipping repetition: %s", isoPath.c_str());
        }
        return true; 
    }


    debugOutput = true;

    if (debugOutput) {
        LOGI("[XEMU HACK] Starting partition type detection...");
    }

    PartitionType detectedType = detectPartitionType();
    switch (detectedType) {
        case PartitionType::STANDARD:
            if (debugOutput) {
                LOGI("[XEMU HACK] Detected: STANDARD ISO partition");
            }
            break;
        case PartitionType::XISO:
            if (debugOutput) {
                LOGI("[XEMU HACK] Detected: XISO partition (offset: %u)", xisoOffset);
            }
            break;
        case PartitionType::HIDDEN:
            if (debugOutput) {
                LOGI("[XEMU HACK] Detected: HIDDEN partition (offset: %u)", xisoOffset);
            }
            break;
        case PartitionType::UNKNOWN:
            if (debugOutput) {
                LOGW("[XEMU HACK] WARNING: Unknown partition type, trying standard parsing");
            }
            break;
    }

    if (debugOutput) {
        LOGI("[XEMU HACK] Starting ISO parsing...");
    }


    uint32_t rootSector, rootSize;
    if (!parseXDVDFS(rootSector, rootSize, errorMsg)) {
        if (debugOutput) {
            LOGE("[XEMU HACK] ERROR: XDVDFS parsing failed: %s", errorMsg.c_str());
        }
        return false;
    }


    lastLoadedISO = isoPath;


    if (debugOutput) {
        LOGI("[XEMU HACK] Parsing directory tree from root sector %u, size %u", rootSector, rootSize);
    }
    parseXdvdfsDirectoryTree(rootSector, 0, rootEntry, "");

    if (debugOutput) {
        LOGI("[XEMU HACK] ISO parsing completed successfully");
        LOGI("[XEMU HACK] Root directory has %zu children", rootEntry.children.size());
        LOGI("[XEMU HACK] Path cache contains %zu entries", pathCache.size());
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Available files in ISO:");
        for (const auto& pair : pathCache) {
            const XboxFileEntry* entry = pair.second;
            if (!entry->isDirectory) {
                LOGI("[XEMU HACK]   %s (Size: %u bytes, Sector: %u)", 
                       entry->fullPath.c_str(), entry->size, entry->sector);
            }
        }
        LOGI("[XEMU HACK] ==========================================");
    }

    return true;
}

bool XboxISOParser::loadISO(const uint8_t* data, size_t size, std::string& errorMsg) {
    if (data == nullptr || size == 0) {
        errorMsg = "Invalid ISO data.";
        if (debugOutput) {
            LOGE("[ERROR] %s", errorMsg.c_str());
        }
        return false;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Loading ISO from memory: %zu bytes", size);
    }

    iso_data.assign(data, data + size);

    if (debugOutput) {
        LOGD("[DEBUG] ISO data loaded into memory: %zu bytes", iso_data.size());
    }

    return true;
}

bool XboxISOParser::parse(const uint8_t* data, size_t size, std::string& errorMsg) {
    if (data != nullptr && size > 0) {

        if (!loadISO(data, size, errorMsg)) {
            return false;
        }
    } else {

        if (!isoFile.is_open()) {
            errorMsg = "No ISO file loaded and no data provided.";
            return false;
        }
    }

    std::string xbeName;
    uint32_t xbeSize = 0, xbeSector = 0;

    if (!findAnyXBE(xbeName, xbeSize, xbeSector, errorMsg)) {
        return false;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Parsing XBE: %s (Size: %u bytes, Sector: %u)", 
               xbeName.c_str(), xbeSize, xbeSector);
    }

    xbe_data = readFileData(xbeSector, xbeSize);

    if (xbe_data.empty()) {
        errorMsg = "Failed to read XBE data.";
        if (debugOutput) {
            LOGE("[ERROR] XBE data is empty after reading");
        }
        return false;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Successfully parsed XBE: %zu bytes", xbe_data.size());
    }

    return true;
}

std::vector<uint8_t> XboxISOParser::getXbeData() {
    return xbe_data;
}

bool XboxISOParser::findAnyXBE(std::string& xbeName, uint32_t& xbeSize, uint32_t& xbeSector, std::string& errorMsg) {
    if (debugOutput) {
        LOGI("[XEMU HACK] Searching for XBE files with alternative names...");
    }


    std::vector<std::string> xbeNames = {
        "default.xbe",      
        "main.xbe",         
        "game.xbe",         
        "xbox.xbe",         
        "start.xbe",        
        "boot.xbe",         
        "launch.xbe",       
        "exec.xbe",         
        "run.xbe",          
        "play.xbe"          
    };


    for (const std::string& name : xbeNames) {
        if (debugOutput) {
            LOGD("[XEMU HACK] Trying to find XBE: %s", name.c_str());
        }
        const XboxFileEntry* entry = findEntry(name);
        if (entry && !entry->isDirectory) {
            xbeSize = entry->size;
            xbeSector = entry->sector;
            xbeName = name;


            if (debugOutput) {
                LOGI("[XEMU HACK] === DEKOMPRESSION AKTIVIERT FÜR XBE ===");
                LOGI("[XEMU HACK] Lade XBE-Header mit automatischer Dekompression...");
            }


            std::vector<uint8_t> xbeHeader = readFileData(xbeSector, 256, true); 

            if (xbeHeader.size() >= 0x38) {
                uint32_t entryPoint = readU32(&xbeHeader[0x104]); 
                if (debugOutput) {
                    LOGI("[XEMU HACK] XBE Entry Point: 0x%08X", entryPoint);


                    if (xbeHeader.size() != 256) {
                        LOGI("[XEMU HACK] ✓ Dekompression erfolgreich: Header-Größe: %zu bytes", xbeHeader.size());
                    } else {
                        LOGI("[XEMU HACK] Keine Kompression erkannt - Header wird unverändert geladen");
                    }
                }


                uint32_t xbeSignature = readU32(&xbeHeader[0]);
                if (debugOutput) {
                    LOGI("[XEMU HACK] XBE Signature: 0x%08X (%s)", xbeSignature, 
                         (xbeSignature == 0x48454258) ? "VALID" : "INVALID");
                }
            }

            if (debugOutput) {
                LOGI("[XEMU HACK] Found XBE in directory: %s (Size: %u bytes, Sector: %u)", xbeName.c_str(), xbeSize, xbeSector);
            }
            return true;
        }
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] No XBE found in directory, trying magic scanning...");
    }

    if (findXBEByScanning(xbeName, xbeSize, xbeSector, errorMsg)) {
        if (debugOutput) {
            LOGI("[XEMU HACK] Found XBE by magic scanning: %s (Size: %u bytes, Sector: %u)", 
                   xbeName.c_str(), xbeSize, xbeSector);
        }
        return true;
    }

    if (debugOutput) {
        LOGI("[XISO-ULTRA] Magic-Scan fehlgeschlagen - starte ULTRA-BRUTE-FORCE...");
    }

    std::optional<XBEInfo> xbeInfo = ultraBruteForceXBEScan();
    if (xbeInfo.has_value()) {
        xbeName = xbeInfo->name;
        xbeSize = xbeInfo->size;
        xbeSector = xbeInfo->sector;
        if (debugOutput) {
            LOGI("[XISO-ULTRA] ✓ XBE durch ULTRA-BRUTE-FORCE gefunden: %s bei Offset 0x%X", 
                 xbeName.c_str(), xbeInfo->absoluteOffset);
        }
        return true;
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Last resort: searching for any executable-like files...");
    }


    std::vector<std::string> exeNames = {
        "default.exe", "main.exe", "game.exe", "xbox.exe",
        "default.com", "main.com", "game.com", "xbox.com",
        "default.bin", "main.bin", "game.bin", "xbox.bin"
    };

    for (const std::string& name : exeNames) {
        if (debugOutput) {
            LOGD("[XEMU HACK] Trying to find executable: %s", name.c_str());
        }
        const XboxFileEntry* entry = findEntry(name);
        if (entry && !entry->isDirectory) {
            xbeSize = entry->size;
            xbeSector = entry->sector;
            xbeName = name;
            if (debugOutput) {
                LOGI("[XEMU HACK] Found executable file: %s (Size: %u bytes, Sector: %u)", xbeName.c_str(), xbeSize, xbeSector);
            }
            return true;
        }
    }

    errorMsg = "No XBE or executable files found in ISO";
    return false;
}

std::vector<std::string> XboxISOParser::listAllXBEFiles() {
    std::vector<std::string> xbeFiles;

    if (debugOutput) {
        LOGD("[DEBUG] Searching for all XBE files in ISO...");
        LOGD("[DEBUG] Total entries in path cache: %zu", pathCache.size());
        LOGD("[DEBUG] Root entry has %zu children", rootEntry.children.size());
    }


    std::function<void(const XboxFileEntry&)> searchAllXBE = [&](const XboxFileEntry& entry) {
        if (!entry.isDirectory) {
            std::string upperName = toUppercase(entry.name);
            if (debugOutput) {
                LOGD("[DEBUG] Checking file: %s (upper: %s)", entry.name.c_str(), upperName.c_str());
            }


            if (upperName.find(".XBE") != std::string::npos ||
                upperName.find("DEFAULT.XBE") != std::string::npos ||
                upperName.find("GAME.XBE") != std::string::npos ||
                upperName.find("MAIN.XBE") != std::string::npos ||
                upperName.find("BOOT.XBE") != std::string::npos ||
                upperName.find("START.XBE") != std::string::npos ||
                upperName.find("LAUNCH.XBE") != std::string::npos ||
                upperName.find("XBOX.XBE") != std::string::npos) {


                if (entry.size > 0 && entry.size < 200 * 1024 * 1024) { 
                    xbeFiles.push_back(entry.fullPath);

                    if (debugOutput) {
                        LOGD("[+] Found XBE: %s (Size: %u bytes, Sector: %u)", 
                               entry.fullPath.c_str(), entry.size, entry.sector);
                    }
                } else {
                    if (debugOutput) {
                        LOGW("[WARNING] XBE file size seems invalid: %s (Size: %u bytes)", 
                               entry.fullPath.c_str(), entry.size);
                    }
                }
            }
        } else {
            if (debugOutput) {
                LOGD("[DEBUG] Searching directory: %s (%zu children)", entry.fullPath.c_str(), entry.children.size());
            }
            for (const auto& child : entry.children) {
                searchAllXBE(child);
            }
        }
    };

    searchAllXBE(rootEntry);

    if (debugOutput) {
        LOGI("[DEBUG] Found %zu XBE files in total", xbeFiles.size());
    }

    return xbeFiles;
}

XboxISOParser::PartitionType XboxISOParser::detectPartitionType() {

    char magic[20] = {0};

    LOGD("[DEBUG] Starting ISO format detection...");


    isoFile.seekg(32 * sectorSize, std::ios::beg);
    isoFile.read(magic, sizeof(magic));

    LOGD("[DEBUG] Standard format check: '%s'", std::string(magic, sizeof(magic)).c_str());

    if (std::string(magic, sizeof(magic)) == "MICROSOFT*XBOX*MEDIA") {
        LOGD("[DEBUG] Found standard Xbox ISO format");
        return PartitionType::STANDARD;
    }


    LOGD("[DEBUG] Checking for XISO format...");


    const uint32_t xisoOffsets[] = {0x0, 0x10000, 0x8000, 0x20000, 0x40000, 0x60000, 0x80000};
    const char* xisoSignatures[] = {"XISO", "CXBX", "XBOX", "XBE", "GAME"};

    for (uint32_t offset : xisoOffsets) {
        isoFile.seekg(offset, std::ios::beg);
        isoFile.read(magic, 4);

        LOGD("[DEBUG] Checking offset 0x%X: '%s'", offset, std::string(magic, 4).c_str());

        for (const char* sig : xisoSignatures) {
            if (std::string(magic, 4) == sig) {
                xisoOffset = offset;
                LOGD("[DEBUG] Found XISO signature '%s' at offset 0x%X", sig, offset);
                return PartitionType::XISO;
            }
        }
    }


    const uint32_t searchSectors[] = {0, 16, 32, 64, 128, 256, 512, 1024, 2048};

    for (uint32_t sector : searchSectors) {
        isoFile.seekg(sector * sectorSize, std::ios::beg);
        isoFile.read(magic, sizeof(magic));

        std::string magicStr(magic, sizeof(magic));
        if (magicStr.find("XBOX") != std::string::npos || 
            magicStr.find("MICROSOFT") != std::string::npos) {
            xisoOffset = sector * sectorSize;
            return PartitionType::STANDARD;
        }
    }


    const uint32_t hiddenSectors[] = {0x100000, 0x200000, 0x300000, 0x80000, 0x40000, 0x60000};
    const char* hiddenSignatures[] = {"XBOX_HIDDEN", "HIDDEN_PART", "XBOX_SECRET", "XBOX"};

    for (uint32_t sector : hiddenSectors) {
        isoFile.seekg(sector * sectorSize, std::ios::beg);

        for (const char* sig : hiddenSignatures) {
            size_t sigLen = strlen(sig);
            std::vector<char> buffer(sigLen);
            isoFile.read(buffer.data(), sigLen);

            if (std::string(buffer.data(), sigLen) == sig) {
                xisoOffset = sector * sectorSize;
                return PartitionType::HIDDEN;
            }
        }
    }


    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);


    if (static_cast<uint64_t>(fileSize) > 4ULL * 1024 * 1024 * 1024) { 

        for (uint32_t offset = 0; offset < 1024 * 1024; offset += 2048) {
            isoFile.seekg(offset, std::ios::beg);
            isoFile.read(magic, 8);

            std::string magicStr(magic, 8);
            if (magicStr.find("XBOX") != std::string::npos || 
                magicStr.find("XBE") != std::string::npos) {
                xisoOffset = offset;
                return PartitionType::STANDARD;
            }
        }
    }

    LOGD("[DEBUG] No valid Xbox ISO format detected");
    return PartitionType::UNKNOWN;
}

bool XboxISOParser::parseXISOFormat(uint32_t& rootSector, uint32_t& rootSize, std::string& errorMsg) {
    (void)errorMsg; 
    LOGD("[DEBUG] Attempting to parse XISO format...");





    const uint32_t possibleSectors[] = {
        0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048
    };

    for (uint32_t sector : possibleSectors) {
        uint32_t offset = xisoOffset + (sector * sectorSize);


        if (offset > 0xFFFFFFF) {
            continue;
        }

        isoFile.seekg(offset, std::ios::beg);
        if (!isoFile) {
            continue;
        }


        std::vector<uint8_t> sectorData(sectorSize);
        isoFile.read(reinterpret_cast<char*>(sectorData.data()), sectorSize);

        if (isoFile.gcount() != sectorSize) {
            continue;
        }



        bool hasValidStrings = false;
        int validStringCount = 0;

        for (size_t i = 0; i < sectorSize - 4; i++) {
            if (sectorData[i] >= 32 && sectorData[i] <= 126) { 

                size_t len = 0;
                while (i + len < sectorSize && sectorData[i + len] >= 32 && sectorData[i + len] <= 126) {
                    len++;
                }

                if (len >= 3 && len <= 64) { 
                    std::string potentialName(reinterpret_cast<char*>(&sectorData[i]), len);
                    if (potentialName.find(".XBE") != std::string::npos ||
                        potentialName.find(".XBE") != std::string::npos ||
                        potentialName.find("DEFAULT") != std::string::npos ||
                        potentialName.find("GAME") != std::string::npos) {
                        validStringCount++;
                        hasValidStrings = true;
                    }
                }
                i += len;
            }
        }

        if (hasValidStrings && validStringCount >= 2) {
            LOGD("[DEBUG] Found potential directory structure at sector %u (valid strings: %d)", 
                 sector, validStringCount);


            rootSector = sector;
            rootSize = sectorSize;


            rootEntry.name = "XISO_ROOT";
            rootEntry.sector = rootSector;
            rootEntry.size = rootSize;
            rootEntry.isDirectory = true;
            rootEntry.fullPath = "";

            LOGD("[DEBUG] Set XISO root directory at sector %u, size %u", rootSector, rootSize);
            return true;
        }
    }


    LOGD("[DEBUG] No directory structure found, treating as single XISO game");

    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    rootSector = 0;
    rootSize = static_cast<uint32_t>(fileSize);


    rootEntry.name = "XISO_GAME";
    rootEntry.sector = rootSector;
    rootEntry.size = rootSize;
    rootEntry.isDirectory = true;
    rootEntry.fullPath = "";

    LOGD("[DEBUG] Set XISO root as entire file: sector %u, size %u", rootSector, rootSize);
    return true;
}

bool XboxISOParser::parseXDVDFS(uint32_t& rootSector, uint32_t& rootSize, std::string& errorMsg) {

    const uint32_t possibleOffsets[] = {
        xisoOffset + (32 * sectorSize),
        xisoOffset + (16 * sectorSize),
        xisoOffset + (64 * sectorSize),
        xisoOffset + (128 * sectorSize),
        xisoOffset + (256 * sectorSize),
        xisoOffset + (512 * sectorSize),
        xisoOffset + (1024 * sectorSize),
        xisoOffset + (2048 * sectorSize),
        xisoOffset + (0 * sectorSize),  
        xisoOffset + (8 * sectorSize),  
        xisoOffset + (24 * sectorSize)  
    };

    for (uint32_t offset : possibleOffsets) {

        if (offset > 0xFFFFFFF) {
            continue;
        }

        isoFile.seekg(offset, std::ios::beg);
        if (!isoFile) {
            continue;
        }

        char magic[20];
        isoFile.read(magic, sizeof(magic));
        if (isoFile.gcount() != sizeof(magic)) {
            continue;
        }

        std::string magicStr(magic, sizeof(magic));
        if (debugOutput) {
            LOGD("[DEBUG] Checking offset 0x%X, magic: '%s'", offset, magicStr.c_str());
        }

        if (magicStr.find("MICROSOFT") != std::string::npos || 
            magicStr.find("XBOX") != std::string::npos ||
            magicStr.find("MEDIA") != std::string::npos) {


            uint8_t buffer[8];
            isoFile.read(reinterpret_cast<char*>(buffer), sizeof(buffer));
            if (isoFile.gcount() == sizeof(buffer)) {
                rootSector = readU32(buffer);
                rootSize = readU32(buffer + 4);

                if (debugOutput) {
                    LOGD("[DEBUG] Raw values at offset 0x%X: sector=%u, size=%u", 
                           offset, rootSector, rootSize);
                }


                if (rootSector > 0 && rootSector < 0xFFFFFFF && 
                    rootSize > 0 && rootSize < 1000 * 1024 * 1024) { 

                    if (debugOutput) {
                        LOGD("[DEBUG] Found valid XDVDFS structure at offset 0x%X: sector=%u, size=%u", 
                               offset, rootSector, rootSize);
                    }
                    return true;
                } else {
                    if (debugOutput) {
                        LOGW("[WARNING] Invalid root directory values at offset 0x%X: sector=%u, size=%u", 
                               offset, rootSector, rootSize);
                    }
                }
            }
        }
    }


    if (debugOutput) {
        LOGD("[DEBUG] Standard XDVDFS structure not found, trying alternative parsing...");
    }


    for (uint32_t sector = 0; sector < 1000; sector++) {
        uint32_t offset = xisoOffset + (sector * sectorSize);

        if (offset > 0xFFFFFFF) {
            break;
        }

        isoFile.seekg(offset, std::ios::beg);
        if (!isoFile) {
            continue;
        }


        std::vector<uint8_t> sectorData(sectorSize);
        isoFile.read(reinterpret_cast<char*>(sectorData.data()), sectorSize);

        if (isoFile.gcount() != sectorSize) {
            continue;
        }


        for (uint32_t i = 0; i < sectorSize - 4; i++) {
            uint8_t entryLen = sectorData[i];
            if (entryLen > 0 && entryLen <= 255 && i + entryLen <= sectorSize) {
                uint8_t attr = sectorData[i + 1];


                if ((attr & 0x10) || (attr & 0x20)) { 
                    rootSector = sector;
                    rootSize = sectorSize;

                    if (debugOutput) {
                        LOGD("[DEBUG] Found potential directory structure at sector %u", sector);
                    }
                    return true;
                }
            }
        }
    }

    errorMsg = "Could not find valid Xbox ISO structure";
    return false;
}

bool XboxISOParser::parseDirectory(uint32_t sector, uint32_t size, XboxFileEntry& parent, 
                                 const std::string& parentPath, std::string& errorMsg) {
    if (size == 0) {
        if (debugOutput) {
            LOGD("[+] Skipping empty directory at sector %u", sector);
        }
        return true;
    }
    if (debugOutput) {
        LOGD("[DEBUG] Parsing directory at sector %u, size %u bytes", sector, size);
    }
    std::vector<uint8_t> data = readFileData(sector, size, false);
    if (sector == parent.sector && debugOutput && !data.empty()) {

        char hexDump[3 * 32 + 1] = {0};
        std::string line;
        LOGD("[XISO-DEBUG] Root sector HEXDUMP (first 256 bytes):");
        for (int i = 0; i < 256 && i < (int)data.size(); i += 16) {
            line.clear();
            for (int j = 0; j < 16 && (i + j) < (int)data.size(); ++j) {
                sprintf(hexDump + j * 3, "%02X ", data[i + j]);
            }
            hexDump[16 * 3] = 0;
            line = std::string(hexDump);
            LOGD("[XISO-DEBUG] %04X: %s", i, line.c_str());
        }
    }
    if (data.size() != size) {
        errorMsg = "Failed to read directory data (expected: " + std::to_string(size) + ", got: " + std::to_string(data.size()) + ")";
        if (debugOutput) {
            LOGE("[ERROR] %s", errorMsg.c_str());
        }
        return false;
    }

    uint32_t offset = 0;
    const uint32_t dataSize = static_cast<uint32_t>(data.size());
    uint32_t consecutiveErrors = 0;
    const uint32_t maxConsecutiveErrors = 10; 

    while (offset < dataSize && consecutiveErrors < maxConsecutiveErrors) {

        if (offset + 1 > dataSize) {
            if (debugOutput) {
                LOGD("[DEBUG] Reached end of directory data at offset %u", offset);
            }
            break;
        }

        const uint8_t entryLength = data[offset];


        if (entryLength == 0) {
            uint32_t sectorOffset = offset % sectorSize;
            uint32_t skip = (sectorOffset == 0) ? sectorSize : (sectorSize - sectorOffset);
            if (offset + skip > dataSize) break;
            offset += skip;
            consecutiveErrors = 0; 
            continue;
        }


        if (entryLength < 4) {
            if (debugOutput) {
                LOGW("[WARNING] Invalid entry length %u at offset %u, skipping", entryLength, offset);
            }
            offset += 1;
            consecutiveErrors++;
            continue;
        }

        if (offset + entryLength > dataSize) {
            if (debugOutput) {
                LOGW("[WARNING] Entry length %u exceeds buffer size at offset %u, stopping", entryLength, offset);
            }
            consecutiveErrors++;
            break;
        }


        if (offset + 4 > dataSize) {
            if (debugOutput) {
                LOGW("[WARNING] Not enough data for entry header at offset %u", offset);
            }
            consecutiveErrors++;
            break;
        }

        const uint8_t attr = data[offset + 1];


        uint16_t nameLen = 0;
        if (offset + 4 <= dataSize) {
            nameLen = readU16(&data[offset + 2]);
        }


        if (nameLen > 255 || nameLen == 0) {
            if (debugOutput) {
                LOGW("[WARNING] Invalid name length %u at offset %u, skipping", nameLen, offset);
            }
            offset += 1;
            consecutiveErrors++;
            continue;
        }


        if (offset + 4 + nameLen > dataSize) {
            if (debugOutput) {
                LOGW("[WARNING] Name length %u exceeds buffer size at offset %u, stopping", nameLen, offset);
            }
            consecutiveErrors++;
            break;
        }


        std::string name;
        try {
            name = std::string(reinterpret_cast<const char*>(&data[offset + 4]), nameLen);


            name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());


            std::string sanitizedName = name;
            sanitizedName.erase(std::remove(sanitizedName.begin(), sanitizedName.end(), '\0'), sanitizedName.end());


            if (sanitizedName.empty() || sanitizedName.length() < 1) {
                if (debugOutput) {
                    LOGW("[WARNING] Name is empty after sanitization at offset %u, skipping", offset);
                }
                offset += 1;
                consecutiveErrors++;
                continue;
            }


            name = sanitizedName;


            std::string upperName = toUppercase(name);



            int specialCharCount = 0;
            for (char c : name) {
                if (c < 32 || c > 126) {
                    specialCharCount++;
                }
            }

            if (specialCharCount > name.length() * 0.8) {
                if (debugOutput) {
                    LOGW("[WARNING] Name has too many special characters at offset %u: '%s', skipping", offset, name.c_str());
                }
                offset += 1;
                consecutiveErrors++;
                continue;
            }



        } catch (...) {
            if (debugOutput) {
                LOGW("[WARNING] Failed to read name at offset %u, skipping", offset);
            }
            offset += 1;
            consecutiveErrors++;
            continue;
        }

        std::string fullPath = parentPath.empty() ? name : parentPath + "/" + name;


        uint32_t nameEnd = offset + 4 + nameLen;
        uint32_t padding = (4 - (nameLen % 4)) % 4;
        uint32_t dataOffset = nameEnd + padding;


        if (dataOffset + 8 > dataSize) {
            if (debugOutput) {
                LOGW("[WARNING] Entry data exceeds buffer size at offset %u, stopping", offset);
            }
            consecutiveErrors++;
            break;
        }


        uint32_t entrySector = readU32(&data[dataOffset]);
        uint32_t entrySize = readU32(&data[dataOffset + 4]);

        if (debugOutput) {
            LOGD("[DEBUG] Raw entry values at offset %u: sector=%u, size=%u", 
                   offset, entrySector, entrySize);
        }


        if (entrySector > 0xFFFFFFF) {
            if (debugOutput) {
                LOGW("[WARNING] Invalid sector value at offset %u (sector=%u), skipping", 
                       offset, entrySector);
            }
            offset += 1;
            consecutiveErrors++;
            continue;
        }


        if (entrySize > 0x7FFFFFFF) {
            if (debugOutput) {
                LOGW("[WARNING] Invalid size value at offset %u (size=%u), skipping", 
                       offset, entrySize);
            }
            offset += 1;
            consecutiveErrors++;
            continue;
        }


        if (!(attr & 0x10) && entrySize > 1000 * 1024 * 1024) { 
            if (debugOutput) {
                LOGW("[WARNING] File size seems too large at offset %u (size=%u), but continuing", 
                       offset, entrySize);
            }

        }

        XboxFileEntry entry;
        entry.name = name;
        entry.sector = entrySector;
        entry.size = entrySize;
        entry.isDirectory = (attr & 0x10) != 0;
        entry.isCompressed = (attr & 0x80) != 0;
        entry.isHidden = (attr & 0x02) != 0;
        entry.fullPath = fullPath;

        if (debugOutput && !entry.isDirectory) {
            LOGD("[DEBUG] Found file: %s (sector=%u, size=%u)", 
                   fullPath.c_str(), entry.sector, entry.size);
        }


        if (entry.isDirectory && entry.size > 0 && entry.size < 100 * 1024 * 1024) { 
            if (debugOutput) {
                LOGD("[+] Parsing directory: %s (sector=%u, size=%u)", 
                      fullPath.c_str(), entry.sector, entry.size);
            }

            if (!parseDirectory(entry.sector, entry.size, entry, fullPath, errorMsg)) {

                if (debugOutput) {
                    LOGW("[WARNING] Failed to parse subdirectory %s, continuing", fullPath.c_str());
                }
            }
        }

        parent.children.push_back(entry);
        offset += entryLength;
        consecutiveErrors = 0; 
    }

    if (consecutiveErrors >= maxConsecutiveErrors) {
        if (debugOutput) {
            LOGW("[WARNING] Too many consecutive errors (%u) in directory parsing, stopping", consecutiveErrors);
        }
    }

    if (debugOutput) {
        LOGD("[DEBUG] Parsed directory %s: %zu entries", parentPath.c_str(), parent.children.size());
    }

    return true;
}

bool XboxISOParser::parseDirectoryAlternative(uint32_t sector, uint32_t size, XboxFileEntry& parent, 
                                            const std::string& parentPath, std::string& errorMsg) {
    if (size == 0) {
        if (debugOutput) {
            LOGD("[+] Skipping empty directory at sector %u", sector);
        }
        return true;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Alternative parsing directory at sector %u, size %u bytes", sector, size);
    }

    std::vector<uint8_t> data = readFileData(sector, size, false);
    if (data.size() != size) {
        errorMsg = "Failed to read directory data (expected: " + 
                  std::to_string(size) + ", got: " + 
                  std::to_string(data.size()) + ")";
        if (debugOutput) {
            LOGE("[ERROR] %s", errorMsg.c_str());
        }
        return false;
    }


    uint32_t offset = 0;
    const uint32_t dataSize = static_cast<uint32_t>(data.size());
    uint32_t foundEntries = 0;

    if (debugOutput) {
        LOGD("[DEBUG] Alternative parsing: scanning %u bytes of data", dataSize);
    }

    while (offset < dataSize - 8) { 

        uint8_t potentialLength = data[offset];

        if (potentialLength == 0) {

            offset++;
            continue;
        }

        if (potentialLength < 4 || potentialLength > 255) {

            offset++;
            continue;
        }

        if (offset + potentialLength > dataSize) {

            offset++;
            continue;
        }


        uint8_t attr = data[offset + 1];
        uint16_t nameLen = readU16(&data[offset + 2]);

        if (nameLen == 0 || nameLen > 255) {
            offset++;
            continue;
        }

        if (offset + 4 + nameLen + 8 > dataSize) {
            offset++;
            continue;
        }


        uint32_t nameEnd = offset + 4 + nameLen;
        uint32_t padding = (4 - (nameLen % 4)) % 4;
        uint32_t dataOffset = nameEnd + padding;

        if (dataOffset + 8 > dataSize) {
            offset++;
            continue;
        }


        uint32_t entrySector = readU32(&data[dataOffset]);
        uint32_t entrySize = readU32(&data[dataOffset + 4]);


        if (entrySector > 0xFFFFFFF || entrySize > 0x7FFFFFFF) {
            offset++;
            continue;
        }


        std::string name;
        try {
            name = std::string(reinterpret_cast<const char*>(&data[offset + 4]), nameLen);
            name.erase(std::remove(name.begin(), name.end(), '\0'), name.end());
        } catch (...) {
            offset++;
            continue;
        }

        if (name.empty()) {
            offset++;
            continue;
        }


        XboxFileEntry entry;
        entry.name = name;
        entry.sector = entrySector;
        entry.size = entrySize;
        entry.isDirectory = (attr & 0x10) != 0;
        entry.isCompressed = (attr & 0x80) != 0;
        entry.isHidden = (attr & 0x02) != 0;
        entry.fullPath = parentPath.empty() ? name : parentPath + "/" + name;

        if (debugOutput) {
            LOGD("[DEBUG] Alternative parsing found: %s (sector=%u, size=%u, dir=%s)", 
                   entry.fullPath.c_str(), entry.sector, entry.size, 
                   entry.isDirectory ? "yes" : "no");
        }

        parent.children.push_back(entry);
        foundEntries++;


        offset += potentialLength;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Alternative parsing found %u entries", foundEntries);
    }

    return foundEntries > 0;
}

bool XboxISOParser::findXBEByScanning(std::string& xbeName, uint32_t& xbeSize, uint32_t& xbeSector, std::string& errorMsg) {
    if (!isoFile.is_open()) {
        errorMsg = "ISO file is not open";
        return false;
    }

    if (debugOutput) {
        LOGI("[XEMU HACK] Starting comprehensive XBE magic scanning...");
        LOGI("[XEMU HACK] Mode: %s", strictMode ? "STRICT" : "TOLERANT");
    }



    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    if (debugOutput) {
        LOGD("[XEMU HACK] File size: %lld bytes", static_cast<long long>(fileSize));
    }


    std::vector<std::pair<uint32_t, std::string>> foundXbes;


    if (debugOutput) {
        LOGI("[XEMU HACK] Method 1: Block-wise scanning (every 1MB)");
    }

    const uint32_t blockSize = 1024 * 1024; 
    uint32_t blockCount = static_cast<uint32_t>(fileSize / blockSize);

    for (uint32_t block = 0; block < blockCount; block++) {
        uint64_t blockOffset = static_cast<uint64_t>(block) * blockSize;

        if (blockOffset >= static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            break;
        }

        isoFile.seekg(static_cast<std::streampos>(blockOffset), std::ios::beg);
        if (!isoFile) continue;

        std::vector<uint8_t> blockData(blockSize);
        isoFile.read(reinterpret_cast<char*>(blockData.data()), blockSize);

        if (isoFile.gcount() < 4) continue;


        for (size_t i = 0; i <= blockData.size() - 4; i++) {
            if (blockData[i] == 'X' && blockData[i+1] == 'B' && 
                blockData[i+2] == 'E' && blockData[i+3] == 'H') {

                uint64_t magicOffset = blockOffset + i;
                uint32_t sector = static_cast<uint32_t>(magicOffset / 2048);

                if (debugOutput) {
                    LOGI("[XEMU HACK] Found XBE magic at block %u, offset %lu, sector %u", 
                           block, magicOffset, sector);
                }

                foundXbes.push_back({sector, "XBE_MAGIC_BLOCK_" + std::to_string(block)});
            }
        }
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Method 2: Sector-by-sector scanning with multiple sector sizes");
    }

    std::vector<uint32_t> testSectorSizes = {2048, 2352, 4096, 512};

    for (uint32_t sectorSize : testSectorSizes) {
        uint32_t maxSectors = static_cast<uint32_t>(fileSize / sectorSize);
        maxSectors = std::min(maxSectors, 10000u); 

        for (uint32_t sector = 0; sector < maxSectors; sector++) {
            uint64_t sectorOffset = static_cast<uint64_t>(sector) * sectorSize;

            if (sectorOffset >= static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
                break;
            }

            isoFile.seekg(static_cast<std::streampos>(sectorOffset), std::ios::beg);
            if (!isoFile) continue;

            std::vector<uint8_t> sectorData(sectorSize);
            isoFile.read(reinterpret_cast<char*>(sectorData.data()), sectorSize);

            if (isoFile.gcount() < 4) continue;


            if (sectorData.size() >= 4 && 
                sectorData[0] == 'X' &&
                sectorData[1] == 'B' &&
                sectorData[2] == 'E' &&
                sectorData[3] == 'H') {

                if (debugOutput) {
                    LOGI("[XEMU HACK] Found XBE magic at sector %u (sectorSize=%u), offset %lu", 
                           sector, sectorSize, sectorOffset);
                }

                foundXbes.push_back({sector, "XBE_MAGIC_SECTOR_" + std::to_string(sector) + "_" + std::to_string(sectorSize)});
            }
        }
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Method 3: Byte-by-byte scanning in first 10MB");
    }

    uint32_t scanSize = std::min(static_cast<uint32_t>(10 * 1024 * 1024), static_cast<uint32_t>(fileSize));
    std::vector<uint8_t> scanData(scanSize);

    isoFile.seekg(0, std::ios::beg);
    isoFile.read(reinterpret_cast<char*>(scanData.data()), scanSize);

    for (size_t i = 0; i <= scanData.size() - 4; i++) {
        if (scanData[i] == 'X' && scanData[i+1] == 'B' && 
            scanData[i+2] == 'E' && scanData[i+3] == 'H') {

            uint32_t sector = static_cast<uint32_t>(i / 2048);

            if (debugOutput) {
                LOGI("[XEMU HACK] Found XBE magic at byte %zu, sector %u", i, sector);
            }

            foundXbes.push_back({sector, "XBE_MAGIC_BYTE_" + std::to_string(i)});
        }
    }


    std::sort(foundXbes.begin(), foundXbes.end());
    foundXbes.erase(std::unique(foundXbes.begin(), foundXbes.end()), foundXbes.end());

    if (debugOutput) {
        LOGI("[XEMU HACK] Found %zu unique XBE magic signatures:", foundXbes.size());
        for (const auto& xbe : foundXbes) {
            LOGI("  - Sector %u: %s", xbe.first, xbe.second.c_str());
        }
    }

    if (foundXbes.empty()) {
        errorMsg = "No XBE magic signatures found in ISO";
        return false;
    }


    for (const auto& xbe : foundXbes) {
        uint32_t testSector = xbe.first;
        std::string testName = xbe.second;

        if (debugOutput) {
            LOGI("[XEMU HACK] Attempting to load XBE from sector %u (%s)", testSector, testName.c_str());
        }


        uint32_t testSize = 1024 * 1024;
        std::vector<uint8_t> testData = readFileData(testSector, testSize);

        if (!testData.empty() && testData.size() >= 256) {

            bool validXBE = false;


            if (testData.size() >= 4 && 
                testData[0] == 'X' && testData[1] == 'B' && 
                testData[2] == 'E' && testData[3] == 'H') {


                if (testData.size() >= 0x38) {
                    uint32_t baseAddress = *reinterpret_cast<uint32_t*>(&testData[0x24]);
                    uint32_t sizeOfHeaders = *reinterpret_cast<uint32_t*>(&testData[0x28]);
                    uint32_t sizeOfImage = *reinterpret_cast<uint32_t*>(&testData[0x2C]);

                    if (debugOutput) {
                        LOGI("[XEMU HACK] XBE header: baseAddress=0x%08X, sizeOfHeaders=%u, sizeOfImage=%u", 
                               baseAddress, sizeOfHeaders, sizeOfImage);
                    }


                    if (sizeOfHeaders > 0 && sizeOfHeaders < 1024 * 1024 && 
                        sizeOfImage > 0 && sizeOfImage < 100 * 1024 * 1024) {
                        validXBE = true;
                        if (debugOutput) {
                            LOGI("[XEMU HACK] XBE header validation passed");
                        }
                    } else {
                        if (debugOutput) {
                            LOGW("[XEMU HACK] XBE header validation failed, but continuing anyway");
                        }
                    }
                }


                if (!validXBE) {
                    if (strictMode) {
                        if (debugOutput) {
                            LOGW("[XEMU HACK] STRICT MODE: XBE header validation failed, skipping");
                        }
                        continue; 
                    } else {
                        if (debugOutput) {
                            LOGW("[XEMU HACK] TOLERANT MODE: Using XBE despite failed header validation");
                        }
                        validXBE = true; 
                    }
                }

                if (validXBE) {
                    xbeName = testName;
                    xbeSize = testSize;
                    xbeSector = testSector;

                    if (debugOutput) {
                        LOGI("[XEMU HACK] Successfully found working XBE: %s (Size: %u bytes, Sector: %u)", 
                               xbeName.c_str(), xbeSize, xbeSector);
                    }

                    return true;
                }
            }
        }

        if (debugOutput) {
            LOGW("[XEMU HACK] Failed to load XBE from sector %u, trying next...", testSector);
        }
    }

    errorMsg = "All found XBE magic signatures failed to load";
    return false;
}

bool XboxISOParser::loadXISOAsGame(std::string& gameName, uint32_t& gameSize, uint32_t& gameSector, std::string& errorMsg) {
    if (debugOutput) {
        LOGI("[XEMU HACK] Attempting to load XISO as game with fallback mechanisms...");
    }




    if (debugOutput) {
        LOGI("[XEMU HACK] Strategy 1: Scanning entire file for XBE magic");
    }

    std::string xbeName;
    uint32_t xbeSize, xbeSector;

    if (findXBEByScanning(xbeName, xbeSize, xbeSector, errorMsg)) {
        gameName = xbeName;
        gameSize = xbeSize;
        gameSector = xbeSector;

        if (debugOutput) {
            LOGI("[XEMU HACK] Strategy 1 successful: Found XBE magic");
        }
        return true;
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Strategy 2: Looking for executable-like files");
    }

    if (findAnyXBE(gameName, gameSize, gameSector, errorMsg)) {
        if (debugOutput) {
            LOGI("[XEMU HACK] Strategy 2 successful: Found executable file");
        }
        return true;
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Strategy 3: Finding largest file in ISO");
    }

    const XboxFileEntry* largestFile = nullptr;
    uint32_t largestSize = 0;

    for (const auto& pair : pathCache) {
        const XboxFileEntry* entry = pair.second;
        if (!entry->isDirectory && entry->size > largestSize) {
            largestSize = entry->size;
            largestFile = entry;
        }
    }

    if (largestFile != nullptr && largestFile->size > 0) {
        gameName = largestFile->fullPath;
        gameSize = largestFile->size;
        gameSector = largestFile->sector;


        if (debugOutput) {
            LOGI("[XEMU HACK] === DEKOMPRESSION AKTIVIERT FÜR XISO-SPIEL ===");
            LOGI("[XEMU HACK] Lade XISO-Spiel-Header mit automatischer Dekompression...");
        }


        std::vector<uint8_t> gameHeader = readFileData(gameSector, std::min(1024u, gameSize), true); 

        if (gameHeader.size() >= 8) {
            if (debugOutput) {
                LOGI("[XEMU HACK] XISO-Spiel-Header geladen: %zu bytes", gameHeader.size());


                if (gameHeader.size() != std::min(1024u, gameSize)) {
                    LOGI("[XEMU HACK] ✓ Dekompression erfolgreich: Header-Größe geändert");
                } else {
                    LOGI("[XEMU HACK] Keine Kompression erkannt - Header wird unverändert geladen");
                }


                if (memcmp(gameHeader.data(), "XISO", 4) == 0) {
                    LOGI("[XEMU HACK] ✓ XISO-Kompression im Spiel-Header erkannt!");
                } else if (memcmp(gameHeader.data(), "XBC", 3) == 0) {
                    LOGI("[XEMU HACK] ✓ XBC-Kompression im Spiel-Header erkannt!");
                } else if (memcmp(gameHeader.data(), "PK", 2) == 0) {
                    LOGI("[XEMU HACK] ✓ ZIP-Kompression im Spiel-Header erkannt!");
                } else if (memcmp(gameHeader.data(), "Rar!", 4) == 0) {
                    LOGI("[XEMU HACK] ✓ RAR-Kompression im Spiel-Header erkannt!");
                } else if (memcmp(gameHeader.data(), "7z", 2) == 0) {
                    LOGI("[XEMU HACK] ✓ 7Z-Kompression im Spiel-Header erkannt!");
                } else if (memcmp(gameHeader.data(), "\x1f\x8b", 2) == 0) {
                    LOGI("[XEMU HACK] ✓ GZIP-Kompression im Spiel-Header erkannt!");
                } else if (memcmp(gameHeader.data(), "MICROSOFT*XBOX*MEDIA", 8) == 0) {
                    LOGI("[XEMU HACK] ✓ Xbox ISO-Format im Spiel-Header erkannt!");
                }
            }
        }

        if (debugOutput) {
            LOGI("[XEMU HACK] Strategy 3 successful: Using largest file %s (%u bytes)", 
                   gameName.c_str(), gameSize);
        }
        return true;
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Strategy 4: Scanning for non-zero data blocks");
    }

    if (!isoFile.is_open()) {
        errorMsg = "ISO file is not open";
        return false;
    }


    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    if (debugOutput) {
        LOGD("[XEMU HACK] File size: %lld bytes", static_cast<long long>(fileSize));
    }


    const uint32_t blockSize = 1024 * 1024;
    uint32_t blockCount = static_cast<uint32_t>(fileSize / blockSize);

    for (uint32_t block = 0; block < blockCount; block++) {
        uint64_t blockOffset = static_cast<uint64_t>(block) * blockSize;

        if (blockOffset >= static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            break;
        }

        isoFile.seekg(static_cast<std::streampos>(blockOffset), std::ios::beg);
        if (!isoFile) continue;

        std::vector<uint8_t> blockData(blockSize);
        isoFile.read(reinterpret_cast<char*>(blockData.data()), blockSize);

        if (isoFile.gcount() < 256) continue;


        bool hasData = false;
        for (size_t i = 0; i < std::min(static_cast<size_t>(256), blockData.size()); i++) {
            if (blockData[i] != 0) {
                hasData = true;
                break;
            }
        }

        if (hasData) {
            uint32_t sector = static_cast<uint32_t>(blockOffset / 2048);

            if (debugOutput) {
                LOGI("[XEMU HACK] Strategy 4 successful: Found data in block %u, sector %u", block, sector);
            }

            gameName = "xiso_data_block_" + std::to_string(block);
            gameSize = blockSize;
            gameSector = sector;
            return true;
        }
    }


    if (debugOutput) {
        LOGI("[XEMU HACK] Strategy 5: Last resort - using first 1MB of file");
    }

    isoFile.seekg(0, std::ios::beg);
    if (isoFile) {
        uint32_t fallbackSize = std::min(static_cast<uint32_t>(1024 * 1024), static_cast<uint32_t>(fileSize));

        if (fallbackSize > 0) {
            gameName = "xiso_fallback";
            gameSize = fallbackSize;
            gameSector = 0;

            if (debugOutput) {
                LOGI("[XEMU HACK] Strategy 5 successful: Using first %u bytes as fallback", fallbackSize);
            }
            return true;
        }
    }

    errorMsg = "All XISO loading strategies failed";
    return false;
}

std::string XboxISOParser::sanitizeGameName(const std::string& name) {
    std::string sanitized = name;
    std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
    sanitized.erase(std::remove_if(sanitized.begin(), sanitized.end(),
        [](char c) { return !isalnum(c) && c != '_'; }), sanitized.end());
    return sanitized;
}

std::string XboxISOParser::getAndroidGamePath(const std::string& gameName) {
    std::string sanitized = sanitizeGameName(gameName);
    return "/data/user/0/com.xanite/files/" + sanitized;
}

bool XboxISOParser::setupGameDirectory(const std::string& gameName)
{

    std::string androidPath = getAndroidGamePath(gameName);



    LOGI("[INSTALL] Setting up game directory for: %s", gameName.c_str());
    LOGI("[INSTALL] Android path: %s", androidPath.c_str());

    try {

        if (!fs::exists(androidPath)) {
            LOGI("[INSTALL] Creating game directory: %s", androidPath.c_str());

            if (!fs::create_directories(androidPath)) {
                LOGE("[INSTALL] Failed to create game directory: %s", androidPath.c_str());
                return false;
            }


            fs::permissions(androidPath, 
                fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                fs::perms::group_read | fs::perms::group_exec |
                fs::perms::others_read | fs::perms::others_exec);

            LOGI("[INSTALL] Game directory created successfully with proper permissions");
        } else {
            LOGI("[INSTALL] Game directory already exists: %s", androidPath.c_str());
        }


        std::string testFile = androidPath + "/install_test.tmp";
        std::ofstream testStream(testFile);
        if (!testStream.is_open()) {
            LOGE("[INSTALL] Cannot write to game directory: %s", androidPath.c_str());
            return false;
        }
        testStream << "install_test";
        testStream.close();
        std::remove(testFile.c_str());

        LOGI("[INSTALL] Write permission verified successfully");

        return true;

    } catch (const std::exception& e) {
        LOGE("[INSTALL] Exception during directory setup: %s", e.what());
        return false;
    }
}

bool XboxISOParser::convertISOToXBE(const std::string& isoPath, const std::string& outputDir) {
    std::string errorMsg;
    if (!loadISO(isoPath, errorMsg)) {
        LOGE("Failed to load ISO: %s", errorMsg.c_str());
        return false;
    }


    if (!isoFile.is_open()) {
        LOGE("ISO file is not open after loading");
        return false;
    }

    std::string xbePath;
    uint32_t xbeSize, xbeSector;
    if (!findMainXBE(xbePath, xbeSize, xbeSector, errorMsg)) {
        LOGE("No XBE found: %s", errorMsg.c_str());
        return false;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Reading XBE data: %s (Size: %u bytes, Sector: %u)", 
               xbePath.c_str(), xbeSize, xbeSector);
    }


    if (xbeSize > 500 * 1024 * 1024) { 
        LOGE("XBE size too large: %u bytes", xbeSize);
        return false;
    }

    std::vector<uint8_t> xbeData = readFileData(xbeSector, xbeSize);

    if (xbeData.empty()) {
        LOGE("Failed to read XBE data (empty result)");
        return false;
    }

    if (xbeData.size() != xbeSize) {
        LOGE("XBE read incomplete: expected %u bytes, got %zu bytes", 
                           xbeSize, xbeData.size());
        return false;
    }

    std::string outputXbePath = outputDir + "/default.xbe";
    std::ofstream outXbe(outputXbePath, std::ios::binary);
    if (!outXbe) {
        LOGE("Failed to create output XBE file: %s", outputXbePath.c_str());
        return false;
    }

    outXbe.write(reinterpret_cast<const char*>(xbeData.data()), xbeData.size());
    outXbe.close();

    if (debugOutput) {
        LOGD("[+] Successfully wrote XBE file: %s (%zu bytes)", outputXbePath.c_str(), xbeData.size());
    }

    return extractAllFiles(outputDir);
}

bool XboxISOParser::processISO(const std::string& isoPath) {

    fs::path pathObj(isoPath);
    std::string gameName = pathObj.stem().string(); 

    std::string androidPath = getAndroidGamePath(gameName);
    if (!setupGameDirectory(gameName)) {
        LOGE("Failed to create game directory: %s", androidPath.c_str());
        return false;
    }

    return convertISOToXBE(isoPath, androidPath);
}



void XboxISOParser::buildPathCache(XboxFileEntry& entry) {
    std::string upperPath = toUppercase(entry.fullPath);
    pathCache[upperPath] = &entry;

    if (debugOutput) {
        if (!entry.isDirectory) {
            LOGD("[+] File: %s (sector=%u, size=%u, hidden=%s, compressed=%s)",
                  entry.fullPath.c_str(), entry.sector, entry.size,
                  entry.isHidden ? "yes" : "no", entry.isCompressed ? "yes" : "no");
        } else {
            LOGD("[+] Directory: %s (%zu children)", entry.fullPath.c_str(), entry.children.size());
        }
    }

    for (auto& child : entry.children) {
        buildPathCache(child);
    }
}

const XboxFileEntry* XboxISOParser::findEntry(const std::string& path) const {
    std::string upperPath = toUppercase(path);

    if (debugOutput) {
        LOGD("[DEBUG] Looking for entry: %s (upper: %s)", path.c_str(), upperPath.c_str());
    }

    auto it = pathCache.find(upperPath);
    if (it != pathCache.end()) {
        if (debugOutput) {
            LOGD("[DEBUG] Found entry: %s", it->second->fullPath.c_str());
        }
        return it->second;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Entry not found: %s", path.c_str());
        LOGD("[DEBUG] Available entries in cache:");
        for (const auto& pair : pathCache) {
            LOGD("[DEBUG]   %s", pair.first.c_str());
        }
    }

    return nullptr;
}

bool XboxISOParser::fileExists(const std::string& path) const {
    return findEntry(path) != nullptr;
}

const XboxFileEntry* XboxISOParser::getRootEntry() const {
    return &rootEntry;
}

bool XboxISOParser::findMainXBE(std::string& xbePath, uint32_t& xbeSize, uint32_t& xbeSector, std::string& errorMsg) {
    if (debugOutput) {
        LOGD("[DEBUG] Searching for main XBE file...");
    }


    std::vector<std::string> xbeNames = {
        "default.xbe",
        "DEFAULT.XBE",
        "Default.xbe",
        "game.xbe",
        "GAME.XBE",
        "Game.xbe",
        "main.xbe",
        "MAIN.XBE",
        "Main.xbe",
        "boot.xbe",
        "BOOT.XBE",
        "Boot.xbe",
        "start.xbe",
        "START.XBE",
        "Start.xbe",
        "launch.xbe",
        "LAUNCH.XBE",
        "Launch.xbe",
        "xbox.xbe",
        "XBOX.XBE",
        "Xbox.xbe"
    };

    for (const auto& xbeName : xbeNames) {
        if (debugOutput) {
            LOGD("[DEBUG] Checking for XBE: %s", xbeName.c_str());
        }

        const XboxFileEntry* entry = findEntry(xbeName);
        if (entry && !entry->isDirectory) {
            xbePath = entry->fullPath;
            xbeSize = entry->size;
            xbeSector = entry->sector;


            if (debugOutput) {
                LOGI("[XBE-DEBUG] === DEKOMPRESSION AKTIVIERT ===");
                LOGI("[XBE-DEBUG] Lade XBE-Header mit automatischer Dekompression...");
            }


            std::vector<uint8_t> xbeHeader = readFileData(xbeSector, 256, true); 

            if (xbeHeader.size() >= 0x38) {
                uint32_t entryPoint = readU32(&xbeHeader[0x104]); 
                if (debugOutput) {
                    LOGI("[XBE-DEBUG] XBE Entry Point: 0x%08X", entryPoint);


                    if (xbeHeader.size() != 256) {
                        LOGI("[XBE-DEBUG] ✓ Dekompression erfolgreich: Header-Größe: %zu bytes", xbeHeader.size());
                    } else {
                        LOGI("[XBE-DEBUG] Keine Kompression erkannt - Header wird unverändert geladen");
                    }
                }


                uint32_t xbeSignature = readU32(&xbeHeader[0]);
                if (debugOutput) {
                    LOGI("[XBE-DEBUG] XBE Signature: 0x%08X (%s)", xbeSignature, 
                         (xbeSignature == 0x48454258) ? "VALID" : "INVALID");
                }


                if (xbeHeader.size() >= 8) {
                    if (memcmp(xbeHeader.data(), "XISO", 4) == 0) {
                        LOGI("[XBE-DEBUG] ✓ XISO-Kompression im XBE-Header erkannt!");
                    } else if (memcmp(xbeHeader.data(), "XBC", 3) == 0) {
                        LOGI("[XBE-DEBUG] ✓ XBC-Kompression im XBE-Header erkannt!");
                    } else if (memcmp(xbeHeader.data(), "PK", 2) == 0) {
                        LOGI("[XBE-DEBUG] ✓ ZIP-Kompression im XBE-Header erkannt!");
                    } else if (memcmp(xbeHeader.data(), "Rar!", 4) == 0) {
                        LOGI("[XBE-DEBUG] ✓ RAR-Kompression im XBE-Header erkannt!");
                    } else if (memcmp(xbeHeader.data(), "7z", 2) == 0) {
                        LOGI("[XBE-DEBUG] ✓ 7Z-Kompression im XBE-Header erkannt!");
                    } else if (memcmp(xbeHeader.data(), "\x1f\x8b", 2) == 0) {
                        LOGI("[XBE-DEBUG] ✓ GZIP-Kompression im XBE-Header erkannt!");
                    }
                }
            } else {
                if (debugOutput) {
                    LOGW("[XBE-DEBUG] ⚠ XBE-Header zu klein: %zu bytes (erwartet: 256 bytes)", xbeHeader.size());
                }
            }

            if (debugOutput) {
                LOGD("[+] Found main XBE: %s (Size: %u bytes, Sector: %u)", 
                       xbePath.c_str(), xbeSize, xbeSector);
            }
            return true;
        }
    }

    if (debugOutput) {
        LOGD("[DEBUG] No main XBE found, searching for any XBE file...");
    }


    return findAnyXBE(xbePath, xbeSize, xbeSector, errorMsg);
}

std::vector<uint8_t> XboxISOParser::readFileData(uint32_t sector, uint32_t size, bool decompress) {
    std::vector<uint8_t> data;
    if (size == 0) return data;


    if (sector > 0xFFFFFFF) {
        if (debugOutput) {
            LOGE("[ERROR] Invalid sector number: %u", sector);
        }
        return data;
    }

    if (size > 1000 * 1024 * 1024) { 
        if (debugOutput) {
            LOGW("[WARNING] Requested size too large: %u bytes, limiting to 1GB", size);
        }
        size = 1000 * 1024 * 1024;
    }

    if (debugOutput) {
        LOGD("[DEBUG] Reading file data: sector=%u, size=%u bytes", sector, size);
    }

    if (!iso_data.empty()) {

        size_t offset = static_cast<size_t>(sector) * 2048;
        if (offset >= iso_data.size()) {
            if (debugOutput) {
                LOGE("[ERROR] Sector offset %zu exceeds available data size %zu", offset, iso_data.size());
            }
            return data;
        }

        if (offset + size > iso_data.size()) {
            if (debugOutput) {
                LOGW("[WARNING] Requested size exceeds available data. Requested: %u, Available: %zu", 
                       size, iso_data.size() - offset);
            }
            size = static_cast<uint32_t>(iso_data.size() - offset);
        }

        if (size > 0) {
            try {
                data = std::vector<uint8_t>(iso_data.begin() + offset, iso_data.begin() + offset + size);
            } catch (const std::exception& e) {
                if (debugOutput) {
                    LOGE("[ERROR] Failed to copy data from memory: %s", e.what());
                }
                return data;
            }
        }
    } else {

        if (!isoFile.is_open()) {
            if (debugOutput) {
                LOGE("[ERROR] ISO file is not open!");
            }
            return data;
        }


        std::vector<uint32_t> sectorSizes = {2048, 2352, 4096, 512};
        std::vector<uint32_t> xisoOffsets = {xisoOffset, 0, 16, 32, 64, 128, 256, 512, 1024, 2048};

        bool readSuccess = false;

        for (uint32_t testSectorSize : sectorSizes) {
            for (uint32_t testXisoOffset : xisoOffsets) {

                uint64_t absoluteOffset = static_cast<uint64_t>(testXisoOffset) + static_cast<uint64_t>(sector) * static_cast<uint64_t>(testSectorSize);

                if (debugOutput) {
                    LOGD("[DEBUG] XEMU HACK: Trying sectorSize=%u, xisoOffset=%u, absoluteOffset=%lu", 
                           testSectorSize, testXisoOffset, absoluteOffset);
                }


                if (absoluteOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
                    if (debugOutput) {
                        LOGW("[WARNING] Position %lu is too large for streampos, trying next combination", absoluteOffset);
                    }
                    continue;
                }


                isoFile.seekg(0, std::ios::end);
                std::streampos fileSize = isoFile.tellg();
                isoFile.seekg(0, std::ios::beg);

                if (static_cast<uint64_t>(absoluteOffset) >= static_cast<uint64_t>(fileSize)) {
                    if (debugOutput) {
                        LOGW("[WARNING] Position %lu is beyond file size %lld, trying next combination", 
                               absoluteOffset, static_cast<long long>(fileSize));
                    }
                    continue;
                }

                std::streampos pos = static_cast<std::streampos>(absoluteOffset);
                isoFile.seekg(pos, std::ios::beg);

                if (!isoFile) {
                    if (debugOutput) {
                        LOGW("[WARNING] Failed to seek to position %lld, trying next combination", static_cast<long long>(pos));
                    }
                    continue;
                }


                std::vector<uint8_t> testBuffer(256);
                isoFile.read(reinterpret_cast<char*>(testBuffer.data()), 256);

                if (isoFile.gcount() == 0) {
                    if (debugOutput) {
                        LOGW("[WARNING] No data readable from position %lld, trying next combination", static_cast<long long>(pos));
                    }
                    continue;
                }


                bool hasValidData = false;
                for (size_t i = 0; i < std::min(static_cast<size_t>(256), testBuffer.size()); i++) {
                    if (testBuffer[i] != 0) {
                        hasValidData = true;
                        break;
                    }
                }

                if (!hasValidData) {
                    if (debugOutput) {
                        LOGW("[WARNING] Position %lld contains only zeros, trying next combination", static_cast<long long>(pos));
                    }
                    continue;
                }


                if (debugOutput) {
                    LOGI("[XEMU HACK] Found working combination: sectorSize=%u, xisoOffset=%u, absoluteOffset=%lu", 
                           testSectorSize, testXisoOffset, absoluteOffset);
                }


                sectorSize = testSectorSize;
                xisoOffset = testXisoOffset;


                isoFile.seekg(pos, std::ios::beg);


                std::streampos availableSize = fileSize - static_cast<std::streampos>(pos);
                uint32_t actualSize = size;
                if (static_cast<uint32_t>(availableSize) < size) {
                    if (debugOutput) {
                        LOGW("[WARNING] Requested size %u exceeds available size %lld, limiting read", 
                               size, static_cast<long long>(availableSize));
                    }
                    actualSize = static_cast<uint32_t>(availableSize);
                }

                if (actualSize == 0) {
                    if (debugOutput) {
                        LOGE("[ERROR] No data available to read");
                    }
                    continue;
                }

                try {
                    data.resize(actualSize);
                    isoFile.read(reinterpret_cast<char*>(data.data()), actualSize);

                    std::streamsize bytesRead = isoFile.gcount();
                    if (debugOutput) {
                        LOGD("[DEBUG] Successfully read %lld bytes from file", static_cast<long long>(bytesRead));
                    }

                    if (static_cast<uint32_t>(bytesRead) != actualSize) {
                        if (debugOutput) {
                            LOGW("[WARNING] Read incomplete data. Expected: %u, Got: %lld", 
                                   actualSize, static_cast<long long>(bytesRead));
                        }
                        data.resize(static_cast<size_t>(bytesRead));
                    }

                    readSuccess = true;
                    break;

                } catch (const std::exception& e) {
                    if (debugOutput) {
                        LOGE("[ERROR] Exception during file read: %s, trying next combination", e.what());
                    }
                    continue;
                }
            }

            if (readSuccess) {
                break;
            }
        }


        if (!readSuccess && data.empty()) {
            if (debugOutput) {
                LOGW("[XEMU HACK] All sector size/offset combinations failed, trying to read from file beginning");
            }

            isoFile.seekg(0, std::ios::beg);
            if (isoFile) {
                uint32_t fallbackSize = std::min(size, (uint32_t)(1024 * 1024)); 

                try {
                    data.resize(fallbackSize);
                    isoFile.read(reinterpret_cast<char*>(data.data()), fallbackSize);

                    std::streamsize bytesRead = isoFile.gcount();
                    if (debugOutput) {
                        LOGI("[XEMU HACK] Fallback read successful: %lld bytes from file beginning", 
                               static_cast<long long>(bytesRead));
                    }

                    if (static_cast<uint32_t>(bytesRead) != fallbackSize) {
                        data.resize(static_cast<size_t>(bytesRead));
                    }

                } catch (const std::exception& e) {
                    if (debugOutput) {
                        LOGE("[ERROR] Fallback read also failed: %s", e.what());
                    }
                }
            }
        }
    }

    if (debugOutput) {
        LOGD("[DEBUG] Final result: read %zu bytes of data", data.size());
    }


    bool allZeros = true;
    for (size_t i = 0; i < std::min(data.size(), static_cast<size_t>(1024)); i++) {
        if (data[i] != 0) {
            allZeros = false;
            break;
        }
    }

    if (allZeros && data.size() > 1024) {
        if (debugOutput) {
            LOGW("[WARNING] Data appears to be all zeros - checking for compression/encryption");
        }


        if (data.size() >= 8) {

            if (memcmp(data.data(), "XISO", 4) == 0) {
                if (debugOutput) {
                    LOGI("[+] Detected XISO compression, attempting decompression");
                }
                data = decompressXISO(data);
            } else if (memcmp(data.data(), "XBC", 3) == 0) {
                if (debugOutput) {
                    LOGI("[+] Detected XBC compression, attempting decompression");
                }
                data = decompressXBC(data);
            } else if (memcmp(data.data(), "Xe", 2) == 0) {
                if (debugOutput) {
                    LOGI("[+] Detected Xbox encryption, attempting decryption");
                }
                data = decryptXboxFile(data);
            }
        }
    }


    if (data.size() >= 256 && memcmp(data.data() + 0x10, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
        if (debugOutput) {
            LOGD("[+] Found Xbox security sector, skipping first 256 bytes");
        }
        data.erase(data.begin(), data.begin() + 256);
    }


    if (data.size() >= 4 && memcmp(data.data(), "Xe", 2) == 0) {
        if (debugOutput) {
            LOGD("[+] Decrypting protected file");
        }
        data = decryptXboxFile(data);
    }


    if (data.size() >= 8) {
        if (debugOutput) {
            LOGI("[DEKOMPRESSION] readFileData: === AUTOMATISCHE DEKOMPRESSION AKTIVIERT ===");
        }


        if (memcmp(data.data(), "XISO", 4) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ XISO-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressXISO(data);
        }

        else if (memcmp(data.data(), "XBC", 3) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ XBC-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressXBC(data);
        }

        else if (memcmp(data.data(), "PK", 2) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ ZIP-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressZIP(data);
        }
        else if (memcmp(data.data(), "Rar!", 4) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ RAR-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressRAR(data);
        }
        else if (memcmp(data.data(), "7z", 2) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ 7Z-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompress7Z(data);
        }
        else if (memcmp(data.data(), "\x1f\x8b", 2) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ GZIP-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressGZIP(data);
        }

        if (debugOutput) {
            LOGI("[DEKOMPRESSION] readFileData: Daten nach automatischer Dekompression: %zu bytes", data.size());
        }
    }


    if (decompress && isXboxCompressed(data.data(), data.size())) {
        if (debugOutput) {
            LOGD("[+] Decompressing XBC file (legacy method)");
        }
        data = decompressXBC(data);
    }

    if (debugOutput) {
        LOGD("[DEBUG] Final data size: %zu bytes", data.size());
    }


    if (data.empty() && isoFile.is_open()) {
        isoFile.seekg(0, std::ios::end);
        uint64_t fileSize = isoFile.tellg();
        isoFile.seekg(0, std::ios::beg);

        LOGW("[XISO-XBE-BRUTE] Starte vollständigen XBE-Scan über %lu Bytes", (unsigned long)fileSize);

        const size_t scanBlock = 1024 * 1024; 
        std::vector<uint8_t> scanBuf(scanBlock);
        uint64_t totalScanned = 0;

        for (uint64_t offset = 0; offset < fileSize; offset += scanBlock) {
            isoFile.seekg(offset, std::ios::beg);
            size_t blockSize = std::min(scanBlock, (size_t)(fileSize - offset));
            isoFile.read(reinterpret_cast<char*>(scanBuf.data()), blockSize);
            size_t got = isoFile.gcount();
            if (got < 4) break;


            for (size_t i = 0; i <= got - 4; ++i) {
                uint32_t magic = (scanBuf[i]) | (scanBuf[i+1] << 8) | (scanBuf[i+2] << 16) | (scanBuf[i+3] << 24);
                if (magic == 0x48454258) { 
                    uint64_t xbeOffset = offset + i;
                    LOGW("[XISO-XBE-BRUTE] XBEH gefunden bei Offset %lu!", (unsigned long)xbeOffset);


                    char hexDump[3*32+1] = {0};
                    for (size_t j = 0; j < 32 && (i+j) < got; ++j) {
                        sprintf(hexDump + j*3, "%02X ", scanBuf[i+j]);
                    }
                    LOGW("[XISO-XBE-BRUTE] Nächste 32 Bytes: %s", hexDump);


                    isoFile.seekg(xbeOffset, std::ios::beg);
                    data.resize(size);
                    isoFile.read(reinterpret_cast<char*>(data.data()), size);
                    size_t got2 = isoFile.gcount();
                    if (got2 != size) data.resize(got2);

                    LOGW("[XISO-XBE-BRUTE] Erfolgreich %zu Bytes geladen", data.size());
                    return data;
                }
            }

            totalScanned += got;
            if (totalScanned % (10 * 1024 * 1024) == 0) { 
                LOGW("[XISO-XBE-BRUTE] Bereits %lu Bytes gescannt...", (unsigned long)totalScanned);
            }
        }

        LOGE("[XISO-XBE-BRUTE] Kein XBEH in der gesamten ISO gefunden! (%lu Bytes gescannt)", (unsigned long)totalScanned);
    }

    return data;
}


std::optional<XboxISOParser::XBEInfo> XboxISOParser::ultraBruteForceXBEScan() {
    if (!isoFile.is_open()) {
        return std::nullopt;
    }

    isoFile.seekg(0, std::ios::end);
    uint64_t fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    LOGW("[XISO-ULTRA] Starte ULTRA-BRUTE-FORCE über %lu Bytes", (unsigned long)fileSize);


    const size_t scanBlock = 64 * 1024; 
    std::vector<uint8_t> scanBuf(scanBlock);
    uint64_t totalScanned = 0;
    std::vector<XBEInfo> candidates;

    for (uint64_t offset = 0; offset < fileSize; offset += scanBlock) {
        isoFile.seekg(offset, std::ios::beg);
        size_t blockSize = std::min(scanBlock, (size_t)(fileSize - offset));
        isoFile.read(reinterpret_cast<char*>(scanBuf.data()), blockSize);
        size_t got = isoFile.gcount();
        if (got < 4) break;


        for (size_t i = 0; i <= got - 4; ++i) {
            uint32_t magic = (scanBuf[i]) | (scanBuf[i+1] << 8) | (scanBuf[i+2] << 16) | (scanBuf[i+3] << 24);
            if (magic == 0x48454258) { 
                uint64_t xbeOffset = offset + i;


                if (i + 0x38 <= got) { 
                    uint32_t entryPoint = (scanBuf[i+0x104]) | (scanBuf[i+0x105] << 8) | 
                                        (scanBuf[i+0x106] << 16) | (scanBuf[i+0x107] << 24);
                    uint32_t imageBase = (scanBuf[i+0x108]) | (scanBuf[i+0x109] << 8) | 
                                       (scanBuf[i+0x10A] << 16) | (scanBuf[i+0x10B] << 24);
                    uint32_t numSections = (scanBuf[i+0x114]) | (scanBuf[i+0x115] << 8) | 
                                         (scanBuf[i+0x116] << 16) | (scanBuf[i+0x117] << 24);


                    bool plausible = true;
                    if (entryPoint < 0x00010000 || entryPoint > 0x7FFFFFFF) plausible = false;
                    if (imageBase < 0x00010000 || imageBase > 0x7FFFFFFF) plausible = false;
                    if (numSections == 0 || numSections > 1000) plausible = false;

                    if (plausible) {
                        XBEInfo info;
                        info.name = "ultra_found_xbe";
                        info.absoluteOffset = (uint32_t)xbeOffset;
                        info.sector = (uint32_t)(xbeOffset / sectorSize);
                        info.size = 1024 * 1024; 
                        info.entryPoint = entryPoint;
                        info.imageBase = imageBase;
                        info.numSections = numSections;

                        candidates.push_back(info);

                        LOGW("[XISO-ULTRA] Kandidat gefunden bei Offset 0x%X: EP=0x%08X, Base=0x%08X, Sections=%u", 
                             info.absoluteOffset, info.entryPoint, info.imageBase, info.numSections);
                    }
                }
            }
        }

        totalScanned += got;
        if (totalScanned % (5 * 1024 * 1024) == 0) { 
            LOGW("[XISO-ULTRA] Bereits %lu Bytes gescannt...", (unsigned long)totalScanned);
        }
    }

    LOGW("[XISO-ULTRA] Scan abgeschlossen: %zu Kandidaten gefunden", candidates.size());


    if (!candidates.empty()) {

        std::sort(candidates.begin(), candidates.end(), [](const XBEInfo& a, const XBEInfo& b) {
            uint32_t aDiff = abs((int32_t)(a.entryPoint - a.imageBase));
            uint32_t bDiff = abs((int32_t)(b.entryPoint - b.imageBase));
            return aDiff < bDiff; 
        });

        LOGW("[XISO-ULTRA] Bester Kandidat gewählt: %s", candidates[0].name.c_str());
        return candidates[0];
    }

    LOGE("[XISO-ULTRA] Kein XBEH in der gesamten ISO gefunden! (%lu Bytes gescannt)", (unsigned long)totalScanned);
    return std::nullopt;
}

std::vector<uint8_t> XboxISOParser::decryptXboxFile(const std::vector<uint8_t>& data) const {

    if (data.size() < 8) return data;

    if (memcmp(data.data(), "Xe", 2) != 0) return data;

    uint32_t decryptedSize = readU32(data.data() + 4);
    std::vector<uint8_t> decrypted(decryptedSize);

    const uint8_t* src = data.data() + 8;
    uint8_t* dst = decrypted.data();
    size_t size = std::min<size_t>(data.size() - 8, decryptedSize);    
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i] ^ 0xA5; 
    }

    return decrypted;
}

std::vector<uint8_t> XboxISOParser::decompressXISO(const std::vector<uint8_t>& data) const {
    if (data.size() < 8) return data;

    if (memcmp(data.data(), "XISO", 4) != 0) return data;

    if (debugOutput) {
        LOGI("[XISO DECOMPRESS] Attempting XISO decompression of %zu bytes", data.size());
    }


    uint32_t uncompressedSize = readU32(data.data() + 4);

    if (debugOutput) {
        LOGI("[XISO DECOMPRESS] Uncompressed size: %u bytes", uncompressedSize);
    }

    if (uncompressedSize == 0 || uncompressedSize > 100 * 1024 * 1024) { 
        if (debugOutput) {
            LOGW("[XISO DECOMPRESS] Invalid uncompressed size: %u", uncompressedSize);
        }
        return data;
    }

    std::vector<uint8_t> decompressed(uncompressedSize);



    size_t copySize = std::min(data.size() - 8, static_cast<size_t>(uncompressedSize));
    memcpy(decompressed.data(), data.data() + 8, copySize);

    if (debugOutput) {
        LOGI("[XISO DECOMPRESS] Decompressed %zu bytes", copySize);
    }

    return decompressed;
}

std::vector<uint8_t> XboxISOParser::decompressXBC(const std::vector<uint8_t>& data) const {

    if (data.size() < 8) return data;


    if (memcmp(data.data(), "XBC", 3) != 0) return data;


    uint32_t decompressedSize = readU32(data.data() + 4);
    std::vector<uint8_t> decompressed(decompressedSize);


    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    if (inflateInit(&zs) != Z_OK) {
        return data;
    }

     zs.next_in = const_cast<uint8_t*>(data.data() + 8);
    zs.avail_in = static_cast<uInt>(data.size() - 8);
    zs.next_out = decompressed.data();
    zs.avail_out = decompressedSize;

    if (inflate(&zs, Z_FINISH) != Z_STREAM_END) {
        inflateEnd(&zs);
        return data;
    }

    inflateEnd(&zs);
    return decompressed;
}

bool XboxISOParser::isXboxCompressed(const uint8_t* data, size_t size) const {
    return size >= 4 && memcmp(data, "XBC", 3) == 0;
}

bool XboxISOParser::extractFile(const std::string& isoPath, const std::string& outputPath, 
                              std::string& errorMsg) {
    const XboxFileEntry* entry = findEntry(isoPath);
    if (!entry) {
        errorMsg = "File not found in ISO: " + isoPath;
        return false;
    }

    if (entry->isDirectory) {
        errorMsg = "Path is a directory: " + isoPath;
        return false;
    }

    std::vector<uint8_t> fileData = readFileData(entry->sector, entry->size, true);
    if (fileData.size() != entry->size) {
        errorMsg = "Failed to read complete file data (expected: " + 
                  std::to_string(entry->size) + ", got: " + 
                  std::to_string(fileData.size()) + ")";
        return false;
    }

    fs::path outPath(outputPath);
    if (!outPath.parent_path().empty()) {
        fs::create_directories(outPath.parent_path());
    }

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile) {
        errorMsg = "Failed to create output file: " + outputPath;
        return false;
    }

    outFile.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
    outFile.close();

    if (debugOutput) {
        LOGD("[+] Extracted file: %s -> %s (%zu bytes)", 
              isoPath.c_str(), outputPath.c_str(), fileData.size());
    }

    return true;
}

bool XboxISOParser::extractAllFiles(const std::string& outputDir, 
                                  std::function<void(const std::string&, bool)> progressCallback) {
    if (!isoFile.is_open() && iso_data.empty()) {
        return false;
    }

    std::queue<const XboxFileEntry*> directories;
    directories.push(&rootEntry);
    int fileCount = 0;
    int dirCount = 0;

    while (!directories.empty()) {
        const XboxFileEntry* currentDir = directories.front();
        directories.pop();

        std::string dirPath = outputDir + "/" + currentDir->fullPath;
        if (currentDir != &rootEntry) {
            fs::create_directories(dirPath);
            dirCount++;

            if (progressCallback) {
                progressCallback(currentDir->fullPath, true);
            }
        }

        for (const auto& entry : currentDir->children) {
            if (entry.isDirectory) {
                directories.push(&entry);
            } else {
                std::string filePath = outputDir + "/" + entry.fullPath;

                auto fileData = readFileData(entry.sector, entry.size, true);

                fs::path outPath(filePath);
                if (!outPath.parent_path().empty()) {
                    fs::create_directories(outPath.parent_path());
                }

                std::ofstream outFile(filePath, std::ios::binary);
                if (outFile) {
                    outFile.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
                    fileCount++;

                    if (progressCallback) {
                        progressCallback(entry.fullPath, false);
                    }
                }
            }
        }
    }

    if (debugOutput) {
        LOGD("[+] Extracted %d files and %d directories to %s", 
              fileCount, dirCount, outputDir.c_str());
    }

    return true;
}

std::vector<uint8_t> XboxISOParser::readFile(const std::string& path) {
    const XboxFileEntry* entry = findEntry(path);
    if (!entry || entry->isDirectory) {
        if (debugOutput) {
            LOGE("[ERROR] File not found or is a directory: %s", path.c_str());
        }
        return {};
    }
    return readFileData(entry->sector, entry->size, false);
}

std::optional<uint32_t> XboxISOParser::scanForXbeMagic(const std::string& isoPath) {
    std::ifstream iso(isoPath, std::ios::binary);
    if (!iso.is_open()) {
        LOGE("[XBE-DEBUG] scanForXbeMagic: Could not open ISO file: %s", isoPath.c_str());
        return std::nullopt;
    }
    const uint32_t magic = 0x48454258;
    std::vector<uint8_t> buffer(sectorSize);
    uint32_t sector = 0;
    while (iso.read(reinterpret_cast<char*>(buffer.data()), sectorSize)) {
        if (*(const uint32_t*)buffer.data() == magic) {
            LOGI("[XBE-DEBUG] XBE magic found at sector %u (offset 0x%X)", sector, sector * sectorSize);
            return sector;
        }
        sector++;
    }
    LOGW("[XBE-DEBUG] scanForXbeMagic: No XBE magic found in ISO");
    return std::nullopt;
}

std::optional<uint32_t> XboxISOParser::scanForXbeMagicAnywhere(const std::string& isoPath) {
    std::ifstream iso(isoPath, std::ios::binary);
    if (!iso.is_open()) {
        LOGE("[XBE-DEBUG] scanForXbeMagicAnywhere: Could not open ISO file: %s", isoPath.c_str());
        return std::nullopt;
    }
    const uint32_t magic = 0x48454258;
    std::vector<uint8_t> buffer(sectorSize);
    uint32_t sector = 0;
    while (iso.read(reinterpret_cast<char*>(buffer.data()), sectorSize)) {
        for (size_t offset = 0; offset + 4 <= sectorSize; offset += 4) {
            if (*(const uint32_t*)(buffer.data() + offset) == magic) {
                uint32_t absOffset = sector * sectorSize + offset;
                LOGI("[XBE-DEBUG] XBE magic found at sector %u, offset %zu (absolute 0x%X)", sector, offset, absOffset);

                return absOffset;
            }
        }
        sector++;
    }
    LOGW("[XBE-DEBUG] scanForXbeMagicAnywhere: No XBE magic found in ISO");
    return std::nullopt;
}

std::optional<uint32_t> XboxISOParser::scanForXbeMagicAnywhereTolerant(const std::string& isoPath) {
    std::ifstream iso(isoPath, std::ios::binary);
    if (!iso.is_open()) {
        LOGE("[XBE-DEBUG] scanForXbeMagicAnywhereTolerant: Could not open ISO file: %s", isoPath.c_str());
        return std::nullopt;
    }
    const uint32_t magic = 0x48454258;
    std::vector<uint32_t> offsets = {0, 0x10000, 0x8000, 0x20000}; 
    for (uint32_t baseOffset : offsets) {
        iso.clear();
        iso.seekg(0, std::ios::end);
        uint32_t fileSize = iso.tellg();
        if (baseOffset >= fileSize) continue;
        iso.seekg(baseOffset, std::ios::beg);
        uint32_t scanSize = static_cast<uint32_t>(static_cast<uint64_t>(fileSize) - baseOffset);
        std::vector<uint8_t> buffer(4096);
        uint32_t absOffset = baseOffset;
        while (scanSize > 0) {
            size_t toRead = std::min(scanSize, (uint32_t)buffer.size());
            iso.read(reinterpret_cast<char*>(buffer.data()), toRead);
            size_t bytesRead = iso.gcount();
            if (bytesRead == 0) break;
            for (size_t i = 0; i + 4 <= bytesRead; i += 4) {
                if (*(const uint32_t*)(buffer.data() + i) == magic) {
                    LOGI("[XBE-DEBUG] TOLERANT: XBE magic found at offset 0x%lX (base 0x%lX, rel 0x%zX)", (unsigned long)(absOffset + i), (unsigned long)baseOffset, i);

                    return absOffset + i;
                }
            }
            absOffset += bytesRead;
            scanSize -= bytesRead;
        }
    }
    LOGW("[XBE-DEBUG] scanForXbeMagicAnywhereTolerant: No XBE magic found in ISO (all offsets)");
    return std::nullopt;
}

std::optional<std::pair<uint32_t, uint32_t>> XboxISOParser::findDefaultXbeInRoot(const std::string& isoPath) {
    std::ifstream iso(isoPath, std::ios::binary);
    if (!iso.is_open()) {
        LOGE("[XBE-DEBUG] findDefaultXbeInRoot: Could not open ISO file: %s", isoPath.c_str());
        return std::nullopt;
    }
    constexpr uint32_t rootOffset = 0x10000;
    constexpr size_t dirTableSize = 4096; 
    iso.seekg(rootOffset, std::ios::beg);
    std::vector<uint8_t> buffer(dirTableSize);
    iso.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    size_t bytesRead = iso.gcount();
    if (bytesRead < 64) return std::nullopt;

    for (size_t i = 0; i + 64 <= bytesRead; i += 64) {
        std::string name(reinterpret_cast<const char*>(buffer.data() + i + 0x28), 42); 

        size_t nullPos = name.find('\0');
        if (nullPos != std::string::npos) name.resize(nullPos);

        std::string lowerName;
        for (char c : name) lowerName += std::tolower((unsigned char)c);
        if (lowerName == "default.xbe") {
            uint32_t sector = *(const uint32_t*)(buffer.data() + i + 0x14);
            uint32_t size = *(const uint32_t*)(buffer.data() + i + 0x18);
            LOGI("[XBE-DEBUG] XDVDFS: Found default.xbe at sector %u, size %u", sector, size);
            return std::make_pair(sector, size);
        }
    }
    LOGW("[XBE-DEBUG] XDVDFS: No default.xbe found in root directory");
    return std::nullopt;
}


void XboxISOParser::parseXdvdfsDirectoryTree(uint32_t sector, uint32_t offsetInSector, XboxFileEntry& parent, const std::string& parentPath) {

    std::vector<uint8_t> sectorData = readFileData(sector, sectorSize, false);
    if (sectorData.size() != sectorSize) return;
    uint32_t nodeOffset = offsetInSector;
    if (nodeOffset + 0x16 > sectorSize) return;

    uint32_t left = readU32(&sectorData[nodeOffset + 0x00]);
    if (left) parseXdvdfsDirectoryTree(sector, left, parent, parentPath);

    uint32_t right = readU32(&sectorData[nodeOffset + 0x04]);
    if (right) parseXdvdfsDirectoryTree(sector, right, parent, parentPath);

    uint32_t subdirSector = readU32(&sectorData[nodeOffset + 0x08]);
    uint32_t fileLength   = readU32(&sectorData[nodeOffset + 0x0C]);
    uint32_t fileSector   = readU32(&sectorData[nodeOffset + 0x10]);
    uint8_t  flags        = sectorData[nodeOffset + 0x14];
    uint8_t  nameLen      = sectorData[nodeOffset + 0x15];
    if (nameLen == 0 || nameLen > 42) return;

    std::u16string name16;
    for (int i = 0; i < nameLen / 2; ++i) {
        uint16_t c = sectorData[nodeOffset + 0x16 + i * 2] | (sectorData[nodeOffset + 0x16 + i * 2 + 1] << 8);
        if (c == 0) break;
        name16 += c;
    }
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::string name = convert.to_bytes(name16);
    std::string fullPath = parentPath.empty() ? name : parentPath + "/" + name;
    XboxFileEntry entry;
    entry.name = name;
    entry.fullPath = fullPath;
    entry.sector = fileSector;
    entry.size = fileLength;
    entry.isDirectory = (flags & 0x10) != 0;
    entry.isCompressed = (flags & 0x80) != 0;
    entry.isHidden = (flags & 0x02) != 0;

    if (entry.isDirectory && subdirSector) {
        parseXdvdfsDirectoryTree(subdirSector, 0, entry, fullPath);
    }
    parent.children.push_back(entry);
}

std::vector<uint8_t> XboxISOParser::readFileDataByOffset(uint64_t absOffset, uint32_t size) {
    std::vector<uint8_t> data;
    if (size == 0) return data;
    if (!isoFile.is_open()) {
        LOGE("[ERROR] ISO file is not open!");
        return data;
    }
    isoFile.clear();
    isoFile.seekg(0, std::ios::end);
    uint64_t fileSize = isoFile.tellg();
    if (absOffset >= fileSize) {
        LOGE("[ERROR] readFileDataByOffset: Offset 0x%lX außerhalb der Datei (Dateigröße: 0x%lX)", (unsigned long)absOffset, (unsigned long)fileSize);
        return data;
    }
    uint32_t maxRead = size;
    if (absOffset + size > fileSize) {
        maxRead = static_cast<uint32_t>(static_cast<uint64_t>(fileSize) - absOffset);
        LOGW("[WARNING] readFileDataByOffset: Requested size zu groß, lese nur %u Bytes", maxRead);
    }
    isoFile.seekg(absOffset, std::ios::beg);
    data.resize(maxRead);
    isoFile.read(reinterpret_cast<char*>(data.data()), maxRead);
    size_t got = isoFile.gcount();
    if (got != maxRead) {
        LOGW("[WARNING] readFileDataByOffset: Nur %zu von %u Bytes gelesen", got, maxRead);
        data.resize(got);
    }


    if (data.size() > 0) {
        bool allZeros = true;
        for (size_t i = 0; i < std::min(data.size(), static_cast<size_t>(1024)); i++) {
            if (data[i] != 0) {
                allZeros = false;
                break;
            }
        }

        if (allZeros && debugOutput) {
            LOGW("[WARNING] readFileDataByOffset: Data at offset 0x%lX is all zeros, trying to find correct offset", (unsigned long)absOffset);


            uint32_t correctOffset = 0;
            if (getGameDataOffset(correctOffset) && correctOffset != absOffset) {
                LOGI("[INFO] readFileDataByOffset: Found correct offset 0x%08X, retrying read", correctOffset);


                isoFile.seekg(correctOffset, std::ios::beg);
                data.resize(maxRead);
                isoFile.read(reinterpret_cast<char*>(data.data()), maxRead);
                got = isoFile.gcount();
                if (got != maxRead) {
                    LOGW("[WARNING] readFileDataByOffset: Retry read incomplete: %zu of %u bytes", got, maxRead);
                    data.resize(got);
                }
            } else {

                LOGI("[INFO] readFileDataByOffset: Trying common Xbox offsets...");
                std::vector<uint32_t> commonOffsets = {0x10000, 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1, 0x0};

                for (uint32_t testOffset : commonOffsets) {
                    if (testOffset >= fileSize) continue;

                    isoFile.seekg(testOffset, std::ios::beg);
                    if (!isoFile) continue;

                    std::vector<uint8_t> testBuffer(1024);
                    isoFile.read(reinterpret_cast<char*>(testBuffer.data()), 1024);
                    std::streamsize testBytesRead = isoFile.gcount();

                    if (testBytesRead == 0) continue;


                    bool hasValidData = false;
                    for (std::streamsize i = 0; i < testBytesRead; i++) {
                        if (testBuffer[i] != 0) {
                            hasValidData = true;
                            break;
                        }
                    }

                    if (hasValidData) {
                        LOGI("[INFO] readFileDataByOffset: Found non-zero data at offset 0x%08X, retrying read", testOffset);


                        isoFile.seekg(testOffset, std::ios::beg);
                        data.resize(maxRead);
                        isoFile.read(reinterpret_cast<char*>(data.data()), maxRead);
                        got = isoFile.gcount();
                        if (got != maxRead) {
                            LOGW("[WARNING] readFileDataByOffset: Retry read incomplete: %zu of %u bytes", got, maxRead);
                            data.resize(got);
                        }
                        break;
                    }
                }
            }
        }
    }


    if (data.size() >= 8) {
        if (debugOutput) {
            LOGI("[DEKOMPRESSION] readFileDataByOffset: === AUTOMATISCHE DEKOMPRESSION AKTIVIERT ===");
        }


        if (memcmp(data.data(), "XISO", 4) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ XISO-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressXISO(data);
        }

        else if (memcmp(data.data(), "XBC", 3) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ XBC-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressXBC(data);
        }

        else if (memcmp(data.data(), "PK", 2) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ ZIP-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressZIP(data);
        }
        else if (memcmp(data.data(), "Rar!", 4) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ RAR-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressRAR(data);
        }
        else if (memcmp(data.data(), "7z", 2) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ 7Z-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompress7Z(data);
        }
        else if (memcmp(data.data(), "\x1f\x8b", 2) == 0) {
            if (debugOutput) {
                LOGI("[DEKOMPRESSION] ✓ GZIP-Kompression erkannt - starte automatische Dekompression");
            }
            data = decompressGZIP(data);
        }

        if (debugOutput) {
            LOGI("[DEKOMPRESSION] readFileDataByOffset: Daten nach automatischer Dekompression: %zu bytes", data.size());
        }
    }

    return data;
}

bool XboxISOParser::loadXISOAsCompleteGame(std::string& gameName, uint32_t& gameSize, uint32_t& gameSector, std::string& errorMsg) {
    if (debugOutput) {
        LOGI("[XISO DIRECT] Loading XISO as complete game (no XBE search)");
    }

    if (!isoFile.is_open()) {
        errorMsg = "ISO file is not open";
        return false;
    }


    if (!checkXISOIntegrity(errorMsg)) {
        if (debugOutput) {
            LOGE("[XISO DIRECT] XISO integrity check failed: %s", errorMsg.c_str());
        }
        return false;
    }


    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    if (debugOutput) {
        LOGI("[XISO DIRECT] XISO file size: %lld bytes (%.2f GB)", 
               static_cast<long long>(fileSize), 
               static_cast<double>(fileSize) / (1024.0 * 1024.0 * 1024.0));
    }


    uint32_t gameDataOffset = 0;
    uint32_t maxSearchSize = std::min(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(100 * 1024 * 1024)); 

    if (debugOutput) {
        LOGI("[XISO DIRECT] Searching for game data in first %u bytes", maxSearchSize);
    }



    std::vector<uint32_t> possibleOffsets = {
        0x00000000,  
        0x00001000,  
        0x00002000,  
        0x00004000,  
        0x00008000,  
        0x00010000,  
        0x00020000,  
        0x00040000,  
        0x00080000,  
        0x00100000   
    };

    std::vector<uint8_t> searchBuffer(4096);
    for (uint32_t testOffset : possibleOffsets) {
        if (testOffset >= maxSearchSize) continue;

        isoFile.seekg(testOffset, std::ios::beg);
        if (!isoFile) continue;

        isoFile.read(reinterpret_cast<char*>(searchBuffer.data()), 4096);
        std::streamsize bytesRead = isoFile.gcount();

        if (bytesRead == 0) continue;



        bool hasValidData = false;
        uint32_t nonZeroCount = 0;

        for (std::streamsize i = 0; i < bytesRead; i++) {
            if (searchBuffer[i] != 0) {
                nonZeroCount++;
            }
        }


        if (nonZeroCount > (bytesRead / 10)) {
            hasValidData = true;
        }


        if (bytesRead >= 4) {
            uint32_t signature = (searchBuffer[0] << 0) | (searchBuffer[1] << 8) | 
                                (searchBuffer[2] << 16) | (searchBuffer[3] << 24);


            if (signature == 0x0000005A ||  
                signature == 0x0000004D ||  
                (signature & 0xFFFF) == 0x5A4D) {  
                hasValidData = true;
            }
        }

        if (hasValidData) {
            gameDataOffset = testOffset;
            if (debugOutput) {
                LOGI("[XISO DIRECT] Found valid game data at offset: 0x%08X (non-zero bytes: %u/%zu)", 
                     gameDataOffset, nonZeroCount, bytesRead);


                LOGI("[XISO DIRECT] === DEKOMPRESSION AKTIVIERT FÜR XISO-VOLLSPIEL ===");
                LOGI("[XISO DIRECT] Lade XISO-Vollspiel-Header mit automatischer Dekompression...");
            }


    std::vector<uint8_t> gameHeader = readFileDataByOffset(testOffset, std::min(1024u, static_cast<uint32_t>(static_cast<uint64_t>(fileSize) - testOffset)));

    if (gameHeader.size() >= 8) {
        if (debugOutput) {
            LOGI("[XISO DIRECT] XISO-Vollspiel-Header geladen: %zu bytes", gameHeader.size());


            if (memcmp(gameHeader.data(), "XISO", 4) == 0) {
                LOGI("[XISO DIRECT] ✓ XISO-Kompression im Vollspiel-Header erkannt!");
            } else if (memcmp(gameHeader.data(), "XBC", 3) == 0) {
                LOGI("[XISO DIRECT] ✓ XBC-Kompression im Vollspiel-Header erkannt!");
            } else if (memcmp(gameHeader.data(), "PK", 2) == 0) {
                LOGI("[XISO DIRECT] ✓ ZIP-Kompression im Vollspiel-Header erkannt!");
            } else if (memcmp(gameHeader.data(), "Rar!", 4) == 0) {
                LOGI("[XISO DIRECT] ✓ RAR-Kompression im Vollspiel-Header erkannt!");
            } else if (memcmp(gameHeader.data(), "7z", 2) == 0) {
                LOGI("[XISO DIRECT] ✓ 7Z-Kompression im Vollspiel-Header erkannt!");
            } else if (memcmp(gameHeader.data(), "\x1f\x8b", 2) == 0) {
                LOGI("[XISO DIRECT] ✓ GZIP-Kompression im Vollspiel-Header erkannt!");
            } else if (memcmp(gameHeader.data(), "MICROSOFT*XBOX*MEDIA", 8) == 0) {
                LOGI("[XISO DIRECT] ✓ Xbox ISO-Format im Vollspiel-Header erkannt!");
            }


            bool headerAllZeros = true;
            for (size_t i = 0; i < std::min(gameHeader.size(), static_cast<size_t>(1024)); i++) {
                if (gameHeader[i] != 0) {
                    headerAllZeros = false;
                    break;
                }
            }

            if (headerAllZeros) {
                LOGW("[XISO DIRECT] WARNING: Header is all zeros, searching for correct offset...");


                uint32_t correctOffset = 0;
                if (getGameDataOffset(correctOffset) && correctOffset != testOffset) {
                    LOGI("[XISO DIRECT] Found correct offset 0x%08X, updating game data offset", correctOffset);
                    gameDataOffset = correctOffset;
                    testOffset = correctOffset;


                    gameHeader = readFileDataByOffset(correctOffset, std::min(1024u, static_cast<uint32_t>(static_cast<uint64_t>(fileSize) - correctOffset)));
                    LOGI("[XISO DIRECT] Retried header read: %zu bytes", gameHeader.size());
                }
            }
        }
    }
            break;
        }
    }


    if (gameDataOffset == 0) {
        if (debugOutput) {
            LOGI("[XISO DIRECT] No non-zero data found, searching for Xbox headers...");
        }


        std::vector<uint32_t> standardOffsets = {0x10000, 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1, 0x0};

        for (uint32_t testOffset : standardOffsets) {
            if (testOffset >= fileSize) continue;

            isoFile.seekg(testOffset, std::ios::beg);
            if (!isoFile) continue;

            std::vector<uint8_t> headerBuffer(32);
            isoFile.read(reinterpret_cast<char*>(headerBuffer.data()), 32);
            std::streamsize bytesRead = isoFile.gcount();

            if (bytesRead < 32) continue;


            if (memcmp(headerBuffer.data(), "MICROSOFT*XBOX*MEDIA", 20) == 0) {
                gameDataOffset = testOffset;
                if (debugOutput) {
                    LOGI("[XISO DIRECT] Found Xbox ISO header at offset: 0x%08X", gameDataOffset);
                }
                break;
            }


            if (memcmp(headerBuffer.data(), "XISO", 4) == 0 || 
                memcmp(headerBuffer.data(), "XBC", 3) == 0 ||
                memcmp(headerBuffer.data(), "PK", 2) == 0 ||
                memcmp(headerBuffer.data(), "Rar!", 4) == 0 ||
                memcmp(headerBuffer.data(), "7z", 2) == 0 ||
                memcmp(headerBuffer.data(), "\x1f\x8b", 2) == 0) {
                gameDataOffset = testOffset;
                if (debugOutput) {
                    LOGI("[XISO DIRECT] Found compression header at offset: 0x%08X", gameDataOffset);
                }
                break;
            }
        }
    }


    this->gameDataOffset = gameDataOffset;


    if (gameDataOffset == 0) {
        if (debugOutput) {
            LOGW("[XISO DIRECT] No valid game data found, using file beginning");
        }
    }



    gameName = "xiso_complete_game";
    gameSize = static_cast<uint32_t>(fileSize) - gameDataOffset;




    gameSector = 0; 

    if (debugOutput) {
        LOGI("[XISO DIRECT] Loading complete XISO as game: %s (Size: %u bytes, Direct Offset: 0x%08X)", 
               gameName.c_str(), gameSize, gameDataOffset);
    }

    return true;
}

bool XboxISOParser::checkXISOIntegrity(std::string& errorMsg) {
    if (!isoFile.is_open()) {
        errorMsg = "ISO file is not open";
        return false;
    }

    if (debugOutput) {
        LOGI("[checkXISOIntegrity] Checking XISO file integrity...");
    }


    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    if (fileSize < 1024) {
        errorMsg = "XISO file is too small (< 1KB)";
        return false;
    }


    std::vector<uint32_t> testOffsets = {0x00000000, 0x00001000, 0x00002000, 0x00004000, 0x00008000, 0x00010000};
    bool foundValidData = false;
    uint32_t validDataOffset = 0;
    uint32_t totalValidBytes = 0;

    std::vector<uint8_t> testBuffer(4096);

    for (uint32_t offset : testOffsets) {
        if (offset >= fileSize) continue;

        isoFile.seekg(offset, std::ios::beg);
        if (!isoFile) continue;

        isoFile.read(reinterpret_cast<char*>(testBuffer.data()), 4096);
        std::streamsize bytesRead = isoFile.gcount();

        if (bytesRead == 0) continue;


        uint32_t nonZeroCount = 0;
        for (std::streamsize i = 0; i < bytesRead; i++) {
            if (testBuffer[i] != 0) {
                nonZeroCount++;
            }
        }

        float validPercentage = (nonZeroCount * 100.0f) / bytesRead;

        if (debugOutput) {
            LOGI("[checkXISOIntegrity] Offset 0x%08X: %u/%lld non-zero bytes (%.1f%%)",
                 offset, nonZeroCount, (long long)bytesRead, validPercentage);
        }

        if (validPercentage > 5.0f) {  
            foundValidData = true;
            validDataOffset = offset;
            totalValidBytes += nonZeroCount;


            if (bytesRead >= 20) {
                std::string header(reinterpret_cast<char*>(testBuffer.data()), 20);
                if (header.find("MICROSOFT*XBOX*MEDIA") != std::string::npos) {
                    if (debugOutput) {
                        LOGI("[checkXISOIntegrity] ✓ Found Xbox ISO signature at offset 0x%08X", offset);
                    }
                }
            }
        }
    }

    if (!foundValidData) {
        errorMsg = "No valid data found in XISO file - file appears to be corrupted or empty";
        return false;
    }

    if (debugOutput) {
        LOGI("[checkXISOIntegrity] ✓ XISO file integrity check passed - found %u bytes of valid data", totalValidBytes);
        LOGI("[checkXISOIntegrity] ✓ Valid data starts at offset 0x%08X", validDataOffset);
    }

    return true;
}

bool XboxISOParser::getGameDataOffset(uint32_t& offset) {
    if (!isoFile.is_open()) {
        return false;
    }


    isoFile.seekg(0, std::ios::end);
    std::streampos fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    if (debugOutput) {
        LOGI("[getGameDataOffset] File size: %lld bytes", static_cast<long long>(fileSize));
    }


    uint32_t maxSearchSize = std::min(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(100 * 1024 * 1024)); 

    if (debugOutput) {
        LOGI("[getGameDataOffset] Searching for game data in first %u bytes", maxSearchSize);
    }


    std::vector<uint32_t> possibleOffsets = {
        0x00000000,  
        0x00001000,  
        0x00002000,  
        0x00004000,  
        0x00008000,  
        0x00010000,  
        0x00020000,  
        0x00040000,  
        0x00080000,  
        0x00100000   
    };

    std::vector<uint8_t> searchBuffer(4096);
    for (uint32_t testOffset : possibleOffsets) {
        if (testOffset >= maxSearchSize) continue;

        isoFile.seekg(testOffset, std::ios::beg);
        if (!isoFile) continue;

        isoFile.read(reinterpret_cast<char*>(searchBuffer.data()), 4096);
        std::streamsize bytesRead = isoFile.gcount();

        if (bytesRead == 0) continue;


        uint32_t nonZeroCount = 0;
        for (std::streamsize i = 0; i < bytesRead; i++) {
            if (searchBuffer[i] != 0) {
                nonZeroCount++;
            }
        }


        if (nonZeroCount > (bytesRead / 10)) {
            offset = testOffset;
            if (debugOutput) {
                LOGI("[getGameDataOffset] Found game data at offset: 0x%08X (non-zero bytes: %u/%lld)", offset, nonZeroCount, (long long)bytesRead);
            }
            return true;
        }


        if (bytesRead >= 4) {
            uint32_t signature = (searchBuffer[0] << 0) | (searchBuffer[1] << 8) |
                                (searchBuffer[2] << 16) | (searchBuffer[3] << 24);


            if (signature == 0x0000005A ||  
                signature == 0x0000004D ||  
                (signature & 0xFFFF0000) == 0x5A4D0000 ||  
                signature == 0x00905A4D ||  
                memcmp(searchBuffer.data(), "XBEH", 4) == 0 ||  
                memcmp(searchBuffer.data(), "MICROSOFT*XBOX*MEDIA", 20) == 0) {  
                offset = testOffset;
                if (debugOutput) {
                    LOGI("[getGameDataOffset] Found executable signature at offset: 0x%08X", offset);
                }
                return true;
            }
        }
    }


    if (debugOutput) {
        LOGI("[getGameDataOffset] No non-zero data found, searching for Xbox headers...");
    }


    std::vector<uint32_t> standardOffsets = {0x10000, 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1, 0x0};

    for (uint32_t testOffset : standardOffsets) {
        if (testOffset >= fileSize) continue;

        isoFile.seekg(testOffset, std::ios::beg);
        if (!isoFile) continue;

        std::vector<uint8_t> headerBuffer(32);
        isoFile.read(reinterpret_cast<char*>(headerBuffer.data()), 32);
        std::streamsize bytesRead = isoFile.gcount();

        if (bytesRead < 32) continue;


        if (memcmp(headerBuffer.data(), "MICROSOFT*XBOX*MEDIA", 20) == 0) {
            offset = testOffset;
            if (debugOutput) {
                LOGI("[getGameDataOffset] Found Xbox ISO header at offset: 0x%08X", offset);
            }
            return true;
        }


        if (memcmp(headerBuffer.data(), "XISO", 4) == 0 || 
            memcmp(headerBuffer.data(), "XBC", 3) == 0 ||
            memcmp(headerBuffer.data(), "PK", 2) == 0 ||
            memcmp(headerBuffer.data(), "Rar!", 4) == 0 ||
            memcmp(headerBuffer.data(), "7z", 2) == 0 ||
            memcmp(headerBuffer.data(), "\x1f\x8b", 2) == 0) {
            offset = testOffset;
            if (debugOutput) {
                LOGI("[getGameDataOffset] Found compression header at offset: 0x%08X", offset);
            }
            return true;
        }
    }


    if (debugOutput) {
        LOGI("[getGameDataOffset] Still no valid data found, searching for any non-zero data...");
    }


    for (uint32_t searchOffset = 0; searchOffset < std::min(static_cast<uint32_t>(fileSize), static_cast<uint32_t>(10 * 1024 * 1024)); searchOffset += 1024) {
        isoFile.seekg(searchOffset, std::ios::beg);
        if (!isoFile) break;

        std::vector<uint8_t> smallBuffer(1024);
        isoFile.read(reinterpret_cast<char*>(smallBuffer.data()), 1024);
        std::streamsize bytesRead = isoFile.gcount();

        if (bytesRead == 0) break;


        bool hasValidData = false;
        for (std::streamsize i = 0; i < bytesRead; i++) {
            if (smallBuffer[i] != 0) {
                hasValidData = true;
                break;
            }
        }

        if (hasValidData) {
            offset = searchOffset;
            if (debugOutput) {
                LOGI("[getGameDataOffset] Found non-zero data at offset: 0x%08X", offset);
            }
            return true;
        }
    }


    if (debugOutput) {
        LOGI("[getGameDataOffset] Still no valid data found, searching for Xbox headers at larger offsets...");
    }


    std::vector<uint32_t> largeOffsets = {0x100000, 0x80000, 0x40000, 0x20000, 0x10000, 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1, 0x0};

    for (uint32_t testOffset : largeOffsets) {
        if (testOffset >= fileSize) continue;

        isoFile.seekg(testOffset, std::ios::beg);
        if (!isoFile) continue;

        std::vector<uint8_t> headerBuffer(64);
        isoFile.read(reinterpret_cast<char*>(headerBuffer.data()), 64);
        std::streamsize bytesRead = isoFile.gcount();

        if (bytesRead < 64) continue;


        if (memcmp(headerBuffer.data(), "MICROSOFT*XBOX*MEDIA", 20) == 0) {
            offset = testOffset;
            if (debugOutput) {
                LOGI("[getGameDataOffset] Found Xbox ISO header at large offset: 0x%08X", offset);
            }
            return true;
        }


        if (memcmp(headerBuffer.data(), "XISO", 4) == 0 || 
            memcmp(headerBuffer.data(), "XBC", 3) == 0 ||
            memcmp(headerBuffer.data(), "PK", 2) == 0 ||
            memcmp(headerBuffer.data(), "Rar!", 4) == 0 ||
            memcmp(headerBuffer.data(), "7z", 2) == 0 ||
            memcmp(headerBuffer.data(), "\x1f\x8b", 2) == 0) {
            offset = testOffset;
            if (debugOutput) {
                LOGI("[getGameDataOffset] Found compression header at large offset: 0x%08X", offset);
            }
            return true;
        }


        if (bytesRead >= 4) {
            uint32_t signature = (headerBuffer[0] << 0) | (headerBuffer[1] << 8) | 
                                (headerBuffer[2] << 16) | (headerBuffer[3] << 24);


            if (signature == 0x0000005A ||  
                signature == 0x0000004D ||  
                (signature & 0xFFFF) == 0x5A4D) {  
                offset = testOffset;
                if (debugOutput) {
                    LOGI("[getGameDataOffset] Found executable signature at large offset: 0x%08X", offset);
                }
                return true;
            }
        }
    }


    offset = 0;
    if (debugOutput) {
        LOGW("[getGameDataOffset] No valid game data found, using file beginning (offset: 0x%08X)", offset);
    }
    return true;
}

bool XboxISOParser::extractAllHiddenFiles(const std::string& isoPath, const std::string& extractDir, std::string& errorMsg) {
    (void)isoPath; 
    if (debugOutput) {
        LOGI("[XISO EXTRACT] Extracting all hidden files from XISO");
    }

    if (!isoFile.is_open()) {
        errorMsg = "ISO file is not open";
        return false;
    }


    try {
        if (!fs::exists(extractDir)) {
            fs::create_directories(extractDir);
        }
    } catch (const std::exception& e) {
        errorMsg = "Failed to create extraction directory: " + std::string(e.what());
        if (debugOutput) {
            LOGE("[XISO EXTRACT] %s", errorMsg.c_str());
        }
        return false;
    }

    if (debugOutput) {
        LOGI("[XISO EXTRACT] Extraction directory: %s", extractDir.c_str());
    }

    int extractedCount = 0;
    int hiddenCount = 0;
    for (const auto& pair : pathCache) {
        const XboxFileEntry* entry = pair.second;
        if (!entry->isDirectory) {
            std::string fileName = entry->name;
            std::string fullPath = entry->fullPath;
            bool isHidden = (fileName[0] == '.') || 
                           (fullPath.find("/.") != std::string::npos) ||
                           (fullPath.find("\\.") != std::string::npos);
            if (isHidden) {
                hiddenCount++;
                if (debugOutput) {
                    LOGI("[XISO EXTRACT] Found hidden file: %s", fullPath.c_str());
                }
            }
            std::string extractPath = extractDir + "/" + fullPath;
            try {
                std::string dirPath = fs::path(extractPath).parent_path().string();
                if (!fs::exists(dirPath)) {
                    fs::create_directories(dirPath);
                }
            } catch (const std::exception& e) {
                if (debugOutput) {
                    LOGW("[XISO EXTRACT] Warning: Could not create directory for %s: %s", 
                         fullPath.c_str(), e.what());
                }
                continue;
            }
            auto fileData = readFileData(entry->sector, entry->size);
            if (fileData.empty()) {
                if (debugOutput) {
                    LOGW("[XISO EXTRACT] Warning: Could not read file data for %s", fullPath.c_str());
                }
                continue;
            }
            FILE* outputFile = fopen(extractPath.c_str(), "wb");
            if (!outputFile) {
                if (debugOutput) {
                    LOGW("[XISO EXTRACT] Warning: Could not create file %s", extractPath.c_str());
                }
                continue;
            }
            size_t written = fwrite(fileData.data(), 1, fileData.size(), outputFile);
            fclose(outputFile);
            if (written == fileData.size()) {
                extractedCount++;
                if (debugOutput && isHidden) {
                    LOGI("[XISO EXTRACT] ✓ Extracted hidden file: %s (%zu bytes)", 
                         fullPath.c_str(), fileData.size());
                }
            } else {
                if (debugOutput) {
                    LOGW("[XISO EXTRACT] Warning: Incomplete write for %s (expected %zu, wrote %zu)", 
                         fullPath.c_str(), fileData.size(), written);
                }
            }
        }
    }
    if (debugOutput) {
        LOGI("[XISO EXTRACT] Extraction complete: %d files extracted (%d hidden files)", 
             extractedCount, hiddenCount);
    }
    return extractedCount > 0;
}



std::vector<uint8_t> XboxISOParser::decompressZIP(const std::vector<uint8_t>& data) const {
    if (debugOutput) {
        LOGI("[ZIP-DEKOMPRESSION] Versuche ZIP-Datei zu dekomprimieren: %zu bytes", data.size());
    }



    if (debugOutput) {
        LOGW("[ZIP-DEKOMPRESSION] ⚠ ZIP-Dekompression noch nicht implementiert");
    }


    return data;
}

std::vector<uint8_t> XboxISOParser::decompressRAR(const std::vector<uint8_t>& data) const {
    if (debugOutput) {
        LOGI("[RAR-DEKOMPRESSION] Versuche RAR-Datei zu dekomprimieren: %zu bytes", data.size());
    }



    if (debugOutput) {
        LOGW("[RAR-DEKOMPRESSION] ⚠ RAR-Dekompression noch nicht implementiert");
    }


    return data;
}

std::vector<uint8_t> XboxISOParser::decompress7Z(const std::vector<uint8_t>& data) const {
    if (debugOutput) {
        LOGI("[7Z-DEKOMPRESSION] Versuche 7Z-Datei zu dekomprimieren: %zu bytes", data.size());
    }



    if (debugOutput) {
        LOGW("[7Z-DEKOMPRESSION] ⚠ 7Z-Dekompression noch nicht implementiert");
    }


    return data;
}

std::vector<uint8_t> XboxISOParser::decompressGZIP(const std::vector<uint8_t>& data) const {
    if (debugOutput) {
        LOGI("[GZIP-DEKOMPRESSION] Versuche GZIP-Datei zu dekomprimieren: %zu bytes", data.size());
    }



    if (debugOutput) {
        LOGW("[GZIP-DEKOMPRESSION] ⚠ GZIP-Dekompression noch nicht implementiert");
    }


    return data;
}

bool XboxISOParser::isZIPCompressed(const uint8_t* data, size_t size) const {
    return size >= 4 && memcmp(data, "PK", 2) == 0;
}

bool XboxISOParser::isRARCompressed(const uint8_t* data, size_t size) const {
    return size >= 4 && memcmp(data, "Rar!", 4) == 0;
}

bool XboxISOParser::is7ZCompressed(const uint8_t* data, size_t size) const {
    return size >= 4 && memcmp(data, "7z", 2) == 0;
}

bool XboxISOParser::isGZIPCompressed(const uint8_t* data, size_t size) const {
    return size >= 4 && memcmp(data, "\x1f\x8b", 2) == 0;
}
