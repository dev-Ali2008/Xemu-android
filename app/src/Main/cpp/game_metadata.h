// ax360e 
#ifndef XANITE_GAME_METADATA_H
#define XANITE_GAME_METADATA_H

#include <cstdint>
#include <string>
#include <vector>

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
    NTSC_U,
    NTSC_J,
    PAL
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
    
    bool LoadMetadata();
  
    std::string GetTitleName() const;
    uint32_t GetTitleId() const;
    GameRegion GetRegion() const;
    GameFileType GetFileType() const;
    std::string GetGamePath() const;
    bool IsValid() const;

    
    std::string GetFormattedTitleId() const;
    std::string GetRegionString() const;
    std::string GetFileTypeString() const;

    
    static std::vector<GameMetadata> ScanDirectory(const std::string& directory_path);
    static bool IsSupportedFileType(const std::string& file_path);
    static std::string GetSavePath(const std::string& content_root, uint32_t title_id);

private:
    bool LoadXexMetadata();
    bool LoadIsoMetadata();
    bool LoadZarMetadata();
    bool AutoDetectAndLoadMetadata();

    std::string game_path_;
    std::string title_name_;
    uint32_t title_id_ = 0;
    GameRegion region_ = GameRegion::UNKNOWN;
    GameFileType file_type_ = GameFileType::UNKNOWN;
    bool is_valid_ = false;
};

} 

#endif 
