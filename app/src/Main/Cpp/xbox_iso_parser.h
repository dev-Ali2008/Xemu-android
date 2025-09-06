#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <cctype>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <optional>

struct XboxFileEntry {
    std::string name;
    uint32_t sector;
    uint32_t size;
    bool isDirectory;
    bool isCompressed;
    bool isHidden;
    std::string fullPath;
    std::vector<XboxFileEntry> children;
};

class XboxISOParser {
public:
    enum class PartitionType {
        STANDARD,
        XISO,
        HIDDEN,
        UNKNOWN
    };

    XboxISOParser();
    XboxISOParser(XboxISOParser&& other) noexcept;
    ~XboxISOParser();

    XboxISOParser& operator=(XboxISOParser&& other) noexcept;


    XboxISOParser(const XboxISOParser&) = delete;
    XboxISOParser& operator=(const XboxISOParser&) = delete;

    bool loadISO(const std::string& isoPath, std::string& errorMsg);
    bool loadISO(const uint8_t* data, size_t size, std::string& errorMsg);
    bool parse(const uint8_t* data, size_t size, std::string& errorMsg);
    std::vector<uint8_t> getXbeData();
    void setDebugOutput(bool enabled);

    bool findMainXBE(std::string& xbePath, uint32_t& xbeSize, uint32_t& xbeSector, std::string& errorMsg);
    bool findAnyXBE(std::string& xbeName, uint32_t& xbeSize, uint32_t& xbeSector, std::string& errorMsg);


    struct XBEInfo {
        std::string name;
        uint32_t absoluteOffset;
        uint32_t sector;
        uint32_t size;
        uint32_t entryPoint;
        uint32_t imageBase;
        uint32_t numSections;
    };

    std::optional<XBEInfo> ultraBruteForceXBEScan();
    bool findXBEByScanning(std::string& xbeName, uint32_t& xbeSize, uint32_t& xbeSector, std::string& errorMsg);
    bool loadXISOAsGame(std::string& gameName, uint32_t& gameSize, uint32_t& gameSector, std::string& errorMsg);
    bool loadXISOAsCompleteGame(std::string& gameName, uint32_t& gameSize, uint32_t& gameSector, std::string& errorMsg);
    bool getGameDataOffset(uint32_t& offset);
    bool checkXISOIntegrity(std::string& errorMsg);
    bool extractAllHiddenFiles(const std::string& isoPath, const std::string& extractDir, std::string& errorMsg);
    std::vector<std::string> listAllXBEFiles();
    const XboxFileEntry* findEntry(const std::string& path) const;
    bool fileExists(const std::string& path) const;
    const XboxFileEntry* getRootEntry() const;

    std::vector<uint8_t> readFileData(uint32_t sector, uint32_t size, bool decompress = false);

    bool extractFile(const std::string& isoPath, const std::string& outputPath, std::string& errorMsg);
    bool extractAllFiles(const std::string& outputDir, std::function<void(const std::string&, bool)> progressCallback = nullptr);

    static std::string sanitizeGameName(const std::string& name);
    static std::string getAndroidGamePath(const std::string& gameName);
    static bool setupGameDirectory(const std::string& gameName);
    bool convertISOToXBE(const std::string& isoPath, const std::string& outputDir);
    bool processISO(const std::string& isoPath);


    std::vector<uint8_t> readFile(const std::string& path);
    std::optional<uint32_t> scanForXbeMagic(const std::string& isoPath);
    std::optional<uint32_t> scanForXbeMagicAnywhere(const std::string& isoPath);
    std::optional<uint32_t> scanForXbeMagicAnywhereTolerant(const std::string& isoPath);
    std::optional<std::pair<uint32_t, uint32_t>> findDefaultXbeInRoot(const std::string& isoPath);


    std::pair<uint32_t, uint32_t> tryXaniteExtendedSearch(const std::string& isoPath, uint32_t failedSector);


    std::pair<uint32_t, uint32_t> tryXaniteIntelligentBypass(const std::string& isoPath);

    static constexpr const char* ALT_XBE_NAMES[] = {
        "default.xbe", "main.xbe", "game.xbe", "dash.xbe", "xboxdash.xbe", "update.xbe", "default.bin", "default.dat"
    };

private:

    static uint16_t readU16(const uint8_t* data);
    static uint32_t readU32(const uint8_t* data);
    static uint64_t readU64(const uint8_t* data);
    static std::string toUppercase(const std::string& str);

    PartitionType detectPartitionType();
    bool parseXDVDFS(uint32_t& rootSector, uint32_t& rootSize, std::string& errorMsg);
    bool parseXISOFormat(uint32_t& rootSector, uint32_t& rootSize, std::string& errorMsg);
    bool parseDirectory(uint32_t sector, uint32_t size, XboxFileEntry& parent, 
                       const std::string& parentPath, std::string& errorMsg);
    bool parseDirectoryAlternative(uint32_t sector, uint32_t size, XboxFileEntry& parent, 
                                  const std::string& parentPath, std::string& errorMsg);
    void buildPathCache(XboxFileEntry& entry);
    void parseXdvdfsDirectoryTree(uint32_t sector, uint32_t offsetInSector, XboxFileEntry& parent, const std::string& parentPath);

    std::vector<uint8_t> decryptXboxFile(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> decompressXISO(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> decompressXBC(const std::vector<uint8_t>& data) const;
    bool isXboxCompressed(const uint8_t* data, size_t size) const;


    std::vector<uint8_t> decompressZIP(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> decompressRAR(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> decompress7Z(const std::vector<uint8_t>& data) const;
    std::vector<uint8_t> decompressGZIP(const std::vector<uint8_t>& data) const;
    bool isZIPCompressed(const uint8_t* data, size_t size) const;
    bool isRARCompressed(const uint8_t* data, size_t size) const;
    bool is7ZCompressed(const uint8_t* data, size_t size) const;
    bool isGZIPCompressed(const uint8_t* data, size_t size) const;

    std::ifstream isoFile;
    std::vector<uint8_t> iso_data;
    std::vector<uint8_t> xbe_data;
    XboxFileEntry rootEntry;
    std::unordered_map<std::string, XboxFileEntry*> pathCache;
    uint32_t sectorSize;
    uint32_t xisoOffset;
    uint32_t gameDataOffset;
    bool debugOutput;
    std::string isoFilePath;

    std::vector<uint8_t> readFileDataByOffset(uint64_t absOffset, uint32_t size);


    bool strictMode;
};
