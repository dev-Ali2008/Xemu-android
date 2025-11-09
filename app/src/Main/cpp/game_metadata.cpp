#include "game_metadata.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xanite {

GameMetadata::GameMetadata(const std::string& game_path) 
    : game_path_(game_path), is_valid_(false) {
    LoadMetadata();
}

GameMetadata::~GameMetadata() = default;

bool GameMetadata::LoadMetadata() {
    if (game_path_.empty()) {
        return false;
    }
   
    auto extension = xe::utf8::to_lower_case(xe::filesystem::GetFileExtension(game_path_));
    
    if (extension == "xex" || extension == "elf") {
        return LoadXexMetadata();
    } else if (extension == "iso" || extension == "xiso") {
        return LoadIsoMetadata();
    } else if (extension == "zar") {
        return LoadZarMetadata();
    } else {
        
        return AutoDetectAndLoadMetadata();
    }
}

bool GameMetadata::LoadXexMetadata() {
    
    std::ifstream file(game_path_, std::ios::binary);
    if (!file.is_open()) {
        XELOGE("Failed to open XEX file: {}", game_path_);
        return false;
    }
   
    uint32_t signature;
    file.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    
    if (signature != 0x5855582F && signature != 0x5845582F) { 
        XELOGE("Invalid XEX signature: {:08X}", signature);
        return false;
    }

    XexHeader header;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (header.magic != 0x5845582F && header.magic != 0x5855582F) {
        XELOGE("Invalid XEX magic: {:08X}", header.magic);
        return false;
    }

    title_id_ = header.entry_point; 
        
    file.seekg(header.optional_header_count * 4, std::ios::cur);
    
    for (uint32_t i = 0; i < header.optional_header_count; ++i) {
        uint32_t key, value;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        
        if (key == 0x40006) { 
            file.seekg(value - (header.optional_header_count * 8) - sizeof(header), std::ios::cur);
            
            XexExecutionInfo exec_info;
            file.read(reinterpret_cast<char*>(&exec_info), sizeof(exec_info));
            
            title_id_ = exec_info.title_id;
            break;
        }
    }
  
    title_name_ = xe::utf8::to_upper_case(xe::filesystem::GetFileBaseName(game_path_));
    region_ = GameRegion::NTSC_U; 
    
    is_valid_ = true;
    file_type_ = GameFileType::XEX;
    
    XELOGI("Loaded XEX metadata - Title: {}, Title ID: {:08X}", title_name_, title_id_);
    return true;
}

bool GameMetadata::LoadIsoMetadata() {
    
    std::ifstream file(game_path_, std::ios::binary);
    if (!file.is_open()) {
        XELOGE("Failed to open ISO file: {}", game_path_);
        return false;
    }

    
    file.seekg(0x10000);
    
    char magic[20];
    file.read(magic, 20);
    
    if (std::memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
        XELOGE("Invalid XBOX 360 ISO magic");
        return false;
    }
    
    file.seekg(0x8020);
    uint32_t root_dir_sector;
    file.read(reinterpret_cast<char*>(&root_dir_sector), sizeof(root_dir_sector));   
    
    title_name_ = xe::utf8::to_upper_case(xe::filesystem::GetFileBaseName(game_path_));
    title_id_ = 0; 
    region_ = GameRegion::NTSC_U; 
    
    is_valid_ = true;
    file_type_ = GameFileType::ISO;
    
    XELOGI("Loaded ISO metadata - Title: {}", title_name_);
    return true;
}

bool GameMetadata::LoadZarMetadata() {
    
    title_name_ = xe::utf8::to_upper_case(xe::filesystem::GetFileBaseName(game_path_));
    title_id_ = 0;
    region_ = GameRegion::NTSC_U;
    
    is_valid_ = true;
    file_type_ = GameFileType::ZAR;
    
    XELOGI("Loaded ZAR metadata - Title: {}", title_name_);
    return true;
}

bool GameMetadata::AutoDetectAndLoadMetadata() {
    std::ifstream file(game_path_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    char magic[8];
    file.read(magic, 8);
    file.seekg(0);
    
    if (std::memcmp(magic, "XEX", 3) == 0 || std::memcmp(magic, "XEX", 3) == 0) {
        return LoadXexMetadata();
    }
    
    else if (std::memcmp(magic + 0x8000, "\x01CD001\x01", 7) == 0) {
        return LoadIsoMetadata();
    }
    
    else {
        file.seekg(-4, std::ios::end);
        char zar_magic[4];
        file.read(zar_magic, 4);
        
        if (std::memcmp(zar_magic, "ZAR", 3) == 0) {
            return LoadZarMetadata();
        }
    }

    XELOGE("Could not auto-detect file type for: {}", game_path_);
    return false;
}

std::string GameMetadata::GetTitleName() const {
    return title_name_;
}

uint32_t GameMetadata::GetTitleId() const {
    return title_id_;
}

GameRegion GameMetadata::GetRegion() const {
    return region_;
}

GameFileType GameMetadata::GetFileType() const {
    return file_type_;
}

std::string GameMetadata::GetGamePath() const {
    return game_path_;
}

bool GameMetadata::IsValid() const {
    return is_valid_;
}

std::string GameMetadata::GetFormattedTitleId() const {
    return fmt::format("{:08X}", title_id_);
}

std::string GameMetadata::GetRegionString() const {
    switch (region_) {
        case GameRegion::NTSC_U: return "NTSC-U";
        case GameRegion::NTSC_J: return "NTSC-J";
        case GameRegion::PAL: return "PAL";
        default: return "Unknown";
    }
}

std::string GameMetadata::GetFileTypeString() const {
    switch (file_type_) {
        case GameFileType::XEX: return "XEX";
        case GameFileType::ISO: return "ISO";
        case GameFileType::ZAR: return "ZAR";
        case GameFileType::CON: return "CON";
        case GameFileType::LIVE: return "LIVE";
        case GameFileType::PIRS: return "PIRS";
        default: return "Unknown";
    }
}

std::vector<GameMetadata> GameMetadata::ScanDirectory(const std::string& directory_path) {
    std::vector<GameMetadata> games;
    
    auto files = xe::filesystem::ListFiles(directory_path);
    for (const auto& file : files) {
        auto extension = xe::utf8::to_lower_case(xe::filesystem::GetFileExtension(file));
        
        
        if (extension == "xex" || extension == "iso" || extension == "xiso" || 
            extension == "zar" || extension == "con" || extension == "live" || 
            extension == "pirs" || extension == "elf") {
            
            GameMetadata metadata(file);
            if (metadata.IsValid()) {
                games.push_back(std::move(metadata));
            }
        }
    }
    
    
    std::sort(games.begin(), games.end(), 
        [](const GameMetadata& a, const GameMetadata& b) {
            return a.GetTitleName() < b.GetTitleName();
        });
    
    return games;
}

bool GameMetadata::IsSupportedFileType(const std::string& file_path) {
    auto extension = xe::utf8::to_lower_case(xe::filesystem::GetFileExtension(file_path));
    
    static const std::vector<std::string> supported_extensions = {
        "xex", "iso", "xiso", "zar", "con", "live", "pirs", "elf"
    };
    
    return std::find(supported_extensions.begin(), supported_extensions.end(), extension) != supported_extensions.end();
}

std::string GameMetadata::GetSavePath(const std::string& content_root, uint32_t title_id) {
    return fmt::format("{}/{:08X}", content_root, title_id);
}

} 
