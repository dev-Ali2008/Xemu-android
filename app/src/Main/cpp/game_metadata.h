#ifndef XANITE_GAME_METADATA_H
#define XANITE_GAME_METADATA_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace xanite {

enum class GameFileType {
    UNKNOWN,
    XEX,
    ISO,
    ZAR,
    CON,
    LIVE,
    PIRS
};

enum class GameRegion {
    UNKNOWN,
    NTSC_U,    // North America
    NTSC_J,    // Japan  
    PAL,       // Europe/Australia
    NTSC_C,    // China
    REGION_FREE
};

struct XexHeader {
    uint32_t magic;
    uint32_t module_flags;
    uint32_t data_offset;
    uint32_t reserved;
    uint32_t security_info;
    uint32_t header_count;
    uint32_t entry_point;
    uint32_t optional_header_count;
};

struct XexExecutionInfo {
    uint32_t media_id;
    uint32_t version;
    uint32_t base_version;
    uint32_t title_id;
    uint32_t platform;
    uint32_t executable_table;
    uint32_t disc_number;
    uint32_t disc_count;
    uint32_t savegame_id;
};

class GameMetadata {
public:
    explicit GameMetadata(const std::string& game_path);
    ~GameMetadata();

    // Load metadata from game file
    bool LoadMetadata();
    bool ReloadMetadata();

    // Basic getters
    std::string GetTitleName() const;
    uint32_t GetTitleId() const;
    GameRegion GetRegion() const;
    GameFileType GetFileType() const;
    std::string GetGamePath() const;
    std::string GetFileName() const;
    uint64_t GetFileSize() const;
    std::string GetVersionString() const;
    uint32_t GetDiscNumber() const;
    uint32_t GetDiscCount() const;
    bool IsValid() const;
    bool IsXbox360Title() const;
    bool IsDemo() const;

    // Formatted information
    std::string GetFormattedTitleId() const;
    std::string GetRegionString() const;
    std::string GetFileTypeString() const;
    std::string GetFormattedFileSize() const;

    // Save data management
    std::string GetSavePath(const std::string& content_root) const;
    bool HasSaveData() const;

    // Static utility methods
    static std::vector<GameMetadata> ScanDirectory(const std::string& directory_path);
    static bool IsSupportedFileType(const std::string& file_path);
    static GameFileType DetectFileType(const std::string& file_path);
    static std::string GetFileTypeExtension(GameFileType file_type);
    static std::vector<std::string> GetSupportedExtensions();
    static bool ValidateGameFile(const std::string& file_path);

private:
    bool LoadXexMetadata();
    bool LoadIsoMetadata();
    bool LoadZarMetadata();
    bool LoadConMetadata();
    bool LoadLiveMetadata();
    bool LoadPirsMetadata();
    bool AutoDetectAndLoadMetadata();
    
    // Helper methods
    bool ParseXexHeaders(std::ifstream& file);
    bool ParseIsoHeaders(std::ifstream& file);
    bool ReadXexOptionalHeader(std::ifstream& file, uint32_t key, uint32_t value);
    bool ExtractTitleNameFromPath();
    bool DetectRegionFromTitleId(uint32_t title_id);
    
    // File parsing utilities
    uint32_t ReadUint32(std::ifstream& file);
    uint64_t ReadUint64(std::ifstream& file);
    std::string ReadString(std::ifstream& file, size_t length);
    bool SeekToOffset(std::ifstream& file, uint64_t offset);

    // Member variables
    std::string game_path_;
    std::string title_name_;
    uint32_t title_id_ = 0;
    uint32_t media_id_ = 0;
    uint32_t version_ = 0;
    uint32_t base_version_ = 0;
    uint32_t disc_number_ = 1;
    uint32_t disc_count_ = 1;
    uint64_t file_size_ = 0;
    
    GameRegion region_ = GameRegion::UNKNOWN;
    GameFileType file_type_ = GameFileType::UNKNOWN;
    
    bool is_valid_ = false;
    bool is_demo_ = false;
    bool has_save_data_ = false;
};

} // namespace xanite

#endif // XANITE_GAME_METADATA_H