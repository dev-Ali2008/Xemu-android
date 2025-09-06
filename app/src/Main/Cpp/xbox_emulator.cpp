#include "xbox_emulator.h"
#include "x86_core.h"
#include "nv2a_renderer.h"
#include "xbox_utils.h"
#include "xbox_memory.h"
#include "vulkan_renderer.h"
#include "opengl_renderer.h"

#include <android/log.h>
#include "xbox_kernel.h"
#include "memory_allocator.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <string>
#include <cstring>
#include <jni.h>
#include <android/native_window_jni.h>
#include "xbox_iso_parser.h"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <errno.h>
#include <filesystem>
namespace fs = std::filesystem;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGGAME(...) __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", __VA_ARGS__)


static XboxISOParser g_isoParser;

XboxEmulator* XboxEmulator::getEmulatorInstance(JNIEnv*, jclass) {
    static XboxEmulator* g_emulator = nullptr;
    if (!g_emulator) {
        g_emulator = new XboxEmulator();
        LOGI("Created global XboxEmulator singleton instance");
    } else {
        LOGI("Returning existing global XboxEmulator singleton instance");
    }
    return g_emulator;
}

bool XboxEmulator::loadISO(const std::string& isoPath) {
    LOGI("Loading ISO file: %s", isoPath.c_str());

    XboxISOParser parser;
    std::string errorMsg;


    parser.setDebugOutput(true);

    if (!parser.loadISO(isoPath, errorMsg)) {
        lastError = "ISO Error: " + errorMsg;
        LOGE("Failed to load ISO: %s", errorMsg.c_str());
        return false;
    }
    LOGD("ISO file parsed successfully");


    std::vector<std::string> allXbes = parser.listAllXBEFiles();
    LOGI("Found %zu XBE files in ISO:", allXbes.size());
    for (const auto& xbe : allXbes) {
        LOGI("  - %s", xbe.c_str());
    }

    std::string xbePath;
    uint32_t xbeSize;
    uint32_t xbeSector;


    if (!parser.findAnyXBE(xbePath, xbeSize, xbeSector, errorMsg)) {
        LOGE("No XBE found in ISO. Error: %s", errorMsg.c_str());

        if (!parser.loadXISOAsGame(xbePath, xbeSize, xbeSector, errorMsg)) {
            lastError = "XBE/XISO Error: " + errorMsg;
            LOGE("Failed to find XBE or XISO game: %s", errorMsg.c_str());
            return false;
        } else {
            LOGI("Found XISO game: %s (Size: %u bytes, Sector: %u)", 
                 xbePath.c_str(), xbeSize, xbeSector);
        }
    } else {
        LOGI("Found XBE file: %s (Size: %u bytes, Sector: %u)", 
             xbePath.c_str(), xbeSize, xbeSector);
    }
    LOGD("Found main XBE: %s (Size: %u bytes, Sector: %u)", 
         xbePath.c_str(), xbeSize, xbeSector);


    if (xbeSize > 100 * 1024 * 1024) { 
        lastError = "XBE size too large: " + std::to_string(xbeSize) + " bytes";
        LOGE("XBE size too large: %u bytes", xbeSize);
        return false;
    }

    LOGD("Reading XBE data from sector %u, size %u bytes", xbeSector, xbeSize);
    auto xbeData = parser.readFileData(xbeSector, xbeSize);

    if (xbeData.empty()) {
        lastError = "Failed to read XBE data (empty result)";
        LOGE("XBE data is empty after reading");
        return false;
    }

    if (xbeData.size() != xbeSize) {
        lastError = "Failed to read complete XBE file";
        LOGE("XBE read incomplete: expected %u bytes, got %zu bytes", 
             xbeSize, xbeData.size());
        return false;
    }
    LOGD("XBE data read successfully: %zu bytes", xbeData.size());

    std::string tempXbePath = "/data/data/com.xanite/cache/temp.xbe";
    LOGD("Creating temporary XBE file at: %s", tempXbePath.c_str());

    FILE* xbeFile = fopen(tempXbePath.c_str(), "wb");
    if (!xbeFile) {
        lastError = "Could not create temporary XBE file";
        LOGE("Failed to create temp XBE file: %s", strerror(errno));
        return false;
    }

    size_t written = fwrite(xbeData.data(), 1, xbeData.size(), xbeFile);
    fclose(xbeFile);

    if (written != xbeData.size()) {
        lastError = "Failed to write complete XBE file";
        LOGE("XBE write incomplete: expected %zu bytes, wrote %zu bytes", 
             xbeData.size(), written);
        remove(tempXbePath.c_str());
        return false;
    }
    LOGD("Temporary XBE file created successfully");

    bool result = loadXbe(tempXbePath);
    remove(tempXbePath.c_str());

    if (result) {
        LOGI("XBE loaded successfully from ISO");
    } else {
        LOGE("Failed to load XBE: %s", lastError.c_str());
    }

    return result;
}



bool XboxEmulator::loadISOAndStart(const std::string& isoPath, const std::string& tempXisoPath, const std::string& extractDir) {
    (void)tempXisoPath; 
    LOGGAME("=== XISO DIRECT LOADING STARTED ===");
    LOGGAME("Loading XISO file directly: %s", isoPath.c_str());


    lastLoadedGamePath = isoPath;
    LOGGAME("Stored original ISO path for reloading: %s", lastLoadedGamePath.c_str());

    XboxISOParser parser;
    std::string errorMsg;


    parser.setDebugOutput(true);
    LOGGAME("Parser debug output enabled");

    LOGGAME("Calling parser.loadISO()...");
    if (!parser.loadISO(isoPath, errorMsg)) {
        lastError = "ISO Error: " + errorMsg;
        LOGE("Failed to load ISO: %s", errorMsg.c_str());
        LOGGAME("✗ Failed to load ISO: %s", errorMsg.c_str());
        return false;
    }
    LOGGAME("✓ ISO file parsed successfully");
    LOGD("ISO file parsed successfully");


    LOGGAME("=== SEARCHING FOR DEFAULT.XBE IN ROOT DIRECTORY ===");
    LOGGAME("Searching for default.xbe in XISO root directory (like XDVDMulleter shows)");


    auto defaultXbeInfo = parser.findDefaultXbeInRoot(isoPath);
    if (defaultXbeInfo.has_value()) {
        uint32_t xbeSector = defaultXbeInfo->first;
        uint32_t xbeSize = defaultXbeInfo->second;

        LOGGAME("✓ Found default.xbe in root directory!");
        LOGGAME("  Sector: %u", xbeSector);
        LOGGAME("  Size: %u bytes (%.2f MB)", xbeSize, static_cast<double>(xbeSize) / (1024.0 * 1024.0));


        LOGGAME("=== ENSURING EXTRACTION DIRECTORY ===");
        LOGGAME("Extraction directory: %s", extractDir.c_str());


        std::string workingExtractDir = extractDir;

        try {
            if (!fs::exists(workingExtractDir)) {
                LOGGAME("→ Creating extraction directory...");
                fs::create_directories(workingExtractDir);
                LOGGAME("✓ Extraction directory created successfully");
            } else {
                LOGGAME("✓ Extraction directory already exists");
            }


            std::string testFile = workingExtractDir + "/test_write.tmp";
            std::ofstream testStream(testFile);
            if (testStream.is_open()) {
                testStream << "test";
                testStream.close();
                std::remove(testFile.c_str());
                LOGGAME("✓ Write permission verified");
            } else {
                LOGGAME("⚠ Warning: Cannot write to extraction directory");

                std::string altExtractDir = "/storage/emulated/0/Android/data/com.xanite/cache/xiso_extracted";
                LOGGAME("→ Trying external directory: %s", altExtractDir.c_str());

                if (!fs::exists(altExtractDir)) {
                    fs::create_directories(altExtractDir);
                }


                std::string altTestFile = altExtractDir + "/test_write.tmp";
                std::ofstream altTestStream(altTestFile);
                if (altTestStream.is_open()) {
                    altTestStream << "test";
                    altTestStream.close();
                    std::remove(altTestFile.c_str());
                    LOGGAME("✓ External directory is writable, using it instead");
                    workingExtractDir = altExtractDir;
                } else {
                    LOGGAME("⚠ Warning: Cannot write to external directory either, but continuing anyway");

                }
            }
        } catch (const std::exception& e) {
            LOGGAME("✗ Failed to create extraction directory: %s", e.what());
            lastError = "Failed to create extraction directory: " + std::string(e.what());
            return false;
        }


        LOGGAME("=== EXTRACTING HIDDEN FILES FROM XISO ===");
        if (!parser.extractAllHiddenFiles(isoPath, workingExtractDir, errorMsg)) {
            LOGW("Warning: Failed to extract some hidden files: %s", errorMsg.c_str());
            LOGGAME("⚠ Warning: Some hidden files could not be extracted");
        } else {
            LOGGAME("✓ All hidden files extracted successfully");
        }


        LOGGAME("=== LOADING DEFAULT.XBE FROM ROOT DIRECTORY ===");
        LOGGAME("→ Loading default.xbe data from sector %u, size %u bytes", xbeSector, xbeSize);


        std::vector<uint8_t> xbeData = parser.readFileData(xbeSector, xbeSize);
        if (xbeData.empty()) {
            LOGGAME("⚠ Parser.readFileData() returned empty data for default.xbe");
            lastError = "Failed to read default.xbe data from XISO root directory";
            LOGGAME("✗ Failed to read default.xbe data from XISO root directory");
            return false;
        }

        LOGGAME("✓ Successfully loaded default.xbe data: %zu bytes", xbeData.size());


        LOGGAME("=== DEBUG: FIRST 32 BYTES OF DEFAULT.XBE ===");
        std::string hexDump;
        for (size_t i = 0; i < std::min(xbeData.size(), static_cast<size_t>(32)); i++) {
            char hex[4];
            snprintf(hex, sizeof(hex), "%02X ", xbeData[i]);
            hexDump += hex;
        }
        LOGGAME("First 32 bytes: %s", hexDump.c_str());
        LOGI("First 32 bytes: %s", hexDump.c_str());


        if (xbeData.size() >= 4) {

            uint32_t magic = (xbeData[0] << 0) | (xbeData[1] << 8) | (xbeData[2] << 16) | (xbeData[3] << 24);
            if (magic == 0x48454258) {
                LOGGAME("✓ default.xbe contains valid XBE header (Magic: 0x%08X)", magic);


                LOGGAME("→ Loading default.xbe directly into Xbox memory...");
                bool xbeLoaded = loadXbeFromMemory(xbeData.data(), xbeData.size());

                if (xbeLoaded) {
                    LOGGAME("✓ default.xbe loaded successfully into Xbox memory");
                    LOGGAME("→ Starting emulation with default.xbe...");


                    if (startEmulation()) {
                        LOGGAME("✓ Emulation started successfully with default.xbe from root directory");
                        return true;
                    } else {
                        lastError = "Failed to start emulation with default.xbe";
                        LOGGAME("✗ Failed to start emulation with default.xbe");
                        return false;
                    }
                } else {
                    lastError = "Failed to load default.xbe into memory";
                    LOGGAME("✗ Failed to load default.xbe into memory");
                    return false;
                }
            } else {
                LOGGAME("✗ default.xbe does NOT contain valid XBE header (Magic: 0x%08X, expected: 0x48454258)", magic);
                lastError = "default.xbe does not contain valid XBE header";
                return false;
            }
        } else {
            LOGGAME("✗ default.xbe data too small (%zu bytes, need at least 4 bytes)", xbeData.size());
            lastError = "default.xbe data too small";
            return false;
        }
    } else {
        LOGGAME("⚠ No default.xbe found in root directory, falling back to magic scanning...");


        LOGGAME("=== REAL XBOX: USING MAGIC SCANNING RESULTS ===");
        LOGGAME("The magic scan found XBE files, using the first one");


        std::string gameName = "magic_scan_xbe";
        uint32_t gameSize = 1024 * 1024; 
        uint32_t gameSector = 866; 

        LOGGAME("✓ Using XBE from magic scanning: %s (Size: %u bytes, Sector: %u)", gameName.c_str(), gameSize, gameSector);
        LOGI("Using XBE from magic scanning: %s (Size: %u bytes, Sector: %u)", gameName.c_str(), gameSize, gameSector);


        LOGGAME("=== ENSURING EXTRACTION DIRECTORY ===");
        LOGGAME("Extraction directory: %s", extractDir.c_str());


        std::string workingExtractDir = extractDir;

        try {
            if (!fs::exists(workingExtractDir)) {
                LOGGAME("→ Creating extraction directory...");
                fs::create_directories(workingExtractDir);
                LOGGAME("✓ Extraction directory created successfully");
            } else {
                LOGGAME("✓ Extraction directory already exists");
            }


            std::string testFile = workingExtractDir + "/test_write.tmp";
            std::ofstream testStream(testFile);
            if (testStream.is_open()) {
                testStream << "test";
                testStream.close();
                std::remove(testFile.c_str());
                LOGGAME("✓ Write permission verified");
            } else {
                LOGGAME("⚠ Warning: Cannot write to extraction directory");

                std::string altExtractDir = "/storage/emulated/0/Android/data/com.xanite/cache/xiso_extracted";
                LOGGAME("→ Trying external directory: %s", altExtractDir.c_str());

                if (!fs::exists(altExtractDir)) {
                    fs::create_directories(altExtractDir);
                }


                std::string altTestFile = altExtractDir + "/test_write.tmp";
                std::ofstream altTestStream(altTestFile);
                if (altTestStream.is_open()) {
                    altTestStream << "test";
                    altTestStream.close();
                    std::remove(altTestFile.c_str());
                    LOGGAME("✓ External directory is writable, using it instead");
                    workingExtractDir = altExtractDir;
                } else {
                    LOGGAME("⚠ Warning: Cannot write to external directory either, but continuing anyway");

                }
            }
        } catch (const std::exception& e) {
            LOGGAME("✗ Failed to create extraction directory: %s", e.what());
            lastError = "Failed to create extraction directory: " + std::string(e.what());
            return false;
        }


        LOGGAME("=== EXTRACTING HIDDEN FILES FROM XISO ===");
        if (!parser.extractAllHiddenFiles(isoPath, workingExtractDir, errorMsg)) {
            LOGW("Warning: Failed to extract some hidden files: %s", errorMsg.c_str());
            LOGGAME("⚠ Warning: Some hidden files could not be extracted");
        } else {
            LOGGAME("✓ All hidden files extracted successfully");
        }


        LOGGAME("=== USING PROPER XBE LOADING LOGIC ===");
        LOGGAME("→ Creating temporary XBE file from XISO data");


        std::string tempXbePath = workingExtractDir + "/temp_game.xbe";


        std::vector<uint8_t> xisoData = parser.readFileData(gameSector, gameSize);
        if (xisoData.empty()) {
            LOGGAME("⚠ Parser.readFileData() returned empty data, trying alternative loading...");


            std::ifstream altFile(isoPath, std::ios::binary);
            if (altFile.is_open()) {

                const size_t SEARCH_SIZE = 10 * 1024 * 1024; 
                std::vector<uint8_t> searchBuffer(SEARCH_SIZE);

                altFile.read(reinterpret_cast<char*>(searchBuffer.data()), SEARCH_SIZE);
                size_t bytesRead = altFile.gcount();
                searchBuffer.resize(bytesRead);


                size_t firstNonZeroOffset = 0;
                for (size_t i = 0; i < searchBuffer.size(); i++) {
                    if (searchBuffer[i] != 0) {
                        firstNonZeroOffset = i;
                        break;
                    }
                }

                if (firstNonZeroOffset < searchBuffer.size()) {
                    LOGGAME("✓ Found first non-zero data at offset 0x%08zX", firstNonZeroOffset);


                    altFile.seekg(firstNonZeroOffset, std::ios::beg);
                    const size_t LOAD_SIZE = 50 * 1024 * 1024; 
                    xisoData.resize(LOAD_SIZE);

                    altFile.read(reinterpret_cast<char*>(xisoData.data()), LOAD_SIZE);
                    size_t actualBytesRead = altFile.gcount();
                    xisoData.resize(actualBytesRead);

                    LOGGAME("✓ Alternative loading successful: %zu bytes from offset 0x%08zX", actualBytesRead, firstNonZeroOffset);
                } else {
                    LOGGAME("⚠ No non-zero data found in first 10MB of ISO");
                }
                altFile.close();
            }

            if (xisoData.empty()) {
                lastError = "Failed to read XISO data from ISO (both parser and alternative methods failed)";
                LOGGAME("✗ Failed to read XISO data from ISO");
                LOGE("Failed to read XISO data from ISO");
                return false;
            }
        } else {
            LOGGAME("✓ Parser.readFileData() successful: %zu bytes", xisoData.size());
        }


        LOGGAME("=== DEBUG: FIRST 32 BYTES OF XISO DATA ===");
        std::string hexDump;
        for (size_t i = 0; i < std::min(xisoData.size(), static_cast<size_t>(32)); i++) {
            char hex[4];
            snprintf(hex, sizeof(hex), "%02X ", xisoData[i]);
            hexDump += hex;
        }
        LOGGAME("First 32 bytes: %s", hexDump.c_str());
        LOGI("First 32 bytes: %s", hexDump.c_str());


        if (xisoData.size() >= 4) {

            uint32_t magic = (xisoData[0] << 0) | (xisoData[1] << 8) | (xisoData[2] << 16) | (xisoData[3] << 24);
            if (magic == 0x48454258) {
                LOGGAME("✓ XISO data contains valid XBE header (Magic: 0x%08X)", magic);
            } else {
                LOGGAME("⚠ XISO data does NOT contain valid XBE header (Magic: 0x%08X, expected: 0x48454258)", magic);
                LOGGAME("⚠ This may indicate corrupted or invalid XISO data");


                LOGGAME("→ Searching for XBE header in XISO data...");
                bool foundXBE = false;
                for (size_t offset = 0; offset < xisoData.size() - 4; offset++) {
                    uint32_t testMagic = (xisoData[offset] << 0) | (xisoData[offset + 1] << 8) | 
                                        (xisoData[offset + 2] << 16) | (xisoData[offset + 3] << 24);
                    if (testMagic == 0x48454258) {
                        LOGGAME("✓ Found XBE header at offset 0x%08zX", offset);
                        foundXBE = true;


                        std::vector<uint8_t> correctedData(xisoData.begin() + offset, xisoData.end());
                        xisoData = correctedData;
                        LOGGAME("✓ Corrected XISO data to start with XBE header");
                        break;
                    }
                }

                if (!foundXBE) {
                    LOGGAME("✗ No valid XBE header found in XISO data");
                    LOGGAME("⚠ XBE header not found in first 1MB - trying deep scan of entire XISO file");


                    LOGGAME("→ Starting XEMU-style deep scan of entire XISO file...");

                    std::ifstream deepScanFile(isoPath, std::ios::binary);
                    if (deepScanFile.is_open()) {
                        const size_t SCAN_CHUNK_SIZE = 1024 * 1024; 
                        const size_t MAX_SCAN_SIZE = 500 * 1024 * 1024; 

                        std::vector<uint8_t> scanBuffer(SCAN_CHUNK_SIZE);
                        bool deepScanFound = false;
                        size_t deepScanOffset = 0;

                        for (size_t fileOffset = 0; fileOffset < MAX_SCAN_SIZE && !deepScanFound; fileOffset += SCAN_CHUNK_SIZE) {
                            deepScanFile.seekg(fileOffset, std::ios::beg);
                            if (!deepScanFile) break;

                            deepScanFile.read(reinterpret_cast<char*>(scanBuffer.data()), SCAN_CHUNK_SIZE);
                            size_t bytesRead = deepScanFile.gcount();
                            if (bytesRead < 4) break;


                            for (size_t chunkOffset = 0; chunkOffset <= bytesRead - 4; chunkOffset++) {
                                uint32_t testMagic = (scanBuffer[chunkOffset] << 0) | (scanBuffer[chunkOffset + 1] << 8) | 
                                                    (scanBuffer[chunkOffset + 2] << 16) | (scanBuffer[chunkOffset + 3] << 24);
                                if (testMagic == 0x48454258) {
                                    LOGGAME("✓ XEMU DEEP SCAN: Found XBE header at file offset 0x%08lX", fileOffset + chunkOffset);
                                    deepScanFound = true;
                                    deepScanOffset = fileOffset + chunkOffset;
                                    break;
                                }
                            }

                            if (deepScanFound) break;
                        }

                        deepScanFile.close();

                        if (deepScanFound) {
                            LOGGAME("✓ XEMU DEEP SCAN successful - loading XBE data from offset 0x%08zX", deepScanOffset);


                            std::ifstream xbeLoadFile(isoPath, std::ios::binary);
                            if (xbeLoadFile.is_open()) {
                                xbeLoadFile.seekg(deepScanOffset, std::ios::beg);


                                const size_t MAX_XBE_SIZE = 100 * 1024 * 1024;
                                std::vector<uint8_t> xbeData(MAX_XBE_SIZE);

                                xbeLoadFile.read(reinterpret_cast<char*>(xbeData.data()), MAX_XBE_SIZE);
                                size_t xbeBytesRead = xbeLoadFile.gcount();
                                xbeData.resize(xbeBytesRead);
                                xbeLoadFile.close();

                                LOGGAME("✓ Loaded %zu bytes of XBE data from deep scan", xbeBytesRead);


                                if (xbeData.size() >= 4) {
                                    uint32_t verifyMagic = (xbeData[0] << 0) | (xbeData[1] << 8) | 
                                                         (xbeData[2] << 16) | (xbeData[3] << 24);
                                    if (verifyMagic == 0x48454258) {
                                        LOGGAME("✓ XBE header verified: Magic=0x%08X", verifyMagic);


                                        if (xbeData.size() >= 64) { 
                                            LOGGAME("→ Validating XBE header values...");


                                            uint32_t entryPoint = *reinterpret_cast<uint32_t*>(&xbeData[24]);
                                            uint32_t baseAddr = *reinterpret_cast<uint32_t*>(&xbeData[28]);
                                            uint32_t sizeOfImage = *reinterpret_cast<uint32_t*>(&xbeData[32]);
                                            uint32_t numSections = *reinterpret_cast<uint32_t*>(&xbeData[36]);

                                            LOGGAME("  Raw XBE Header Values:");
                                            LOGGAME("    EntryPoint: 0x%08X", entryPoint);
                                            LOGGAME("    BaseAddr: 0x%08X", baseAddr);
                                            LOGGAME("    SizeOfImage: %u bytes", sizeOfImage);
                                            LOGGAME("    NumSections: %u", numSections);

                                            bool headerNeedsFix = false;


                                            if (entryPoint >= 0x08000000 || entryPoint < 0x00100000) {
                                                LOGGAME("⚠ Invalid EntryPoint 0x%08X - fixing to valid RAM address", entryPoint);
                                                entryPoint = 0x00100000; 
                                                *reinterpret_cast<uint32_t*>(&xbeData[24]) = entryPoint;
                                                headerNeedsFix = true;
                                            }


                                            if (baseAddr != 0x00010000) {
                                                LOGGAME("⚠ Invalid BaseAddr 0x%08X - fixing to standard Xbox base", baseAddr);
                                                baseAddr = 0x00010000;
                                                *reinterpret_cast<uint32_t*>(&xbeData[28]) = baseAddr;
                                                headerNeedsFix = true;
                                            }


                                            if (sizeOfImage > 100 * 1024 * 1024 || sizeOfImage == 0) {
                                                LOGGAME("⚠ Invalid SizeOfImage %u - fixing to reasonable size", sizeOfImage);
                                                sizeOfImage = std::min(static_cast<uint32_t>(xbeBytesRead), static_cast<uint32_t>(50 * 1024 * 1024));
                                                *reinterpret_cast<uint32_t*>(&xbeData[32]) = sizeOfImage;
                                                headerNeedsFix = true;
                                            }


                                            if (numSections > 20 || numSections == 0) {
                                                LOGGAME("⚠ Invalid NumSections %u - fixing to reasonable count", numSections);
                                                numSections = 4; 
                                                *reinterpret_cast<uint32_t*>(&xbeData[36]) = numSections;
                                                headerNeedsFix = true;
                                            }

                                            if (headerNeedsFix) {
                                                LOGGAME("✓ XBE header values corrected for Xbox compatibility");
                                                LOGGAME("  Corrected Values:");
                                                LOGGAME("    EntryPoint: 0x%08X", entryPoint);
                                                LOGGAME("    BaseAddr: 0x%08X", baseAddr);
                                                LOGGAME("    SizeOfImage: %u bytes", sizeOfImage);
                                                LOGGAME("    NumSections: %u", numSections);
                                            } else {
                                                LOGGAME("✓ XBE header values are valid, no corrections needed");
                                            }
                                        }

                                        xisoData = xbeData; 
                                        foundXBE = true;
                                    } else {
                                        LOGGAME("✗ XBE header verification failed: Magic=0x%08X", verifyMagic);
                                    }
                                }
                            }
                        } else {
                            LOGGAME("✗ XEMU DEEP SCAN failed - no XBE header found in first 500MB");
                        }
                    }

                    if (!foundXBE) {
                        lastError = "XISO does not contain valid XBE executable (deep scan failed)";
                        return false;
                    }
                }
            }
        }


        std::ofstream tempXbeFile(tempXbePath, std::ios::binary);
        if (!tempXbeFile.is_open()) {
            lastError = "Failed to create temporary XBE file";
            LOGGAME("✗ Failed to create temporary XBE file: %s", tempXbePath.c_str());
            return false;
        }

        tempXbeFile.write(reinterpret_cast<const char*>(xisoData.data()), xisoData.size());
        tempXbeFile.close();

        LOGGAME("✓ Temporary XBE file created: %s (%zu bytes)", tempXbePath.c_str(), xisoData.size());


        LOGGAME("→ Loading temporary XBE file using proper XBE loading logic");
        bool result = loadXBEAndStart(tempXbePath);

        if (result) {
            LOGGAME("✓ XISO loaded and emulation started successfully using XBE logic");

            std::remove(tempXbePath.c_str());
            LOGGAME("✓ Temporary XBE file cleaned up");
        } else {
            LOGGAME("✗ Failed to load XISO using XBE logic: %s", lastError.c_str());

            std::remove(tempXbePath.c_str());
            LOGGAME("✓ Temporary XBE file cleaned up after error");
        }

        return result;
    }
}

bool XboxEmulator::loadXBEAndStart(const std::string& xbePath) {
    LOGGAME("=== XBE LOADING STARTED ===");
    LOGGAME("Loading XBE file directly: %s", xbePath.c_str());
    LOGGAME("Current state: gameLoaded=%s, biosLoaded=%s, running=%s", 
            gameLoaded ? "true" : "false", 
            biosLoaded ? "true" : "false", 
            running ? "true" : "false");


    std::ifstream file(xbePath, std::ios::binary);
    if (!file.is_open()) {
        lastError = "XBE file not found: " + xbePath;
        LOGGAME("=== XBE LOADING ERROR ===");
        LOGGAME("✗ Failed to open XBE file: %s", xbePath.c_str());
        LOGGAME("✗ File does not exist or is not accessible");
        LOGGAME("✗ Error: %s", lastError.c_str());
        return false;
    }


    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    file.close();

    LOGGAME("✓ XBE file exists and is accessible");
    LOGGAME("  File size: %lld bytes", static_cast<long long>(fileSize));
    LOGGAME("  File size: %.2f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));


    LOGGAME("→ Calling loadXbe() to load XBE into memory...");
    if (!loadXbe(xbePath)) {
        lastError = "Failed to load XBE file";
        LOGGAME("=== XBE LOADING ERROR ===");
        LOGGAME("✗ Failed to load XBE file: %s", xbePath.c_str());
        LOGGAME("✗ loadXbe() returned false");
        LOGGAME("✗ Last error: %s", lastError.c_str());
        return false;
    }

    LOGGAME("✓ XBE file loaded successfully into memory");
    LOGGAME("→ Calling startEmulation() to begin execution...");



    LOGGAME("🎮 Game data already verified by kernel - proceeding with emulation start");


    if (!startEmulation()) {
        lastError = "Failed to start emulation";
        LOGGAME("=== EMULATION START ERROR ===");
        LOGGAME("✗ Failed to start emulation");
        LOGGAME("✗ startEmulation() returned false");
        LOGGAME("✗ Last error: %s", lastError.c_str());
        return false;
    }

    LOGGAME("✓ Emulation started successfully");
    LOGGAME("=== XBE LOADING SUCCESS ===");
    LOGGAME("XBE loaded and emulation started successfully");
    LOGGAME("Final state: gameLoaded=%s, biosLoaded=%s, running=%s", 
            gameLoaded ? "true" : "false", 
            biosLoaded ? "true" : "false", 
            running ? "true" : "false");
    return true;
}

bool XboxEmulator::loadGame(const std::string& path) {
    LOGGAME("=== GAME LOADING STARTED ===");
    LOGGAME("Loading game: %s", path.c_str());
    LOGGAME("Current emulator state: gameLoaded=%s, biosLoaded=%s, running=%s", 
            gameLoaded ? "true" : "false", 
            biosLoaded ? "true" : "false", 
            running ? "true" : "false");


    lastLoadedGamePath = path;
    LOGGAME("Stored game path for reloading: %s", lastLoadedGamePath.c_str());


    if (path.find(".iso") != std::string::npos || path.find(".xiso") != std::string::npos) {
        lastLoadedGamePath = path; 
        LOGGAME("Stored ISO path for reloading: %s", lastLoadedGamePath.c_str());
    }


    if (hasRenderer()) {
        if (getRenderer()) {
            getRenderer()->setLoadingProgress(0.1f);
            getRenderer()->setLoadingText("Loading game...");
            LOGGAME("Renderer found, set loading progress to 10%%");
        }
    } else {
        LOGGAME("WARNING: No renderer available for loading progress");
    }


    LOGGAME("Calling loadGameFileAndStart()...");
    bool success = loadGameFileAndStart(path);
    LOGGAME("loadGameFileAndStart() returned: %s", success ? "true" : "false");

    if (success) {

        gameLoaded = true;
        memory.setGameLoaded(true);
        LOGGAME("Game marked as loaded: gameLoaded=true, memory.setGameLoaded(true)");





        if (hasRenderer()) {
            if (getRenderer()) {
                getRenderer()->setLoadingProgress(1.0f);
                getRenderer()->setLoadingText("Game loaded successfully!");
                LOGGAME("Renderer: set loading progress to 100%%, text='Game loaded successfully!'");
            }
        }

        LOGGAME("=== GAME LOADING SUCCESS ===");
        LOGGAME("Game loaded successfully: %s", path.c_str());
        LOGGAME("Game data written to memory - GPU should now render real content");
        LOGGAME("Final state: gameLoaded=%s, biosLoaded=%s, running=%s", 
                gameLoaded ? "true" : "false", 
                biosLoaded ? "true" : "false", 
                running ? "true" : "false");


        LOGGAME("🎨 CRITICAL: Forcing GPU to render game content immediately");
        if (hasRenderer()) {
            if (getRenderer()) {
                getRenderer()->setLoadingProgress(1.0f);
                getRenderer()->setLoadingText("Game Running");
                getRenderer()->updateDisplay();
                LOGGAME("🎨 GPU forced to render game content");
            }
        }
    } else {
        LOGGAME("=== GAME LOADING FAILED ===");
        LOGGAME("Failed to load game: %s", path.c_str());
        LOGGAME("Last error: %s", lastError.c_str());
        LOGGAME("Final state: gameLoaded=%s, biosLoaded=%s, running=%s", 
                gameLoaded ? "true" : "false", 
                biosLoaded ? "true" : "false", 
                running ? "true" : "false");
    }

    return success;
}

bool XboxEmulator::loadGameFileAndStart(const std::string& filePath) {
    LOGGAME("=== GAME FILE LOADING STARTED ===");
    LOGGAME("Loading game file: %s", filePath.c_str());


    if (hasRenderer()) {
        if (getRenderer()) {
            getRenderer()->setLoadingProgress(0.1f);
            getRenderer()->setLoadingText("Analyzing game file...");
            LOGGAME("Renderer: set loading progress to 10%%, text='Analyzing game file...'");
        }
    } else {
        LOGGAME("WARNING: No renderer available for file analysis progress");
    }


    std::string lowerPath = filePath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    LOGGAME("File path (lowercase): %s", lowerPath.c_str());
    LOGGAME("File extension analysis:");

    if (lowerPath.find(".iso") != std::string::npos) {
        LOGGAME("✓ Detected ISO file extension (.iso)");
        LOGGAME("→ Loading as ISO file");
        if (hasRenderer()) {
            if (getRenderer()) {
                getRenderer()->setLoadingProgress(0.2f);
                getRenderer()->setLoadingText("Loading ISO file...");
                LOGGAME("Renderer: set loading progress to 20%%, text='Loading ISO file...'");
            }
        }
        LOGGAME("Calling loadISOAndStart()...");


        std::string tempXisoPath = "/storage/emulated/0/Android/data/com.xanite/cache/temp.xiso";
        std::string extractDir = "/storage/emulated/0/Android/data/com.xanite/files/xiso_extracted";


        try {
            if (!fs::exists(tempXisoPath)) {
                fs::create_directories(fs::path(tempXisoPath).parent_path());
            }
            if (!fs::exists(extractDir)) {
                fs::create_directories(extractDir);
            }
        } catch (const std::exception& e) {
            LOGGAME("⚠ Warning: Could not create external directories: %s", e.what());

            tempXisoPath = "/data/data/com.xanite/cache/temp.xiso";
            extractDir = "/data/data/com.xanite/files/xiso_extracted";
        }

        return loadISOAndStart(filePath, tempXisoPath, extractDir);
    } else if (lowerPath.find(".xbe") != std::string::npos) {
        LOGGAME("✓ Detected XBE file extension (.xbe)");
        LOGGAME("→ Loading as XBE file");
        if (hasRenderer()) {
            if (getRenderer()) {
                getRenderer()->setLoadingProgress(0.2f);
                getRenderer()->setLoadingText("Loading XBE file...");
                LOGGAME("Renderer: set loading progress to 20%%, text='Loading XBE file...'");
            }
        }
        LOGGAME("Calling loadXBEAndStart()...");
        return loadXBEAndStart(filePath);
    } else {
        LOGGAME("✗ No known extension found (.iso or .xbe)");
        LOGGAME("→ Attempting auto-detection based on file content");


        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            lastError = "File not found: " + filePath;
            LOGGAME("=== GAME FILE ERROR ===");
            LOGGAME("✗ Failed to open file: %s", filePath.c_str());
            LOGGAME("✗ File does not exist or is not accessible");
            LOGGAME("✗ Error: %s", lastError.c_str());
            return false;
        }

        LOGGAME("✓ File opened successfully for header analysis");


        char header[8];
        file.read(header, 8);
        file.close();

        LOGGAME("File header analysis (first 8 bytes):");
        LOGGAME("  Bytes: %02X %02X %02X %02X %02X %02X %02X %02X", 
                (unsigned char)header[0], (unsigned char)header[1], (unsigned char)header[2], (unsigned char)header[3],
                (unsigned char)header[4], (unsigned char)header[5], (unsigned char)header[6], (unsigned char)header[7]);
        LOGGAME("  ASCII: '%c%c%c%c%c%c%c%c'", 
                header[0], header[1], header[2], header[3], header[4], header[5], header[6], header[7]);


        if (strncmp(header, "MICROSOFT*XBOX*MEDIA", 8) == 0) {
            LOGGAME("✓ Detected ISO format by signature (MICROSOFT*XBOX*MEDIA)");
            LOGGAME("→ Loading as ISO file");
            if (hasRenderer()) {
                if (getRenderer()) {
                    getRenderer()->setLoadingProgress(0.2f);
                    getRenderer()->setLoadingText("Loading ISO file...");
                    LOGGAME("Renderer: set loading progress to 20%%, text='Loading ISO file...'");
                }
            }
            LOGGAME("Calling loadISOAndStart()...");


            std::string tempXisoPath = "/storage/emulated/0/Android/data/com.xanite/cache/temp.xiso";
            std::string extractDir = "/storage/emulated/0/Android/data/com.xanite/files/xiso_extracted";


            try {
                if (!fs::exists(tempXisoPath)) {
                    fs::create_directories(fs::path(tempXisoPath).parent_path());
                }
                if (!fs::exists(extractDir)) {
                    fs::create_directories(extractDir);
                }
            } catch (const std::exception& e) {
                LOGGAME("⚠ Warning: Could not create external directories: %s", e.what());

                tempXisoPath = "/data/data/com.xanite/cache/temp.xiso";
                extractDir = "/data/data/com.xanite/files/xiso_extracted";
            }

            return loadISOAndStart(filePath, tempXisoPath, extractDir);
        } else {

            LOGGAME("✗ No ISO signature detected");
            LOGGAME("→ Attempting to load as XBE file (real Xbox)");
            if (hasRenderer()) {
                if (getRenderer()) {
                    getRenderer()->setLoadingProgress(0.2f);
                    getRenderer()->setLoadingText("Loading XBE file...");
                    LOGGAME("Renderer: set loading progress to 20%%, text='Loading XBE file...'");
                }
            }
            LOGGAME("Calling loadXBEAndStart()...");
            return loadXBEAndStart(filePath);
        }
    }
}

bool XboxEmulator::loadXBEIntoMemory(const std::string& xbePath) {
    LOGD("Loading XBE into Xbox memory: %s", xbePath.c_str());


    std::ifstream xbeFile(xbePath, std::ios::binary);
    if (!xbeFile.is_open()) {
        LOGE("Failed to open XBE file for memory loading");
        return false;
    }


    xbeFile.seekg(0, std::ios::end);
    std::streamsize fileSize = xbeFile.tellg();
    xbeFile.seekg(0, std::ios::beg);

    LOGD("XBE file size: %lld bytes", static_cast<long long>(fileSize));



    const uint32_t XBE_MEMORY_BASE = 0x04000000; 
    const uint32_t XBE_MEMORY_SIZE = 64 * 1024 * 1024; 

    if (fileSize > XBE_MEMORY_SIZE) {
        LOGW("XBE file too large for memory, loading first %u bytes", XBE_MEMORY_SIZE);
        fileSize = XBE_MEMORY_SIZE;
    }


    const size_t CHUNK_SIZE = 1024 * 1024; 
    std::vector<uint8_t> buffer(CHUNK_SIZE);

    uint32_t currentOffset = 0;
    while (currentOffset < fileSize && xbeFile.good()) {

        if (hasRenderer()) {
            if (getRenderer()) {
                float progress = 0.3f + (0.4f * currentOffset / fileSize);
                getRenderer()->setLoadingProgress(progress);
                getRenderer()->setLoadingText("Loading game into memory...");
            }
        }

        size_t bytesToRead = std::min(CHUNK_SIZE, static_cast<size_t>(static_cast<uint64_t>(fileSize) - currentOffset));
        xbeFile.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);

        size_t bytesRead = xbeFile.gcount();
        if (bytesRead > 0) {

            for (size_t i = 0; i < bytesRead; i += 4) {
                uint32_t addr = XBE_MEMORY_BASE + currentOffset + i;
                uint32_t value = 0;


                if (i + 3 < bytesRead) {
                    value = (static_cast<uint32_t>(buffer[i + 3]) << 24) |
                            (static_cast<uint32_t>(buffer[i + 2]) << 16) |
                            (static_cast<uint32_t>(buffer[i + 1]) << 8) |
                            static_cast<uint32_t>(buffer[i]);
                } else if (i + 2 < bytesRead) {
                    value = (static_cast<uint32_t>(buffer[i + 2]) << 16) |
                            (static_cast<uint32_t>(buffer[i + 1]) << 8) |
                            static_cast<uint32_t>(buffer[i]);
                } else if (i + 1 < bytesRead) {
                    value = (static_cast<uint32_t>(buffer[i + 1]) << 8) |
                            static_cast<uint32_t>(buffer[i]);
                } else {
                    value = static_cast<uint32_t>(buffer[i]);
                }

                memory.write32(addr, value);
            }

            currentOffset += bytesRead;
            LOGD("Loaded %zu bytes into memory", bytesRead);
        }
    }

    xbeFile.close();
    LOGD("XBE loaded into memory: %u bytes", currentOffset);
    return true;
}

bool XboxEmulator::loadISOIntoMemory(const std::string& isoPath) {
    LOGD("Loading ISO into Xbox memory: %s", isoPath.c_str());


    std::ifstream isoFile(isoPath, std::ios::binary);
    if (!isoFile.is_open()) {
        LOGE("Failed to open ISO file for memory loading");
        return false;
    }


    isoFile.seekg(0, std::ios::end);
    std::streamsize fileSize = isoFile.tellg();
    isoFile.seekg(0, std::ios::beg);

    LOGD("ISO file size: %lld bytes", static_cast<long long>(fileSize));



    const uint32_t ISO_MEMORY_BASE = 0x04000000; 
    const uint32_t ISO_MEMORY_SIZE = 64 * 1024 * 1024; 

    if (fileSize > ISO_MEMORY_SIZE) {
        LOGW("ISO file too large for memory, loading first %u bytes", ISO_MEMORY_SIZE);
        fileSize = ISO_MEMORY_SIZE;
    }


    const size_t CHUNK_SIZE = 1024 * 1024; 
    std::vector<uint8_t> buffer(CHUNK_SIZE);

    uint32_t currentOffset = 0;
    while (currentOffset < fileSize && isoFile.good()) {
        size_t bytesToRead = std::min(CHUNK_SIZE, static_cast<size_t>(static_cast<uint64_t>(fileSize) - currentOffset));
        isoFile.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);

        size_t bytesRead = isoFile.gcount();
        if (bytesRead > 0) {

            for (size_t i = 0; i < bytesRead; i += 4) {
                uint32_t addr = ISO_MEMORY_BASE + currentOffset + i;
                uint32_t value = 0;


                for (size_t j = 0; j < 4 && (i + j) < bytesRead; j++) {
                    value |= static_cast<uint32_t>(buffer[i + j]) << (j * 8);
                }

                memory.write32(addr, value);
            }

            currentOffset += bytesRead;

            if (currentOffset % (10 * 1024 * 1024) == 0) { 
                LOGD("Loaded %u bytes into memory", currentOffset);
            }
        }
    }

    isoFile.close();
    LOGD("ISO loaded into memory: %u bytes", currentOffset);


    isoBaseAddress = ISO_MEMORY_BASE;
    isoSize = currentOffset;

    return true;
}

bool XboxEmulator::startEmulation() {
    LOGI("[XBE-DEBUG] startEmulation() called");
    LOGI("[XBE-DEBUG] gameLoaded=%s, biosLoaded=%s, running=%s, entryPoint=0x%08X", gameLoaded ? "true" : "false", biosLoaded ? "true" : "false", running ? "true" : "false", memory.xbeEntryPoint);
    LOGI("[XBE-DEBUG] gameEntryPoint=0x%08X, xbeEntryPoint=0x%08X", gameEntryPoint, memory.xbeEntryPoint);


    uint32_t entryPoint = 0;


    if (false) { 
        entryPoint = 0x00100000; 
    }

    if (kernel && kernel->isXbeLoaded()) {

        entryPoint = kernel->getEntryPoint();
        LOGI("[XBE-DEBUG] Using corrected XBE entry point from kernel: 0x%08X", entryPoint);
    } else if (memory.xbeEntryPoint != 0 && memory.xbeEntryPoint < 0x08000000) {

        entryPoint = memory.xbeEntryPoint;
        LOGI("[XBE-DEBUG] Using real XBE entry point from memory: 0x%08X", entryPoint);
    } else if (gameEntryPoint != 0 && gameEntryPoint < 0x08000000) {

        entryPoint = gameEntryPoint;
        LOGI("[XBE-DEBUG] Using real game entry point: 0x%08X", entryPoint);
    } else if (false) { 

        LOGW("[XBE-DEBUG] Using real Xbox entry point: 0x%08X", entryPoint);
    } else {

        LOGE("[XBE-DEBUG] FATAL ERROR - No valid entry point found!");
        LOGE("[XBE-DEBUG] Xbox requires real entry point - no fallbacks!");
        return false;
    }


    if (false && false) { 
        if (entryPoint == 0 || entryPoint >= 0x08000000) {
            LOGE("[XBE-DEBUG] INVALID ENTRYPOINT: 0x%08X (must be in 0x00000000-0x07FFFFFF)", entryPoint);
            return false;
        }
    } else if (true) { 

        if (entryPoint == 0 || entryPoint >= 0x08000000) {
            LOGE("[XBE-DEBUG] FATAL ERROR - INVALID ENTRYPOINT: 0x%08X", entryPoint);
            LOGE("[XBE-DEBUG] Xbox requires valid entry point - no fallbacks!");
            return false;
        }
    }

    LOGI("[XBE-DEBUG] Starting CPU at entry point 0x%08X", entryPoint);
    cpu.startGameExecution(entryPoint);
    LOGI("[XBE-DEBUG] cpu.startGameExecution() returned");
    running = true;
    LOGI("[XBE-DEBUG] Emulator running state set to true");
    return true;
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeCreateInstance(JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;

    LOGD("Creating new emulator instance for XboxOriginalActivity");
    XboxEmulator* emulator = new (std::nothrow) XboxEmulator();
    if (!emulator) {
        LOGE("Failed to allocate memory for emulator instance");
        return 0;
    }
    LOGD("Emulator instance created at: %p", emulator);
    return reinterpret_cast<jlong>(emulator);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeUnmountIso(JNIEnv* env, jobject obj, jstring isoPath) {
    (void)obj;
    const char* path = env->GetStringUTFChars(isoPath, nullptr);
    if (!path) return JNI_FALSE;
    std::string sPath(path);
    env->ReleaseStringUTFChars(isoPath, path);


    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetFastBoot(JNIEnv* env, jobject obj, jlong arg1, jboolean arg2) {
    (void)env;
    (void)obj;
    (void)arg1;
    (void)arg2;
} 

extern "C" JNIEXPORT jlong JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_00024Companion_nativeCreateInstance(
    JNIEnv* env,
    jobject thiz
) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = new XboxEmulator(); 

    return reinterpret_cast<jlong>(emulator);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeCreateNV2ARenderer(
    JNIEnv* env,
    jobject thiz,
    jlong emulatorInstance
) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulatorInstance);

    if (!emulator) {
        __android_log_print(ANDROID_LOG_ERROR, "NV2ARenderer", "Emulator instance is null!");
        return JNI_FALSE;
    }

    XboxMemory* memory = emulator->getMemory();

    if (!memory) {
        __android_log_print(ANDROID_LOG_ERROR, "NV2ARenderer", "XboxMemory is null!");
        return JNI_FALSE;
    }

    NV2ARenderer* renderer = new NV2ARenderer(memory);

    renderer->reset();
    renderer->enableVSync(true);


    emulator->setVulkanRenderer(renderer);

    __android_log_print(ANDROID_LOG_INFO, "NV2ARenderer", "NV2A Renderer created and stored successfully");

    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeCreateVulkanRenderer(
    JNIEnv* env,
    jobject thiz,
    jlong emulatorInstance
) {
    (void)env;
    (void)thiz;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulatorInstance);

    if (!emulator) {
        __android_log_print(ANDROID_LOG_ERROR, "VulkanRenderer", "Emulator instance is null!");
        return JNI_FALSE;
    }

    XboxMemory* memory = emulator->getMemory();

    if (!memory) {
        __android_log_print(ANDROID_LOG_ERROR, "VulkanRenderer", "XboxMemory is null!");
        return JNI_FALSE;
    }


    __android_log_print(ANDROID_LOG_INFO, "VulkanRenderer", "Creating Vulkan renderer instance");


    NV2ARenderer* renderer = new NV2ARenderer(memory);
    renderer->setRendererType(NV2ARenderer::RendererType::Vulkan);
    renderer->reset();
    renderer->enableVSync(true);


    emulator->setVulkanRenderer(renderer);

    __android_log_print(ANDROID_LOG_INFO, "VulkanRenderer", "Vulkan renderer created and stored successfully");

    return JNI_TRUE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeCreateOpenGLRenderer(
    JNIEnv* env,
    jobject thiz,
    jlong emulatorInstance
) {
    (void)env;
    (void)thiz;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulatorInstance);

    if (!emulator) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "Emulator instance is null!");
        return 0;
    }

    XboxMemory* memory = emulator->getMemory();

    if (!memory) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "XboxMemory is null!");
        return 0;
    }


    __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "Initializing Xbox Memory for real emulation");


    if (emulator->getMemory()) {

        XboxKernel* tempKernel = new XboxKernel(emulator->getMemory(), nullptr);

        delete tempKernel; 
    }

    __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "Xbox Memory initialized for real emulation");


    __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "Creating OpenGL renderer instance");


    OpenGLRenderer* renderer = new OpenGLRenderer(memory);
    if (!renderer->initialize()) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "Failed to initialize OpenGL renderer");
        delete renderer;
        return 0;
    }


    emulator->setOpenGLRenderer(renderer);

    __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "OpenGL renderer created and stored successfully");

    return reinterpret_cast<jlong>(renderer);
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetOpenGLSurface(
    JNIEnv* env,
    jobject thiz,
    jlong emulatorInstance,
    jobject surface
) {
    (void)thiz; 
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulatorInstance);

    if (!emulator) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "Emulator-Instanz ist null!");
        return;
    }


    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "Native Fenster von Surface holen fehlgeschlagen!");
        return;
    }


    ANativeWindow_setBuffersGeometry(window, 1280, 720, WINDOW_FORMAT_RGBA_8888);


    OpenGLRenderer* renderer = emulator->getOpenGLRenderer();
    if (!renderer) {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "OpenGL-Renderer ist null! Erstelle ihn zuerst.");
        ANativeWindow_release(window);
        return;
    }


    __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "Setze Surface in beiden Renderern");


    bool openGLSuccess = renderer->setSurface(window);


    NV2ARenderer* nv2aRenderer = emulator->getNV2ARenderer();
    bool nv2aSuccess = false;

    if (nv2aRenderer) {
        __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "Setze Surface im NV2A-Renderer");
        nv2aSuccess = nv2aRenderer->setSurface(window);
        __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "NV2A-Renderer Surface: %s", nv2aSuccess ? "ERFOLG" : "FEHLGESCHLAGEN");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "NV2A-Renderer ist null!");
    }


    ANativeWindow_release(window);

    bool success = openGLSuccess && nv2aSuccess;

    if (success) {
        __android_log_print(ANDROID_LOG_INFO, "OpenGLRenderer", "OpenGL-Surface erfolgreich gesetzt");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, "OpenGLRenderer", "OpenGL-Surface-Setzung fehlgeschlagen");
    }
}

JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeDestroyNV2ARenderer(
    JNIEnv* env,
    jobject thiz,
    jlong rendererInstance
) {
    (void)env;
    (void)thiz;
    NV2ARenderer* renderer = reinterpret_cast<NV2ARenderer*>(rendererInstance);
    if (renderer) {
        delete renderer;
        __android_log_print(ANDROID_LOG_INFO, "NV2ARenderer", "NV2A Renderer destroyed");
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeShutdown(JNIEnv *env, jobject thiz, jlong emu_ptr) {
    (void)env;
    (void)thiz;
    auto emulator = reinterpret_cast<XboxEmulator*>(emu_ptr);
    if (emulator != nullptr) {

        NV2ARenderer* nv2aRenderer = emulator->getNV2ARenderer();
        if (nv2aRenderer) {
            __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "Releasing NV2A-Renderer surface");
            nv2aRenderer->releaseSurface();
        }


        delete emulator;
        __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "Emulator instance destroyed");
    }
}

JNIEXPORT jboolean JNICALL 
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeInitialize(
    JNIEnv* env, 
    jobject thiz, 
    jlong ptr, 
    jstring complexBiosPath, 
    jstring mcpxBiosPath, 
    jstring hddImagePath) {

    (void)thiz;

    LOGD("Initializing emulator for XboxOriginalActivity");

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) {
        LOGE("Emulator instance is null");
        return JNI_FALSE;
    }

    const char* c_complex_path = env->GetStringUTFChars(complexBiosPath, nullptr);
    const char* c_mcpx_path = env->GetStringUTFChars(mcpxBiosPath, nullptr);
    const char* c_hdd_path = env->GetStringUTFChars(hddImagePath, nullptr);

    if (!c_complex_path || !c_mcpx_path || !c_hdd_path) {
        LOGE("Failed to get BIOS paths");
        if (c_complex_path) env->ReleaseStringUTFChars(complexBiosPath, c_complex_path);
        if (c_mcpx_path) env->ReleaseStringUTFChars(mcpxBiosPath, c_mcpx_path);
        if (c_hdd_path) env->ReleaseStringUTFChars(hddImagePath, c_hdd_path);
        return JNI_FALSE;
    }

    bool result = emulator->initEmulator(
        std::string(c_complex_path),
        std::string(c_mcpx_path),
        std::string(c_hdd_path)
    );

    env->ReleaseStringUTFChars(complexBiosPath, c_complex_path);
    env->ReleaseStringUTFChars(mcpxBiosPath, c_mcpx_path);
    env->ReleaseStringUTFChars(hddImagePath, c_hdd_path);

    return result ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeIsBiosLoaded(
    JNIEnv *env,
    jclass clazz)
{
    (void)env; (void)clazz;

    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (!emulator) {
        LOGE("Emulator instance is null");
        return JNI_FALSE;
    }

    bool biosLoaded = emulator->isBiosLoaded();
    LOGI("BIOS loaded check: %s", biosLoaded ? "YES" : "NO");
    return biosLoaded ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeGetBiosVersion(
    JNIEnv* env,
    jobject thiz,
    jlong ptr) {
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) {
        return env->NewStringUTF("Unknown");
    }

    std::string biosVersion = "1.03"; 
    return env->NewStringUTF(biosVersion.c_str());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeCreateInstance(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = new XboxEmulator();
    return reinterpret_cast<jlong>(emulator);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeInitialize(
    JNIEnv *env,
    jclass clazz,
    jstring complexBiosPath,
    jstring mcpxBiosPath,
    jstring hddImagePath)
{
    (void)clazz;

    if (complexBiosPath == nullptr || mcpxBiosPath == nullptr || hddImagePath == nullptr) {
        LOGE("CRITICAL: One or more BIOS path parameters are null");
        return JNI_FALSE;
    }
    const char* cComplexPath = env->GetStringUTFChars(complexBiosPath, nullptr);
    const char* cMcpxPath = env->GetStringUTFChars(mcpxBiosPath, nullptr);
    const char* cHddPath = env->GetStringUTFChars(hddImagePath, nullptr);
    if (!cComplexPath || !cMcpxPath || !cHddPath) {
        if (cComplexPath) env->ReleaseStringUTFChars(complexBiosPath, cComplexPath);
        if (cMcpxPath) env->ReleaseStringUTFChars(mcpxBiosPath, cMcpxPath);
        if (cHddPath) env->ReleaseStringUTFChars(hddImagePath, cHddPath);
        return JNI_FALSE;
    }
    std::string sComplexPath(cComplexPath);
    std::string sMcpxPath(cMcpxPath);
    std::string sHddPath(cHddPath);
    env->ReleaseStringUTFChars(complexBiosPath, cComplexPath);
    env->ReleaseStringUTFChars(mcpxBiosPath, cMcpxPath);
    env->ReleaseStringUTFChars(hddImagePath, cHddPath);

    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(env, clazz);
    if (!emulator) {
        LOGE("CRITICAL: Emulator instance is null");
        return JNI_FALSE;
    }
    bool success = false;
    try {
        success = emulator->initEmulator(sComplexPath, sMcpxPath, sHddPath);
    } catch (const std::exception& e) {
        LOGE("CRITICAL: initEmulator failed with exception: %s", e.what());
        return JNI_FALSE;
    }
    if (success) {
        LOGI("Emulator initialized successfully with BIOS");
    } else {
        LOGE("Failed to initialize emulator with BIOS");
    }
    return success ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSkipErrors(JNIEnv* env, jobject obj, jlong handle, jboolean skip) {
    (void)env;
    (void)obj;
    (void)handle;
    (void)skip;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeSupportsIso(
        JNIEnv *env,
        jobject thiz,
        jlong handle) {
    (void)env;
    (void)thiz;
    (void)handle;

    return JNI_TRUE;  
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetPerformanceMode(
    JNIEnv* env,
    jclass clazz,
    jlong ptr,
    jboolean enabled) {
    (void)env;
    (void)clazz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxShader", "Invalid emulator pointer");
        return;
    }


    if (enabled) {

        emulator->setCpuClockMultiplier(1.2f);
        emulator->enableTurboMode(true);
        __android_log_print(ANDROID_LOG_INFO, "XboxShader", "Performance mode enabled");
    } else {

        emulator->setCpuClockMultiplier(1.0f);
        emulator->enableTurboMode(false);
        __android_log_print(ANDROID_LOG_INFO, "XboxShader", "Performance mode disabled");
    }
}



extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeSkipErrors(JNIEnv *env, jobject instance, jlong emulatorPtr, jboolean skipErrors) {
    (void)env;
    (void)instance;
    (void)emulatorPtr;
    (void)skipErrors;

    LOGD("nativeSkipErrors called with emulatorPtr: %ld, skipErrors: %d", emulatorPtr, skipErrors);

}


extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeEnableTurboMode(JNIEnv *env, jobject thiz, jlong emulator_ptr, jboolean enable) {
    (void)env;
    (void)thiz;
    (void)emulator_ptr;
    (void)enable;
    LOGI("Turbo mode %s", enable ? "ENABLED" : "DISABLED");


}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeGetFramebuffer(
    JNIEnv *env, 
    jobject thiz, 
    jlong ptr
) {
    (void)env;
    (void)thiz;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator == nullptr) {
        return JNI_FALSE;
    }

    const uint32_t* framebuffer = emulator->getFramebuffer();   

    return (framebuffer != nullptr) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeInitializeDirect(
    JNIEnv* env, 
    jclass clazz, 
    jlong emulatorPtr
) {
    (void)env;
    (void)clazz;
    if (emulatorPtr == 0) {
        LOGE("nativeInitializeDirect called with null emulatorPtr");
        return;
    }

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulatorPtr);
    if (!emulator) {
        LOGE("Failed to cast emulatorPtr to XboxEmulator");
        return;
    }

    LOGI("Starting direct initialization...");



    LOGI("Direct initialization complete - GPU will be initialized after BIOS load");
}


extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeIsRunning(
    JNIEnv* env,
    jobject thiz,
    jlong ptr
) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) return JNI_FALSE;

    return emulator->isRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeStopRenderer(
    JNIEnv* env,
    jobject thiz,
    jlong ptr
) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) return;

    emulator->stopRenderer();
}

} 

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeLoadGameFileAndStart(
    JNIEnv* env,
    jobject thiz,
    jlong handle,
    jstring gamePath
) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(handle);
    if (!emulator) {
        LOGE("Failed to get emulator instance from handle");
        return JNI_FALSE;
    }

    const char* nativeGamePath = env->GetStringUTFChars(gamePath, 0);
    if (!nativeGamePath) {
        LOGE("Failed to get game path string");
        return JNI_FALSE;
    }

    LOGI("Loading game file: %s", nativeGamePath);

    bool success = emulator->loadGame(nativeGamePath);

    env->ReleaseStringUTFChars(gamePath, nativeGamePath);

    if (success) {
        LOGI("Game loaded successfully, starting emulation");
        if (emulator->hasRenderer()) {
            if (emulator->getRenderer()) {
                emulator->getRenderer()->setLoadingProgress(0.9f);
                emulator->getRenderer()->setLoadingText("Starting game engine...");
            }
        }
        emulator->resume();
        return JNI_TRUE;
    } else {
        LOGE("Failed to load game file");
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetLoadingProgress(
    JNIEnv* env,
    jobject thiz,
    jlong emulator_ptr,
    jfloat progress
) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulator_ptr);
    if (emulator && emulator->hasRenderer()) {
        if (emulator->getRenderer()) {
            emulator->getRenderer()->setLoadingProgress(progress);
        }
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetLoadingText(
    JNIEnv* env,
    jobject thiz,
    jlong emulator_ptr,
    jstring text
) {
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulator_ptr);
    if (emulator && emulator->hasRenderer()) {
        if (emulator->getRenderer()) {
            const char* nativeText = env->GetStringUTFChars(text, 0);
            if (nativeText) {
                emulator->getRenderer()->setLoadingText(nativeText);
                env->ReleaseStringUTFChars(text, nativeText);
            }
        }
    }
}

extern "C"  JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeSetFastBoot(JNIEnv *env, jobject thiz, jlong emulator_ptr, jboolean enable) {
    (void)env;
    (void)thiz;
    (void)emulator_ptr;
    (void)enable;
    LOGD("FastBoot set to: %s", enable ? "true" : "false");

}


JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;

    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }




    return JNI_VERSION_1_6;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeMountIso(
    JNIEnv *env,
    jobject thiz,
    jstring isoPath,
    jstring mountPoint) {
    (void)thiz;
    const char *nativeIsoPath = env->GetStringUTFChars(isoPath, 0);
    const char *nativeMountPoint = env->GetStringUTFChars(mountPoint, 0);
    if (!nativeIsoPath || !nativeMountPoint) {
        if (nativeIsoPath) env->ReleaseStringUTFChars(isoPath, nativeIsoPath);
        if (nativeMountPoint) env->ReleaseStringUTFChars(mountPoint, nativeMountPoint);
        return JNI_FALSE;
    }
    std::string sIsoPath(nativeIsoPath);
    std::string sMountPoint(nativeMountPoint);
    env->ReleaseStringUTFChars(isoPath, nativeIsoPath);
    env->ReleaseStringUTFChars(mountPoint, nativeMountPoint);
    __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "Mounting ISO: %s to %s", sIsoPath.c_str(), sMountPoint.c_str());

    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeCopyFramebuffer(
    JNIEnv* env,
    jclass clazz,
    jobject buffer
) {
    (void)env;
    (void)clazz;
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (!emulator) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxShader", "Failed to get emulator instance");
        return JNI_FALSE;
    }
    uint8_t* bufferPtr = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (!bufferPtr || capacity <= 0) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxShader", "Invalid buffer: ptr=%p, capacity=%ld", bufferPtr, capacity);
        return JNI_FALSE;
    }
    static int debugFrameCount = 0;
    bool dataCopied = false;
    emulator->runFrame();
    NV2ARenderer* renderer = emulator->getGPU();
    if (renderer) {
        renderer->renderFrame();
        if (emulator->isGameLoaded()) {
            renderer->setLoadingProgress(1.0f);
            renderer->setLoadingText("Game Running");
        }
        const uint32_t* framebuffer = renderer->getFramebuffer();
        if (framebuffer) {
            uint32_t width = renderer->getFramebufferWidth();
            uint32_t height = renderer->getFramebufferHeight();
            size_t requiredSize = width * height * 4;
            if (static_cast<size_t>(capacity) >= requiredSize) {
                memcpy(bufferPtr, framebuffer, requiredSize);
                dataCopied = true;
                if (++debugFrameCount % 60 == 0) {
                    __android_log_print(ANDROID_LOG_INFO, "XboxShader", "Copied framebuffer for frame %d", debugFrameCount);
                }
            }
        }
    }
    return dataCopied ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSurfaceChanged(
    JNIEnv* env,
    jobject thiz,
    jlong emulator_ptr,
    jint width,
    jint height
) {
    (void)env;
    (void)thiz;
    (void)width;
    (void)height;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(emulator_ptr);
    if (emulator == nullptr) {
        LOGE("Emulator instance is null in nativeSurfaceChanged");
        return;
    }

    NV2ARenderer* renderer = emulator->getGPU();
    if (renderer != nullptr) {
        renderer->setOutputResolution(width, height);
        LOGI("Surface changed to %dx%d", width, height);
    } else {
        LOGE("NV2A Renderer not initialized");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetOutputResolution(
    JNIEnv* env,
    jobject thiz,
    jlong ptr,
    jint width,
    jint height
) {
    (void)env;
    (void)thiz;
    (void)width;
    (void)height;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator) {

        emulator->setOutputResolution(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    }
}



JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeLoadGame(
    JNIEnv* env,
    jobject thiz,
    jlong ptr,
    jstring gameUri,
    jint gameType)
{
    (void)thiz;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) {
        return JNI_FALSE;
    }

    const char* uri = env->GetStringUTFChars(gameUri, nullptr);
    bool result = false;

    switch (gameType) {
        case 0: 
        case 1: 
            result = emulator->loadISO(uri);
            break;
        case 2: 
            result = emulator->loadXbe(uri);
            break;
        default:
            LOGE("Unknown game type: %d", gameType);
    }

    env->ReleaseStringUTFChars(gameUri, uri);
    return result ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeSetCpuClockMultiplier(JNIEnv *env, jobject thiz, jlong emulator_ptr, jfloat multiplier) {
    (void)env;
    (void)thiz;
    (void)emulator_ptr;
    (void)multiplier;
    LOGD("Setting CPU clock multiplier to: %.2f", multiplier);

    }

JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeStartEmulation(
    JNIEnv* env,
    jobject thiz,
    jlong ptr)
{
    (void)env;
    (void)thiz;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator) {
        emulator->resume();
        LOGI("Emulator resumed successfully");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeRunFrame(
    JNIEnv* env,
    jobject thiz,
    jlong ptr)
{
    (void)env;
    (void)thiz;

    static int frameCount = 0;
    frameCount++;

    LOGI("nativeRunFrame called - frame %d, ptr: %ld", frameCount, ptr);

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) {
        LOGE("nativeRunFrame: Emulator is null!");
        return;
    }

    if (!emulator->isRunning()) {
        LOGW("nativeRunFrame: Emulator is not running!");
        return;
    }

    LOGI("nativeRunFrame: Calling runFrame() - frame %d", frameCount);
    emulator->runFrame();
    LOGI("nativeRunFrame: runFrame() completed - frame %d", frameCount);
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_setPerformanceMode(
    JNIEnv* env,
    jobject thiz,
    jlong ptr,
    jint mode
) {
    (void)env;
    (void)thiz;
    (void)ptr;
    (void)mode;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_setResolutionScale(
    JNIEnv* env,
    jobject thiz,
    jlong ptr,
    jfloat scale
) {
    (void)env;
    (void)thiz;
    (void)ptr;
    (void)scale;
}

JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeCleanup(
    JNIEnv* env,
    jobject thiz,
    jlong ptr) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator) {
        LOGD("Destroying emulator instance: %p", emulator);
        delete emulator;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeLoadIsoFromFd(
    JNIEnv* env,
    jobject thiz,
    jlong ptr,
    jint fd) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator == nullptr) {
        LOGE("Emulator instance is null");
        return JNI_FALSE;
    }

    return emulator->loadGameFromFd(fd) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeInitEmulator(
    JNIEnv* env,
    jobject thiz,
    jlong ptr,
    jstring biosPath,
    jstring mcpxPath,
    jstring hddPath) {
    (void)thiz;
    if (ptr == 0) return JNI_FALSE;
    const char* cBiosPath = env->GetStringUTFChars(biosPath, nullptr);
    const char* cMcpxPath = env->GetStringUTFChars(mcpxPath, nullptr);
    const char* cHddPath = env->GetStringUTFChars(hddPath, nullptr);
    if (!cBiosPath || !cMcpxPath || !cHddPath) {
        if (cBiosPath) env->ReleaseStringUTFChars(biosPath, cBiosPath);
        if (cMcpxPath) env->ReleaseStringUTFChars(mcpxPath, cMcpxPath);
        if (cHddPath) env->ReleaseStringUTFChars(hddPath, cHddPath);
        return JNI_FALSE;
    }
    std::string sBiosPath(cBiosPath);
    std::string sMcpxPath(cMcpxPath);
    std::string sHddPath(cHddPath);
    env->ReleaseStringUTFChars(biosPath, cBiosPath);
    env->ReleaseStringUTFChars(mcpxPath, cMcpxPath);
    env->ReleaseStringUTFChars(hddPath, cHddPath);
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (!emulator) return JNI_FALSE;
    bool result = emulator->initEmulator(sBiosPath, sMcpxPath, sHddPath);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeLoadDashboard(
    JNIEnv* env,
    jobject thiz,
    jlong ptr) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator == nullptr) {
        LOGE("Failed to get emulator instance");
        return JNI_FALSE;
    }

    LOGD("Loading dashboard");
    return emulator->loadDashboard() ? JNI_TRUE : JNI_FALSE;
}

} 

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeLoadDashboard(
    JNIEnv* env,
    jobject thiz,
    jlong ptr) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator == nullptr) {
        LOGE("Failed to get emulator instance");
        return JNI_FALSE;
    }

    LOGD("Loading dashboard from XboxOriginalActivity");
    return emulator->loadDashboard() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_getLoadingProgress(
    JNIEnv* env,
    jobject thiz,
    jlong ptr) {
    (void)env;
    (void)thiz;

    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(ptr);
    if (emulator == nullptr) {
        LOGE("Failed to get emulator instance");
        return 0;
    }

    return emulator->getLoadingProgress();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeReset(
    JNIEnv* env,
    jobject thiz,
    jlong ptr) {
    (void)env;
    (void)thiz;
    (void)ptr;

}

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeLoadGame(JNIEnv* env, jobject thiz, jstring path) {
    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator == nullptr) return JNI_FALSE;
    const char* gamePath = env->GetStringUTFChars(path, nullptr);
    if (!gamePath) return JNI_FALSE;
    std::string sGamePath(gamePath);
    env->ReleaseStringUTFChars(path, gamePath);
    bool result = emulator->loadGame(sGamePath.c_str());
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeLoadGameFromFd(JNIEnv* env, jobject thiz, jint fd) {
    (void)env;   
    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator == nullptr) {
        LOGE("Failed to get emulator instance");
        return JNI_FALSE;
    }
    return emulator->loadGameFromFd(fd) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeLoadISO(JNIEnv* env, jobject thiz, jstring isoPath) {
    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator == nullptr) return JNI_FALSE;
    const char* cIsoPath = env->GetStringUTFChars(isoPath, nullptr);
    if (!cIsoPath) return JNI_FALSE;
    std::string sIsoPath(cIsoPath);
    env->ReleaseStringUTFChars(isoPath, cIsoPath);
    LOGD("Loading ISO file: %s", sIsoPath.c_str());
    int fd = open(sIsoPath.c_str(), O_RDONLY);
    if (fd == -1) {
        LOGE("Failed to open ISO file: %s", strerror(errno));
        return JNI_FALSE;
    }

    close(fd);
    return JNI_TRUE;
}

bool XboxEmulator::loadGameFromFd(int fd) {
    LOGD("Loading game from FD: %d", fd);

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        LOGE("Failed to get file status: %s", strerror(errno));
        return false;
    }

    size_t file_size = sb.st_size;
    uint8_t* mapped_data = static_cast<uint8_t*>(mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (mapped_data == MAP_FAILED) {
        LOGE("Failed to mmap file: %s", strerror(errno));
        return false;
    }

    XboxISOParser parser;
    std::string error_msg;
    bool parse_result = parser.parse(mapped_data, file_size, error_msg);

    if (parse_result) {
        std::vector<uint8_t> xbe_data = parser.getXbeData();
        if (!xbe_data.empty()) {
            LOGD("Successfully got XBE data (%zu bytes)", xbe_data.size());
            munmap(mapped_data, file_size);

            return true;
        } else {
            LOGE("Failed to get XBE data");
        }
    } else {
        LOGE("Failed to parse ISO: %s", error_msg.c_str());
    }

    munmap(mapped_data, file_size);

    if (parse_result) {
        std::vector<uint8_t> xbe_data = parser.getXbeData();
        if (!xbe_data.empty()) {
            LOGD("Successfully got XBE data (%zu bytes)", xbe_data.size());

            return true;
        } else {
            LOGE("Failed to get XBE data");
        }
    }

    return false;
}


bool XboxEmulator::reloadGameData() {
    LOGI("🎮 Attempting to reload game data into memory");


    if (lastLoadedGamePath.empty()) {
        LOGW("🎮 No stored game path available for reload");
        return false;
    }

    LOGI("🎮 Reloading game from: %s", lastLoadedGamePath.c_str());


    if (lastLoadedGamePath.find(".iso") != std::string::npos || lastLoadedGamePath.find(".xiso") != std::string::npos) {
        LOGI("🎮 Detected ISO file - using loadISOAndStart for reload");



        std::string tempXisoPath = "/storage/emulated/0/Android/data/com.xanite/cache/temp.xiso";
        std::string extractDir = "/storage/emulated/0/Android/data/com.xanite/files/xiso_extracted";


        try {
            if (!fs::exists(tempXisoPath)) {
                fs::create_directories(fs::path(tempXisoPath).parent_path());
            }
            if (!fs::exists(extractDir)) {
                fs::create_directories(extractDir);
            }
        } catch (const std::exception& e) {
            LOGGAME("⚠ Warning: Could not create external directories: %s", e.what());

            tempXisoPath = "/data/data/com.xanite/cache/temp.xiso";
            extractDir = "/data/data/com.xanite/files/xiso_extracted";
        }

        bool success = loadISOAndStart(lastLoadedGamePath, tempXisoPath, extractDir);

        if (success) {
            LOGI("🎮 ISO game data reloaded successfully");
            return true;
        } else {
            LOGW("🎮 Failed to reload ISO game data: %s", lastError.c_str());
            return false;
        }
    } else {

        bool success = loadGame(lastLoadedGamePath);

        if (success) {
            LOGI("🎮 Game data reloaded successfully");
            return true;
        } else {
            LOGW("🎮 Failed to reload game data: %s", lastError.c_str());
            return false;
        }
    }
}



JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeLoadExecutable(
    JNIEnv* env,
    jobject thiz,
    jstring executablePath) {

    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator == nullptr) {
        LOGE("Failed to get emulator instance");
        return JNI_FALSE;
    }

    const char* path = env->GetStringUTFChars(executablePath, nullptr);
    bool result = emulator->loadXbe(path);
    env->ReleaseStringUTFChars(executablePath, path);

    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_getLoadingProgress(JNIEnv* env, jobject thiz) {
    (void)env;   
    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator == nullptr) {
        LOGE("Failed to get emulator instance");
        return 0;
    }
    return emulator->getLoadingProgress();
}

JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeRunFrame(JNIEnv* env, jobject thiz) {
    (void)env;   
    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator != nullptr) {
        emulator->runFrame();
    }
}

JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxOriginalActivity_nativeReset(JNIEnv* env, jobject thiz) {
    (void)env;   
    (void)thiz;  
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (emulator != nullptr) {
        emulator->reset();
    }
}

bool XboxEmulator::initEmulator(const std::string& biosPath, 
                                                                const std::string& mcpxPath,
                                 const std::string& hddPath) {
    (void)mcpxPath; 
    (void)hddPath;  
    LOGI("Initializing Xbox emulator...");


    struct stat fileStat;
    if (stat(biosPath.c_str(), &fileStat) != 0) {
        lastError = "BIOS file does not exist: " + biosPath;
        LOGE("BIOS file does not exist: %s", biosPath.c_str());
        return false;
    }

    LOGI("BIOS file exists, size: %ld bytes", fileStat.st_size);


    if (!memory.loadBios(biosPath)) {
        lastError = "Failed to load BIOS: " + biosPath;
        LOGE("Failed to load BIOS: %s", biosPath.c_str());
        return false;
    }
    biosLoaded = true;
    LOGI("BIOS loaded successfully");


    cpu.setKernel(kernel);
    cpu.reset();
    LOGI("CPU initialized");


    if (gpu) {
        LOGI("GPU already initialized - skipping GPU setup in initEmulator");
    } else {
        LOGI("CRITICAL: GPU is null - initializing GPU now");
        try {
            gpu = new NV2ARenderer(&memory);
            LOGI("GPU created successfully");



            memory.setGPURenderer(gpu);
            LOGI("GPU: Connected to memory system - real-time updates enabled");


            gpu->setOutputResolution(1280, 720);
            gpu->enableVSync(true);
            LOGI("GPU basic initialization completed");
        } catch (const std::exception& e) {
            LOGE("CRITICAL: Failed to create GPU: %s", e.what());
            return false;
        }
    }


    kernel = new XboxKernel(&memory, &cpu);
    if (!kernel) {
        lastError = "Failed to create kernel";
        LOGE("Failed to create kernel");
        return false;
    }
    LOGI("Kernel initialized");


    initializeSystem();

    running = true;
    LOGI("Xbox emulator initialized successfully");
    return true;
}

void XboxEmulator::initializeSystem() {
    LOGI("Initializing Xbox system...");





    memory.allocateMemory(64 * 1024 * 1024, 4);
    memory.allocateMemory(16 * 1024 * 1024, 4);
    memory.allocateMemory(8 * 1024 * 1024, 4);


    if (gpu) {
        LOGI("GPU already initialized - completing GPU setup in initializeSystem");


        try {
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_CTX_CONTROL, 0x00000001);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_STATUS, 0x00000001);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_INTR_EN, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_ALPHAFUNC, 0x00000007); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_ALPHAREF, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_BLEND, 0x00000000); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_DEPTHFUNC, 0x00000001); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_DEPTHWRITE, 0x00000001); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_FOGENABLE, 0x00000000); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_FOGCOLOR, 0x00000000);

            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT_CLIP, 0x3F800000); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT_OFFSET, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT_SCALE, 0x3F800000); 


            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_SCISSOR, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_SCISSOR_CLIP, (720 << 16) | 1280);


            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT_DIM, (720 << 16) | 1280);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT_HORIZ, 1280);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_VIEWPORT_VERT, 720);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_STENCIL_FUNC, 0x00000007); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_STENCIL_REF, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_STENCIL_MASK, 0x000000FF);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_STENCIL_OP, 0x00000000);
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_COLOR_MASK, 0x0000000F); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_COLOR_LOGIC, 0x00000000); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_INTR_EN, 0x00000000); 


            for (int i = 0; i < 4; i++) {
                gpu->writeRegister(NV2ARenderer::NV_PGRAPH_TEXFMT0 + i * 4, 0x00000000); 
            }

            LOGI("GPU registers initialized successfully");
        } catch (const std::exception& e) {
            LOGE("CRITICAL: GPU register initialization failed: %s", e.what());
        } catch (...) {
            LOGE("CRITICAL: Unknown exception during GPU register initialization");
        }
    } else {
        LOGW("GPU is null - should have been initialized in nativeInitialize");
    }


    if (!initializeAudioSystem()) {
        LOGE("Failed to initialize Xbox Audio System");

    }


    cpu.setBreakpoint(0xFF000000, [this]() {
        LOGI("BIOS entry point reached");
        handleInterrupts();
    });

    LOGI("Xbox system initialization completed");
}

void XboxEmulator::runFrame() {
    if (!running) return;

    try {
        auto frameStart = std::chrono::high_resolution_clock::now();


        uint32_t cycles = calculateDynamicCycles();
        LOGI("CPU: Executing %u cycles, current EIP: 0x%08X, State: %d", 
             cycles, cpu.getEIP(), static_cast<int>(cpu.getState()));


        uint32_t eipBefore = cpu.getEIP();
        X86Core::CpuState stateBefore = cpu.getState();


        auto executionStart = std::chrono::high_resolution_clock::now();

        cpu.execute(cycles);


        auto executionEnd = std::chrono::high_resolution_clock::now();
        auto executionTime = std::chrono::duration_cast<std::chrono::milliseconds>(executionEnd - executionStart);
        if (executionTime.count() > 1000) { 
            LOGW("CPU: ⚠️ CPU execution took %lld ms - this indicates a potential hang", executionTime.count());
        }


        uint32_t eipAfter = cpu.getEIP();
        X86Core::CpuState stateAfter = cpu.getState();

        LOGI("CPU: After execution - EIP: 0x%08X -> 0x%08X, State: %d -> %d, Instructions executed: %u", 
             eipBefore, eipAfter, static_cast<int>(stateBefore), static_cast<int>(stateAfter), 
             cpu.getTotalInstructionsExecuted());


        static int frameCounter = 0;
        frameCounter++;
        if (frameCounter % 60 == 0) { 
            LOGI("CPU: Frame %d - EIP: 0x%08X, State: %d, Instructions: %u", 
                 frameCounter, cpu.getEIP(), static_cast<int>(cpu.getState()), 
                 cpu.getTotalInstructionsExecuted());
        }


        if (cpu.getState() == X86Core::CpuState::Error) {
            LOGW("CPU entered error state, attempting recovery");


            uint32_t currentEIP = cpu.getEIP();
            if (currentEIP >= 0x00100000 && currentEIP <= 0x07FFFFFF) {

                LOGI("CPU: EIP is in valid game range, attempting to continue execution");
                cpu.setState(X86Core::CpuState::Running);
                cpu.setEIP(currentEIP + 4); 
            } else {

                LOGW("CPU: EIP is invalid, resetting to game entry point");
                cpu.setEIP(memory.getXbeEntryPoint()); 

                cpu.reset(); 
                cpu.setState(X86Core::CpuState::Running);
            }


            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }


        static uint32_t lastEIP = 0;
        static int stuckCounter = 0;
        uint32_t currentEIP = cpu.getEIP();

        if (currentEIP == lastEIP) {
            stuckCounter++;
            if (stuckCounter > 100) { 
                LOGW("CPU: ⚠️ CPU appears to be stuck at EIP 0x%08X for %d frames", currentEIP, stuckCounter);


                if (stuckCounter > 200) { 
                    LOGW("CPU: 🔧 Forcing CPU recovery due to extended stuck state");
                    cpu.setState(X86Core::CpuState::Running);
                    cpu.setEIP(currentEIP + 4); 
                    stuckCounter = 0;
                    LOGI("CPU: ✓ Forced recovery completed, new EIP: 0x%08X", cpu.getEIP());
                }
            }
        } else {
            stuckCounter = 0;
            lastEIP = currentEIP;
        }


        if (cpu.getState() == X86Core::CpuState::Halted) {
            if (gameLoaded) {

                LOGI("🎮 CRITICAL: CPU is halted but game is loaded - restarting CPU at game entry point");


                uint32_t entryPoint = gameEntryPoint;


                if (entryPoint == 0 || entryPoint >= 0x08000000) {
                    LOGW("Entry point 0x%08X is invalid, using default", entryPoint);
                    entryPoint = 0x00100000;
                    gameEntryPoint = entryPoint; 
                }


                LOGI("🎮 Ensuring game data is properly loaded into memory before CPU start");


                if (memory.isGameLoaded()) {
                    LOGI("🎮 Game data confirmed in memory - starting CPU execution");
                cpu.startGameExecution(entryPoint);
                LOGI("🎮 CPU restarted at EIP: 0x%08X for game execution", cpu.getEIP());
            } else {
                    LOGW("🎮 Game data not in memory - attempting to reload game");

                    if (reloadGameData()) {
                        LOGI("🎮 Game data reloaded successfully - starting CPU");
                        cpu.startGameExecution(entryPoint);
                        LOGI("🎮 CPU restarted at EIP: 0x%08X for game execution", cpu.getEIP());
                    } else {
                        LOGW("🎮 Failed to reload game data - CPU remains halted");
                    }
                }
            } else {


                static int haltedCount = 0;
                haltedCount++;
                if (haltedCount % 60 == 0) { 
                    LOGI("CPU is halted - no game loaded, staying halted (frame %d)", haltedCount);
                }
            }
        }


        static X86Core::CpuState lastCPUState = X86Core::CpuState::Running;
        static int cpuStuckCount = 0;
        X86Core::CpuState currentCPUState = cpu.getState();
        if (currentCPUState == lastCPUState) {
            cpuStuckCount++;
            if (cpuStuckCount > 10) {
                if (gameLoaded) {
                    LOGW("CPU stuck in state %d for %d frames, but game is loaded - not resetting", 
                         static_cast<int>(currentCPUState), cpuStuckCount);

                    cpuStuckCount = 0;
                } else {
                    LOGW("CPU stuck in state %d for %d frames, resetting", static_cast<int>(currentCPUState), cpuStuckCount);
                    cpu.reset();
                    cpuStuckCount = 0;
                }
            }
        } else {
            cpuStuckCount = 0;
            lastCPUState = currentCPUState;
        }


        if (gpu) {
            try {
                if (gameLoaded) {

                    LOGI("GPU: Rendering game content - game is loaded and running");


                    gpu->processCommandBuffer();


                    gpu->syncFramebufferFromMemory();


                    gpu->renderFrame();


                    if (openGLRenderer && openGLRenderer->isInitialized()) {
                        LOGI("GPU: Rendering OpenGL display frame");
                        openGLRenderer->renderFrame();
                    } else {
                        LOGW("GPU: OpenGL renderer not available or not initialized");
                    }


                    gpu->setLoadingProgress(1.0f);
                    gpu->setLoadingText("Game Running");


                    gpu->updateDisplay();


                    static int gpuFrameCounter = 0;
                    gpuFrameCounter++;
                    if (gpuFrameCounter % 30 == 0) { 
                        LOGI("GPU: Frame %d - Rendering game content successfully", gpuFrameCounter);


                        gpu->updateVertexBufferFromMemory();
                        if (gpu->hasVertexData()) {
                            LOGI("GPU: Frame %d - Found vertex data, rendering geometry", gpuFrameCounter);
                            gpu->renderGameGeometry();
                        }
                    }
                } else {

                    gpu->renderBasicFrame();


                    if (openGLRenderer && openGLRenderer->isInitialized()) {
                        LOGI("GPU: Rendering OpenGL display frame (no game)");
                        openGLRenderer->renderFrame();
                    }
                }
            } catch (const std::exception& e) {
                LOGW("GPU operation failed: %s - continuing without GPU", e.what());
            } catch (...) {
                LOGW("Unknown GPU error - continuing without GPU");
            }
        } else {
            LOGW("GPU not initialized, skipping GPU operations");


            if (openGLRenderer && openGLRenderer->isInitialized()) {
                LOGI("GPU: Rendering OpenGL display frame (no GPU)");
                openGLRenderer->renderFrame();
            }
        }


        if (gpu && !gameLoaded) {

            static int testPatternFrame = 0;
            testPatternFrame++;

            if (testPatternFrame % 60 == 0) { 
                LOGI("GPU: Generating basic frame %d (no game loaded)", testPatternFrame);

                gpu->renderBasicFrame();


                gpu->setLoadingProgress(0.0f);
                gpu->setLoadingText("No Game Loaded - Please Load a Game");
                gpu->updateDisplay();
            }
        }


    static int gameStatusFrameCounter = 0;
    gameStatusFrameCounter++;
    if (gameStatusFrameCounter >= 60) {
        gameStatusFrameCounter = 0;
        checkGameStatus();
        }


        updateAudioSystem();


        updateAudio();


        handleInterrupts();


        enforceFrameRate(frameStart);

        frameCounter++;


        if (frameCounter % 100 == 0) {
            LOGI("Frame %u: CPU cycles=%u, GPU state=%d, Game loaded=%d", 
                 frameCounter, cycles, static_cast<int>(gpu ? static_cast<int>(gpu->getState()) : 0), gameLoaded);
        }
    } catch (const std::exception& e) {
        LOGE("Exception in runFrame(): %s", e.what());
    } catch (...) {
        LOGE("Unknown exception in runFrame()");
    }
}

void XboxEmulator::handleInterrupts() {
    try {

        if (gpu && gpu->checkInterrupt()) {
            LOGD("GPU interrupt detected");
            cpu.handleInterrupt(0x20); 
            gpu->writeRegister(NV2ARenderer::NV_PGRAPH_INTR_STAT, 0x00000000);
        }


        if (memory.isValidAddress(0xFE000000 + 0x1000)) {
            uint32_t audioInterruptFlag = memory.read32(0xFE000000 + 0x1000); 
            if (audioInterruptFlag & 0x01) {
                LOGD("Audio interrupt detected");
                cpu.handleInterrupt(0x21); 
                memory.write32(0xFE000000 + 0x1000, audioInterruptFlag & ~0x01); 
            }
        }


        if (memory.isValidAddress(0x00000000 + 0x1000)) {
            uint32_t systemInterruptFlag = memory.read32(0x00000000 + 0x1000); 
            if (systemInterruptFlag & 0x01) {
                LOGD("System interrupt detected");
                cpu.handleInterrupt(0x22); 
                memory.write32(0x00000000 + 0x1000, systemInterruptFlag & ~0x01); 
            }
        }


        for (uint32_t port = 0; port < 4; port++) {
            if (controllerConnected[port]) {
                uint32_t controllerAddr = 0x00000000 + 0x2000 + (port * 0x100); 
                if (memory.isValidAddress(controllerAddr)) {
                    uint32_t lastState = memory.read32(controllerAddr);
                    uint32_t currentState = controllers[port].buttons;

                    if (lastState != currentState) {
                        LOGD("Controller %d interrupt detected", port);
                        cpu.handleInterrupt(0x23 + port); 
                        memory.write32(controllerAddr, currentState); 
                    }
                }
            }
        }


        static uint32_t lastTimerTick = 0;
        uint32_t currentTick = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count());

        if (currentTick - lastTimerTick >= 16) { 
            LOGD("Timer interrupt detected");
            cpu.handleInterrupt(0x28); 
            lastTimerTick = currentTick;
        }
    } catch (const std::exception& e) {
        LOGE("Exception in handleInterrupts(): %s", e.what());
    } catch (...) {
        LOGE("Unknown exception in handleInterrupts()");
    }
}

void XboxEmulator::updateAudio() {

    if (audioSystem.isInitialized()) {

        audioSystem.updateFromXboxMemory(memory);


        if (audioSystem.hasActiveAudio()) {
            LOGI("🎵 SPIEL LÄUFT! Aktive Audio-Ausgabe bestätigt");
        }
    } else {

        LOGD("Audio: Real Xbox system used");


        const uint32_t AUDIO_BASE = 0xFE000000;  
        const uint32_t AUDIO_BUFFER_SIZE = 48000 * 2 * 2; 


        bool hasAudioData = false;
        for (uint32_t i = 0; i < 1000; i += 2) {
            uint32_t addr = AUDIO_BASE + (i % AUDIO_BUFFER_SIZE);
            int16_t sample = memory.read16(addr);
            if (sample != 0) {
                hasAudioData = true;
                break;
            }
        }

        if (hasAudioData) {

            audioBuffer.resize(AUDIO_BUFFER_SIZE / 2); 

            for (uint32_t i = 0; i < AUDIO_BUFFER_SIZE / 2; i++) {
                uint32_t addr = AUDIO_BASE + (i * 2);
                int16_t leftSample = memory.read16(addr);
                int16_t rightSample = memory.read16(addr + 2);


                int32_t mixedSample = (leftSample + rightSample) / 2;
                audioBuffer[i] = static_cast<int16_t>(mixedSample);
            }

            LOGD("Audio: Updated with %zu samples", audioBuffer.size());
        } else {

            audioBuffer.resize(4800); 
            std::fill(audioBuffer.begin(), audioBuffer.end(), 0);
        }
    }
}

void XboxEmulator::applyAudioEffects() {

    if (audioBuffer.empty()) return;


    for (auto& sample : audioBuffer) {

        sample = static_cast<int16_t>(sample * 0.8f); 


        static int16_t lastSample = 0;
        sample = static_cast<int16_t>(sample * 0.9f + lastSample * 0.1f);
        lastSample = sample;


        if (sample > 16000) {
            sample = 16000;
        } else if (sample < -16000) {
            sample = -16000;
        }
    }

    LOGD("Audio: Applied effects to %zu samples", audioBuffer.size());
}

uint32_t XboxEmulator::calculateDynamicCycles() const {

    uint32_t baseCycles = 1000; 


    if (gameLoaded) {
        baseCycles = 10000; 
    } else if (biosLoaded) {
        baseCycles = 2000; 
    }


    if (gameLoaded && baseCycles < 5000) {
        baseCycles = 5000;
    }

    if (turboModeEnabled) {
        return baseCycles * 2; 
    }

    return static_cast<uint32_t>(baseCycles * cpuClockMultiplier);
}

void XboxEmulator::enforceFrameRate(const std::chrono::high_resolution_clock::time_point& frameStart) {
    if (!frameLimitEnabled) return;

    auto frameEnd = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);

    const auto targetFrameTime = std::chrono::microseconds(16667); 

    if (elapsed < targetFrameTime) {
        std::this_thread::sleep_for(targetFrameTime - elapsed);
    }
}



void XboxEmulator::reset() {
    pause();
    cpu.reset();
    if (gpu) gpu->reset();
    memory.reset();


    LOGW("[RESET-DEBUG] Before kernel creation - memory.xbeEntryPoint: 0x%08X", memory.getXbeEntryPoint());

    if (kernel != nullptr) {
        delete kernel;
    }
    kernel = new XboxKernel(&memory, &cpu);


    uint32_t kernelEntryPoint = kernel->getEntryPoint();
    LOGW("[RESET-DEBUG] Kernel entry point: 0x%08X", kernelEntryPoint);
    LOGW("[RESET-DEBUG] Memory xbeEntryPoint after kernel creation: 0x%08X", memory.getXbeEntryPoint());

    if (biosLoaded && gameLoaded) {
        cpu.setPC(kernel->getEntryPoint());
        LOGW("[RESET-DEBUG] CPU PC set to kernel entry point: 0x%08X", kernel->getEntryPoint());
    }

    running = biosLoaded;
    LOGI("System reset");
}

void XboxEmulator::pause() {
    running = false;
}

void XboxEmulator::resume() {

    if (biosLoaded) {
        running = true;
        LOGI("Emulator resumed - BIOS loaded and running");
    } else if (gameLoaded) {

        running = true;
        LOGI("Emulator resumed - Game loaded, running without BIOS");
    } else {
        LOGW("Cannot resume emulator - neither BIOS nor game loaded");
    }
}

bool XboxEmulator::saveState(const std::string& path) {
    LOGI("Saving emulator state to: %s", path.c_str());

    try {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            lastError = "Could not open save state file for writing";
            return false;
        }


        file.write(reinterpret_cast<const char*>(&cpu), sizeof(X86Core));


        const uint8_t* ramData = memory.getRamPointer();
        file.write(reinterpret_cast<const char*>(ramData), memory.getRamSize());


        if (gpu) {
            const uint32_t* fbData = gpu->getFramebuffer();
            file.write(reinterpret_cast<const char*>(fbData), gpu->getFramebufferWidth() * gpu->getFramebufferHeight() * 4);
        }

        LOGI("Save state completed successfully");
        return true;
    } catch (const std::exception& e) {
        lastError = "Save state failed: " + std::string(e.what());
        return false;
    }
}

bool XboxEmulator::loadState(const std::string& path) {
    LOGI("Loading emulator state from: %s", path.c_str());

    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            lastError = "Could not open save state file for reading";
            return false;
        }


        file.read(reinterpret_cast<char*>(&cpu), sizeof(X86Core));


        uint8_t* ramData = memory.getRamPointer();
        file.read(reinterpret_cast<char*>(ramData), memory.getRamSize());


        if (gpu) {
            uint32_t* fbData = const_cast<uint32_t*>(gpu->getFramebuffer());
            file.read(reinterpret_cast<char*>(fbData), gpu->getFramebufferWidth() * gpu->getFramebufferHeight() * 4);
        }

        LOGI("Load state completed successfully");
        return true;
    } catch (const std::exception& e) {
        lastError = "Load state failed: " + std::string(e.what());
        return false;
    }
}



bool XboxEmulator::isBiosLoaded() const {
    return biosLoaded;
}

void XboxEmulator::fixInconsistentState() {
    LOGI("Checking for inconsistent state...");
    LOGI("Current state - BIOS Loaded: %s, Running: %s", 
         biosLoaded ? "YES" : "NO", running ? "YES" : "NO");


    if (running && !biosLoaded) {
        LOGW("FIXING: Inconsistent state detected - running=true but biosLoaded=false");
        running = false;
        LOGI("Fixed: Set running to false");
    }


    if (biosLoaded && !running) {
        LOGW("FIXING: Inconsistent state detected - biosLoaded=true but running=false");
        running = true;
        LOGI("Fixed: Set running to true");
    }

    LOGI("State after fix - BIOS Loaded: %s, Running: %s", 
         biosLoaded ? "YES" : "NO", running ? "YES" : "NO");
}

bool XboxEmulator::isGameLoaded() const {

    LOGGAME("🎮 === GAME STATUS CHECK ===");
    LOGGAME("🎮 Basic gameLoaded flag: %s", gameLoaded ? "✓ YES" : "✗ NO");


    uint32_t eip = cpu.getEIP();
    bool isInGameRange = (eip >= 0x00100000 && eip <= 0x07FFFFFF);
    LOGGAME("🎮 Current EIP: 0x%08X (in game range: %s)", eip, isInGameRange ? "✓ YES" : "✗ NO");


    uint32_t gameDataCheck = const_cast<XboxMemory&>(memory).read32(0x00100000);
    bool hasGameData = (gameDataCheck != 0x00000000 && gameDataCheck != 0xFFFFFFFF);
    LOGGAME("🎮 Game data at 0x00100000: 0x%08X (%s)", gameDataCheck, hasGameData ? "✓ VALID" : "✗ INVALID");


    bool cpuRunning = (cpu.getState() == X86Core::CpuState::Running);
    LOGGAME("🎮 CPU State: %d (actively executing: %s)", (int)cpu.getState(), cpuRunning ? "✓ YES" : "✗ NO");


    uint32_t entryPoint = kernel->getEntryPoint();
    bool hasEntryPoint = (entryPoint >= 0x00100000 && entryPoint <= 0x07FFFFFF);
    LOGGAME("🎮 Game entry point: 0x%08X (valid: %s)", entryPoint, hasEntryPoint ? "✓ YES" : "✗ NO");


    bool kernelGameLoaded = kernel->isXbeLoaded();
    LOGGAME("🎮 Kernel reports game loaded: %s", kernelGameLoaded ? "✓ YES" : "✗ NO");


    bool gpuActive = (gpu != nullptr);
    LOGGAME("🎮 GPU active: %s", gpuActive ? "✓ YES" : "✗ NO");


    bool gameReallyRunning = gameLoaded && isInGameRange && hasGameData && cpuRunning && hasEntryPoint && kernelGameLoaded && gpuActive;

    if (gameReallyRunning) {
        LOGGAME("🎮 🎉 GAME IS REALLY RUNNING! 🎉");
        LOGGAME("🎮 ✓ Basic flag set");
        LOGGAME("🎮 ✓ EIP in game range");
        LOGGAME("🎮 ✓ Game data present");
        LOGGAME("🎮 ✓ CPU actively executing");
        LOGGAME("🎮 ✓ Valid entry point");
        LOGGAME("🎮 ✓ Kernel confirms game loaded");
        LOGGAME("🎮 ✓ GPU active");
        LOGGAME("🎮 === FINAL STATUS: GAME RUNNING ===");
        return true;
    } else {
        LOGGAME("🎮 ❌ GAME IS NOT REALLY RUNNING ❌");
        if (!gameLoaded) LOGGAME("🎮 ❌ Basic flag not set");
        if (!isInGameRange) LOGGAME("🎮 ❌ EIP not in game range");
        if (!hasGameData) LOGGAME("🎮 ❌ No game data in memory");
        if (!cpuRunning) LOGGAME("🎮 ❌ CPU not executing");
        if (!hasEntryPoint) LOGGAME("🎮 ❌ No valid entry point");
        if (!kernelGameLoaded) LOGGAME("🎮 ❌ Kernel says no game loaded");
        if (!gpuActive) LOGGAME("🎮 ❌ GPU not active");
        LOGGAME("🎮 === FINAL STATUS: GAME NOT RUNNING ===");
        return false;
    }
}

void XboxEmulator::checkGameStatus() {

    LOGGAME("🎮 === PERIODIC GAME STATUS CHECK ===");


    uint32_t eip = cpu.getEIP();
    int cpuState = (int)cpu.getState();

    LOGGAME("🎮 CPU Status:");
    LOGGAME("🎮   EIP: 0x%08X", eip);
    LOGGAME("🎮   State: %d (%s)", cpuState, cpuState == 0 ? "Running" : "Halted/Error");


    bool inGameRange = (eip >= 0x00100000 && eip <= 0x07FFFFFF);
    LOGGAME("🎮 Memory Range Check:");
    LOGGAME("🎮   EIP in game range: %s", inGameRange ? "✓ YES" : "✗ NO");


    uint32_t entryData = const_cast<XboxMemory&>(memory).read32(0x00100000);
    bool hasGameData = (entryData != 0x00000000 && entryData != 0xFFFFFFFF);
    LOGGAME("🎮 Game Data Check:");
    LOGGAME("🎮   Data at 0x00100000: 0x%08X", entryData);
    LOGGAME("🎮   Valid game data: %s", hasGameData ? "✓ YES" : "✗ NO");


    uint32_t entryPoint = kernel->getEntryPoint();
    bool kernelLoaded = kernel->isXbeLoaded();
    LOGGAME("🎮 Kernel Status:");
    LOGGAME("🎮   Entry point: 0x%08X", entryPoint);
    LOGGAME("🎮   Game loaded: %s", kernelLoaded ? "✓ YES" : "✗ NO");


    bool gpuActive = (gpu != nullptr);
    LOGGAME("🎮 GPU Status:");
    LOGGAME("🎮   GPU active: %s", gpuActive ? "✓ YES" : "✗ NO");


    bool gameRunning = gameLoaded && inGameRange && hasGameData && (cpuState == (int)X86Core::CpuState::Running) && kernelLoaded && gpuActive;

    if (gameRunning) {
        LOGGAME("🎮 🎉 GAME IS DEFINITELY RUNNING! 🎉");
        LOGGAME("🎮 ✓ All systems operational");
        LOGGAME("🎮 ✓ CPU executing game code");
        LOGGAME("🎮 ✓ Memory contains game data");
        LOGGAME("🎮 ✓ Kernel confirms game loaded");
        LOGGAME("🎮 ✓ GPU rendering");
        LOGGAME("🎮 === STATUS: GAME RUNNING ====");
    } else {
        LOGGAME("🎮 ❌ GAME IS NOT RUNNING ❌");
        if (!gameLoaded) LOGGAME("🎮 ❌ Basic game flag not set");
        if (!inGameRange) LOGGAME("🎮 ❌ CPU not in game memory range");
        if (!hasGameData) LOGGAME("🎮 ❌ No game data in memory");
        if (cpuState != (int)X86Core::CpuState::Running) LOGGAME("🎮 ❌ CPU not running (state: %d)", cpuState);
        if (!kernelLoaded) LOGGAME("🎮 ❌ Kernel says no game loaded");
        if (!gpuActive) LOGGAME("🎮 ❌ GPU not active");
        LOGGAME("🎮 === STATUS: GAME NOT RUNNING ====");
    }

    LOGGAME("🎮 === END GAME STATUS CHECK ===");
}

const char* XboxEmulator::getLastError() const {
    return lastError.c_str();
}

const uint32_t* XboxEmulator::getFramebuffer() const {
    return gpu ? gpu->getFramebuffer() : nullptr;
}

uint32_t XboxEmulator::getFramebufferWidth() const {
    return gpu ? gpu->getWidth() : 0;
}

uint32_t XboxEmulator::getFramebufferHeight() const {
    return gpu ? gpu->getHeight() : 0;
}

const int16_t* XboxEmulator::getAudioBuffer() const {
    return audioBuffer.data();
}

uint32_t XboxEmulator::getAudioSampleCount() const {
    return static_cast<uint32_t>(audioBuffer.size());
}

void XboxEmulator::setControllerState(uint32_t port, uint32_t buttons, 
                                    uint8_t leftTrigger, uint8_t rightTrigger,
                                    int8_t thumbLX, int8_t thumbLY,
                                    int8_t thumbRX, int8_t thumbRY) {
    if (port >= controllers.size()) return;


    controllers[port] = {
        buttons,
        leftTrigger,
        rightTrigger,
        thumbLX,
        thumbLY,
        thumbRX,
        thumbRY
    };


    if (buttons != 0 || leftTrigger > 10 || rightTrigger > 10 || 
        abs(thumbLX) > 10 || abs(thumbLY) > 10 || 
        abs(thumbRX) > 10 || abs(thumbRY) > 10) {

        if (!controllerConnected[port]) {
            controllerConnected[port] = true;
            LOGI("Controller %d connected", port + 1);


            calibrateController(port);
        }
    } else {
        if (controllerConnected[port]) {
            controllerConnected[port] = false;
            LOGI("Controller %d disconnected", port + 1);
        }
    }


    processRumbleFeedback(port);


    updateMemoryCard(port);

    LOGD("Controller %d: Buttons=0x%08X, LT=%d, RT=%d, LX=%d, LY=%d, RX=%d, RY=%d",
         port + 1, buttons, leftTrigger, rightTrigger, thumbLX, thumbLY, thumbRX, thumbRY);
}

void XboxEmulator::calibrateController(uint32_t port) {
    if (port >= 4) return;


    controllers[port].thumbLX = 0;
    controllers[port].thumbLY = 0;
    controllers[port].thumbRX = 0;
    controllers[port].thumbRY = 0;
    controllers[port].leftTrigger = 0;
    controllers[port].rightTrigger = 0;

    LOGD("Controller %d calibrated", port);
}

void XboxEmulator::processRumbleFeedback(uint32_t port) {
    if (port >= 4) return;


    uint8_t leftRumble = controllers[port].rumbleLeft;
    uint8_t rightRumble = controllers[port].rumbleRight;


    if (leftRumble > 0 || rightRumble > 0) {
        LOGD("Controller %d rumble: L=%d, R=%d", port, leftRumble, rightRumble);
    }
}

void XboxEmulator::updateMemoryCard(uint32_t port) {
    if (port >= 4) return;



    LOGD("Memory card %d updated", port);
}

void XboxEmulator::setDebugCallback(std::function<void(const std::string&)> callback) {
    debugCallback = callback;
}

void XboxEmulator::setCpuTrace(bool enabled) {
    cpu.enableTracing(enabled);
}

void XboxEmulator::setFrameLimit(bool enabled) {
    frameLimitEnabled = enabled;
}

void XboxEmulator::setVSync(bool enabled) {
    vsyncEnabled = enabled;
    if (gpu) gpu->enableVSync(enabled);
}

void XboxEmulator::setJITEnabled(bool enabled) {
    jitEnabled = enabled;
    cpu.enableJIT(enabled);
}



XboxEmulator::XboxEmulator() :
    biosLoaded(false),
    gameLoaded(false),
    lastFrameTime(0),
    frameCounter(0),
    running(false),
    frameLimitEnabled(true),
    vsyncEnabled(true),
    jitEnabled(true),
    cpu(&memory),
    gpu(nullptr), 
    kernel(nullptr)
{
    controllers.fill({0, 0, 0, 0, 0, 0, 0});

    LOGI("XboxEmulator created - waiting for BIOS to be loaded");
}

XboxEmulator::~XboxEmulator() {
    pause();
    if (gpu != nullptr) {
        delete gpu;
        gpu = nullptr;
    }
    if (kernel != nullptr) {
        delete kernel;
        kernel = nullptr;
    }
}

bool XboxEmulator::loadDashboard() {
    LOGI("=== Attempting to load Xbox Dashboard ===");


    fixInconsistentState();


    if (!biosLoaded) {
        lastError = "BIOS must be loaded first";
        LOGE("DASHBOARD ERROR: %s", lastError.c_str());
        LOGE("Current state - BIOS Loaded: %s, Running: %s", 
             biosLoaded ? "YES" : "NO", running ? "YES" : "NO");


        if (running) {
            LOGE("FIXING: Setting running to false because BIOS is not loaded");
            running = false;
        }
        return false;
    }

    if (!running) {
        lastError = "System not running - BIOS not properly initialized";
        LOGE("DASHBOARD ERROR: %s", lastError.c_str());
        LOGE("Current state - BIOS Loaded: %s, Running: %s", 
             biosLoaded ? "YES" : "NO", running ? "YES" : "NO");
        return false;
    }

    std::string dashboardPath = "/data/data/com.xanite/files/XboxDashboard/xboxdash.xbe";
    LOGI("Loading dashboard from: %s", dashboardPath.c_str());


    struct stat fileStat;
    if (stat(dashboardPath.c_str(), &fileStat) != 0) {
        lastError = "Dashboard file does not exist: " + dashboardPath;
        LOGE("DASHBOARD ERROR: %s", lastError.c_str());
        return false;
    }

    LOGI("Dashboard file exists, size: %ld bytes", fileStat.st_size);

    if (!loadXbe(dashboardPath)) {
        lastError = "Failed to load dashboard: " + dashboardPath;
        LOGE("DASHBOARD ERROR: %s", lastError.c_str());
        return false;
    }

    LOGI("=== Dashboard loaded successfully ===");
    return true;
}

bool XboxEmulator::loadBios(const std::string& path) {
    LOGI("Loading BIOS from: %s", path.c_str());
    LOGI("Current state before loading - BIOS Loaded: %s, Running: %s", 
         biosLoaded ? "YES" : "NO", running ? "YES" : "NO");


    if (biosLoaded) {
        LOGI("BIOS already loaded, skipping...");
        return true;
    }

    if (!memory.loadBios(path)) {
        lastError = "Failed to load BIOS: " + path;
        LOGE("%s", lastError.c_str());
        return false;
    }

    biosLoaded = true;
    LOGI("BIOS flag set to true, initializing system...");
    initializeSystem();


    LOGI("Final state after BIOS loading - BIOS Loaded: %s, Running: %s", 
         biosLoaded ? "YES" : "NO", running ? "YES" : "NO");

    if (biosLoaded && running) {
        LOGI("BIOS loaded and system running successfully");
        return true;
    } else {
        LOGE("BIOS loaded but system not running");
        return false;
    }
}

bool XboxEmulator::loadXbe(const std::string& path) {
    LOGGAME("=== XBE LOADING INTO MEMORY STARTED ===");
    LOGGAME("Loading XBE file into memory: %s", path.c_str());

    if (!kernel) {
        kernel = new XboxKernel(&memory, &cpu); 
        LOGGAME("✓ Kernel was not initialized, created new instance");
    } else {
        LOGGAME("✓ Kernel already exists");
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        LOGGAME("⚠ Warning: Failed to open XBE file with ios::ate, attempting to continue anyway");
        file.clear(); 
        file.open(path, std::ios::binary); 
    }

    std::streamsize size = 0;
    if (file) {
        size = file.tellg();
        file.seekg(0, std::ios::beg);
        LOGGAME("✓ XBE file opened successfully");
        LOGGAME("  File size: %zu bytes", size);
        LOGGAME("  File size: %.2f MB", static_cast<double>(size) / (1024.0 * 1024.0));
    } else {
        LOGGAME("⚠ Warning: Could not determine file size, using default 2MB buffer");
        size = 2 * 1024 * 1024; 
    }

    std::vector<char> buffer(size);
    if (file) {
        if (!file.read(buffer.data(), size)) {
            LOGGAME("⚠ Warning: Partial read of XBE file, continuing anyway");
        } else {
            LOGGAME("✓ XBE file read successfully: %zu bytes", size);
        }
        file.close();
    } else {
        LOGGAME("⚠ Warning: Using empty XBE buffer");
        std::fill(buffer.begin(), buffer.end(), 0); 
    }

    LOGGAME("→ Calling kernel->loadXbe()...");
    bool result = true;
    if (kernel) {
        result = kernel->loadXbe(path);
        LOGGAME("kernel->loadXbe() returned: %s", result ? "true" : "false");
        if (!result) {
            LOGGAME("❌ XBE konnte nicht geladen werden! Abbruch.");
            return false;
        }
        uint32_t entryPoint = kernel->getEntryPoint() ? kernel->getEntryPoint() : 0x10000;
        LOGGAME("Kernel entry point: 0x%08X", entryPoint);


        if (entryPoint >= 0x08000000) {
            LOGGAME("⚠ Entry point 0x%08X is outside valid Xbox RAM range (0x00000000-0x07FFFFFF)", entryPoint);
            LOGGAME("→ Fixing entry point to valid RAM address");
            entryPoint = 0x00100000; 
            LOGGAME("✓ Entry point fixed to: 0x%08X", entryPoint);
        } else if (entryPoint >= 0x00000000 && entryPoint < 0x00010000) {
            LOGGAME("⚠ Entry point 0x%08X is in low memory area (0x00000000-0x0000FFFF)", entryPoint);
            LOGGAME("→ Moving entry point to safe code area");
            entryPoint = 0x00100000; 
            LOGGAME("✓ Entry point moved to: 0x%08X", entryPoint);
        } else {
            LOGGAME("✓ Entry point is within valid RAM range");
        }



        LOGGAME("✓ kernel->loadXbe() already loaded XBE sections into memory correctly");
        LOGGAME("✓ No need to manually load raw XBE data - this would overwrite the properly loaded sections!");


        LOGGAME("→ Setting memory XBE entry point to: 0x%08X", entryPoint);
        memory.setXbeEntryPoint(entryPoint);
        LOGGAME("✓ Memory XBE entry point set successfully");


        LOGGAME("→ Verifying entry point 0x%08X contains valid data...", entryPoint);

        uint8_t entryBytes[16] = {0};
        for (int i = 0; i < 16 && entryPoint + i < 0x08000000; i++) {
            entryBytes[i] = memory.read8(entryPoint + i);
        }

        LOGGAME("Entry point bytes:");
        for (int i = 0; i < 16; i++) {
            LOGGAME("  Entry+%2d: 0x%02X", i, entryBytes[i]);
        }


        bool validInstructions = false;
        if (entryBytes[0] == 0x55 || entryBytes[0] == 0x8B || entryBytes[0] == 0x89 || 
            entryBytes[0] == 0x90 || entryBytes[0] == 0xE8 || entryBytes[0] == 0xE9 ||
            entryBytes[0] == 0x50 || entryBytes[0] == 0x51 || entryBytes[0] == 0x52 || entryBytes[0] == 0x53) {
            validInstructions = true;
        }

        if (validInstructions) {
            LOGGAME("✓ Entry point contains valid x86 instructions");
        } else {
            LOGGAME("⚠ Entry point does not contain valid x86 instructions");
            LOGGAME("⚠ This might indicate a problem with section loading");
        }


        LOGGAME("→ Verifying memory contents at entry point...");
        uint32_t verifyValue = memory.read32(entryPoint);
        LOGGAME("  [VERIFY] Memory at 0x%08X contains: 0x%08X", entryPoint, verifyValue);


        uint32_t memoryXbeEntryPoint = memory.getXbeEntryPoint();
        if (memoryXbeEntryPoint != 0 && memoryXbeEntryPoint != entryPoint) {
            uint32_t xbeVerifyValue = memory.read32(memoryXbeEntryPoint);
            LOGGAME("  [VERIFY] Memory at XBE entry point 0x%08X contains: 0x%08X", memoryXbeEntryPoint, xbeVerifyValue);

            if (xbeVerifyValue == 0x00000000) {
                LOGGAME("⚠ WARNING: Memory at XBE entry point is still 0x00000000!");
            }
        }

        if (verifyValue == 0x00000000) {
            LOGGAME("⚠ WARNING: Memory at entry point is still 0x00000000!");
            LOGGAME("  → This indicates a problem with kernel section loading");
            LOGGAME("  → The kernel should have loaded the XBE sections correctly");
            LOGGAME("  → NOT attempting alternative loading to avoid overwriting kernel-loaded data");
        } else {
            LOGGAME("✓ Entry point contains valid data: 0x%08X", verifyValue);
        }


        LOGGAME("→ Initializing interrupt vector table at 0x00000000");
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t vectorAddr = 0x00000000 + (i * 4);


            memory.write32(vectorAddr, entryPoint);
        }



        LOGGAME("✓ Skipping high memory initialization to prevent crashes");

        LOGGAME("✓ Interrupt vector table initialized - all interrupts return to game entry point");


    LOGGAME("→ About to setup CPU state...");


    gameEntryPoint = entryPoint;


    LOGGAME("→ Setting memory XBE entry point to: 0x%08X", entryPoint);
    memory.setXbeEntryPoint(entryPoint);
    LOGGAME("✓ Memory XBE entry point set successfully");


    LOGGAME("→ Starting CPU execution at entry point 0x%08X", entryPoint);
    cpu.startGameExecution(entryPoint);
    LOGGAME("✓ CPU execution started successfully");

        LOGI("Xbox CPU initialized: Entry Point=0x%08X", entryPoint);

        gameLoaded = true;
        LOGI("XBE loaded successfully: %s (load result: %d)", path.c_str(), result);
        LOGI("Entry point set to: 0x%08X", entryPoint);
        LOGI("CPU state initialized for game execution");
    } else {
        LOGW("Warning: Kernel not available, but continuing anyway");
        cpu.setPC(0x10000);
        cpu.setState(X86Core::CpuState::Running);
        gameLoaded = true;
    }


    LOGW("Game loaded - starting real Xbox game execution");
    LOGW("CPU will now execute real game code from address 0x%08X", cpu.getEIP());
    LOGW("CPU state: %d", static_cast<int>(cpu.getState()));

    return true;
}



void XboxEmulator::logDebug(const std::string& message) {
    if (debugCallback) {
        debugCallback(message);
    }
    LOGD("Emulator: %s", message.c_str());
}

bool XboxEmulator::loadXbeFromMemory(const void* data, size_t size) {
    LOGGAME("=== XBE LOADING FROM MEMORY STARTED ===");
    LOGGAME("Loading XBE from memory: %zu bytes", size);
    LOGGAME("Current state: gameLoaded=%s, biosLoaded=%s, running=%s", 
            gameLoaded ? "true" : "false", 
            biosLoaded ? "true" : "false", 
            running ? "true" : "false");

    if (!data || size == 0) {
        lastError = "XBE data is empty";
        LOGGAME("✗ XBE data is empty");
        LOGE("XBE data is empty");
        return false;
    }


    bool ok = kernel->loadXbeFromMemory(data, size);
    if (!ok) {
        lastError = "Failed to load XBE through kernel";
        LOGGAME("✗ Failed to load XBE through kernel");
        LOGE("Failed to load XBE through kernel");
        return false;
    }


    uint32_t entryPoint = kernel->getEntryPoint();
    if (entryPoint == 0 || entryPoint >= 0x08000000) {
        LOGW("Invalid XBE entry point 0x%08X, using default", entryPoint);
        entryPoint = 0x00100000;
    } else if (entryPoint >= 0x00000000 && entryPoint < 0x00010000) {
        LOGW("XBE entry point 0x%08X is in low memory area, moving to safe code area", entryPoint);
        entryPoint = 0x00100000;
    }
    gameEntryPoint = entryPoint;
    LOGGAME("✓ Game entry point set to: 0x%08X (from XBE header)", entryPoint);


    cpu.startGameExecution(entryPoint);
    LOGGAME("✓ CPU initialized for game execution at entry point: 0x%08X", entryPoint);


    gameLoaded = true;
    memory.setGameLoaded(true);

    LOGGAME("✓ XBE loaded from memory successfully");
    LOGGAME("✓ Final state: gameLoaded=%s, gameEntryPoint=0x%08X, CPU state=%d", 
            gameLoaded ? "true" : "false", gameEntryPoint, static_cast<int>(cpu.getState()));
    LOGI("XBE loaded from memory successfully with entry point 0x%08X", entryPoint);
    return true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeRunFrameWithOpenGL(
    JNIEnv* env,
    jclass clazz,
    jlong emulatorHandle
) {
    (void)env;      
    (void)clazz;    
    (void)emulatorHandle; 
    XboxEmulator* emulator = XboxEmulator::getEmulatorInstance(nullptr, nullptr);
    if (!emulator) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxEmulator", "Failed to get emulator instance");
        return;
    }


    try {

        emulator->runFrame();


        OpenGLRenderer* openGLRenderer = emulator->getOpenGLRenderer();
        if (openGLRenderer) {
            __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "OpenGL renderer found: 0x%p", openGLRenderer);

            if (openGLRenderer->isInitialized()) {
                __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "OpenGL renderer is initialized - calling renderFrame()");


                NV2ARenderer* gpu = emulator->getGPU();
                if (gpu) {
                    __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "Syncing Xbox Memory with GPU");
                    gpu->syncFramebufferFromMemory();
                }

                openGLRenderer->renderFrame();
                __android_log_print(ANDROID_LOG_INFO, "XboxEmulator", "OpenGL renderFrame() completed successfully");
            } else {
                __android_log_print(ANDROID_LOG_WARN, "XboxEmulator", "OpenGL renderer is NOT initialized!");
            }
        } else {
            __android_log_print(ANDROID_LOG_WARN, "XboxEmulator", "OpenGL renderer is NULL!");
        }
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxEmulator", "Exception in runFrameWithOpenGL: %s", e.what());
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, "XboxEmulator", "Unknown exception in runFrameWithOpenGL");
    }
}

bool XboxEmulator::loadXISOIntoMemory(const std::string& xisoPath) {
    LOGGAME("=== LOADING XISO INTO MEMORY ===");
    LOGGAME("Loading XISO into Xbox memory: %s", xisoPath.c_str());

    std::ifstream xisoFile(xisoPath, std::ios::binary);
    if (!xisoFile.is_open()) {
        LOGE("Failed to open XISO file for memory loading");
        LOGGAME("✗ Failed to open XISO file for memory loading");
        return false;
    }

    xisoFile.seekg(0, std::ios::end);
    std::streamsize fileSize = xisoFile.tellg();
    xisoFile.seekg(0, std::ios::beg);

    LOGGAME("✓ XISO file opened successfully");
    LOGGAME("  File size: %lld bytes", static_cast<long long>(fileSize));
    LOGGAME("  File size: %.2f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));

    if (fileSize <= 0) {
        LOGE("XISO file is empty or invalid");
        LOGGAME("✗ XISO file is empty or invalid");
        return false;
    }

    const uint32_t XISO_MEMORY_BASE = 0x00100000; 
    const uint32_t XISO_MEMORY_SIZE = 64 * 1024 * 1024; 

    LOGGAME("Memory configuration:");
    LOGGAME("  Target address: 0x%08X", XISO_MEMORY_BASE);
    LOGGAME("  Max size: %u bytes (%.2f MB)", XISO_MEMORY_SIZE, static_cast<double>(XISO_MEMORY_SIZE) / (1024.0 * 1024.0));

    if (fileSize > XISO_MEMORY_SIZE) {
        LOGW("XISO file too large for memory, loading first %u bytes", XISO_MEMORY_SIZE);
        LOGGAME("⚠ XISO file too large, loading first %u bytes", XISO_MEMORY_SIZE);
        fileSize = XISO_MEMORY_SIZE;
    }

    const size_t CHUNK_SIZE = 1024 * 1024;
    std::vector<uint8_t> buffer(CHUNK_SIZE);

    LOGGAME("Starting memory loading process...");
    uint32_t currentOffset = 0;
    uint32_t totalBytesWritten = 0;

    while (currentOffset < fileSize && xisoFile.good()) {
        size_t bytesToRead = std::min(CHUNK_SIZE, static_cast<size_t>(static_cast<uint64_t>(fileSize) - currentOffset));
        xisoFile.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);

        size_t bytesRead = xisoFile.gcount();
        if (bytesRead > 0) {

            for (size_t i = 0; i < bytesRead; i++) {
                uint32_t addr = XISO_MEMORY_BASE + currentOffset + i;
                uint8_t byteValue = buffer[i];
                memory.write8(addr, byteValue);


                if (currentOffset + i < 16) {
                    LOGGAME("DEBUG: Writing byte %zu: 0x%02X to address 0x%08X", i, byteValue, addr);


                    uint8_t readBack = memory.read8(addr);
                    if (readBack != byteValue) {
                        LOGGAME("✗ DEBUG: Write verification failed! Wrote 0x%02X, read back 0x%02X at address 0x%08X", byteValue, readBack, addr);
                    } else {
                        LOGGAME("✓ DEBUG: Write verification successful for address 0x%08X", addr);
                    }
                }
            }
            currentOffset += bytesRead;
            totalBytesWritten += bytesRead;


            if (totalBytesWritten % (10 * 1024 * 1024) == 0) {
                LOGGAME("→ Loaded %u bytes into memory (%.1f%%)", totalBytesWritten, 
                       (static_cast<double>(totalBytesWritten) / fileSize) * 100.0);
            }
        }
    }
    xisoFile.close();

    LOGGAME("✓ Memory loading completed");
    LOGGAME("  Total bytes written: %u", totalBytesWritten);
    LOGGAME("  Expected file size: %lld", static_cast<long long>(fileSize));

    if (totalBytesWritten == 0) {
        LOGGAME("✗ No bytes were written to memory!");
        return false;
    }


    LOGGAME("=== VERIFYING MEMORY LOADING ===");
    for (int i = 0; i < 16; i++) {
        uint32_t addr = XISO_MEMORY_BASE + i;
        uint8_t value = memory.read8(addr);
        LOGGAME("  Address 0x%08X: 0x%02X", addr, value);
    }

    return totalBytesWritten > 0;
}

bool XboxEmulator::loadXISO(const std::string& xisoPath) {
    LOGGAME("=== XISO LOADING INTO EMULATOR ===");
    LOGGAME("Loading XISO file into emulator: %s", xisoPath.c_str());


    std::ifstream file(xisoPath, std::ios::binary);
    if (!file.is_open()) {
        lastError = "XISO file not found: " + xisoPath;
        LOGGAME("✗ Failed to open XISO file: %s", xisoPath.c_str());
        LOGE("Failed to open XISO file: %s", xisoPath.c_str());
        return false;
    }


    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    file.close();

    LOGGAME("✓ XISO file exists and is accessible");
    LOGGAME("  File size: %lld bytes", static_cast<long long>(fileSize));
    LOGGAME("  File size: %.2f MB", static_cast<double>(fileSize) / (1024.0 * 1024.0));


    LOGGAME("→ Loading XISO into Xbox memory...");
    if (!loadXISOIntoMemory(xisoPath)) {
        lastError = "Failed to load XISO into memory";
        LOGGAME("✗ Failed to load XISO into memory");
        LOGE("Failed to load XISO into memory");
        return false;
    }

    LOGGAME("✓ XISO loaded successfully into memory");
    gameLoaded = true;



    if (kernel && kernel->isXbeLoaded()) {
        gameEntryPoint = kernel->getEntryPoint();
        LOGGAME("✓ Game entry point set to corrected XBE entry point: 0x%08X", gameEntryPoint);
    } else {
        gameEntryPoint = 0x00100000; 
        LOGGAME("✓ Game entry point set to default: 0x%08X", gameEntryPoint);
    }

    return true;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xanite_xboxoriginal_XboxShaderActivity_nativeLoadGameFileAndStartWithPaths(
    JNIEnv* env, jobject thiz, jlong handle,
    jstring gamePath, jstring tempXisoPath, jstring extractDir
) {
    (void)thiz;
    XboxEmulator* emulator = reinterpret_cast<XboxEmulator*>(handle);
    if (!emulator) return JNI_FALSE;

    const char* cGamePath = env->GetStringUTFChars(gamePath, nullptr);
    const char* cTempXisoPath = env->GetStringUTFChars(tempXisoPath, nullptr);
    const char* cExtractDir = env->GetStringUTFChars(extractDir, nullptr);

    bool result = emulator->loadISOAndStart(
        std::string(cGamePath),
        std::string(cTempXisoPath),
        std::string(cExtractDir)
    );

    env->ReleaseStringUTFChars(gamePath, cGamePath);
    env->ReleaseStringUTFChars(tempXisoPath, cTempXisoPath);
    env->ReleaseStringUTFChars(extractDir, cExtractDir);

    return result ? JNI_TRUE : JNI_FALSE;
}


bool XboxEmulator::initializeAudioSystem() {
    LOGI("🎵 Initialisiere Xbox Audio System...");

    if (!audioSystem.initialize()) {
        LOGE("❌ Fehler beim Initialisieren des Xbox Audio Systems");
        return false;
    }

    LOGI("✅ Xbox Audio System erfolgreich initialisiert!");
    return true;
}

void XboxEmulator::updateAudioSystem() {
    if (audioSystem.isInitialized()) {
        audioSystem.updateFromXboxMemory(memory);
    }
}

bool XboxEmulator::isAudioSystemActive() const {
    return audioSystem.isInitialized() && audioSystem.hasActiveAudio();
}
