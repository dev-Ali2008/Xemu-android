#include "game_metadata.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
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
        XELOGE("Empty game path provided");
        return false;
    }

    // Get file size
    struct stat file_stat;
    if (stat(game_path_.c_str(), &file_stat) == 0) {
        file_size_ = file_stat.st_size;
    }

    // Try to determine file type and extract metadata
    file_type_ = DetectFileType(game_path_);
    
    switch (file_type_) {
        case GameFileType::XEX:
            return LoadXexMetadata();
        case GameFileType::ISO:
            return LoadIsoMetadata();
        case GameFileType::ZAR:
            return LoadZarMetadata();
        case GameFileType::CON:
            return LoadConMetadata();
        case GameFileType::LIVE:
            return LoadLiveMetadata();
        case GameFileType::PIRS:
            return LoadPirsMetadata();
        default:
            return AutoDetectAndLoadMetadata();
    }
}

bool GameMetadata::ReloadMetadata() {
    is_valid_ = false;
    return LoadMetadata();
}

bool GameMetadata::LoadXexMetadata() {
    std::ifstream file(game_path_, std::ios::binary);
    if (!file.is_open()) {
        XELOGE("Failed to open XEX file: %s", game_path_.c_str());
        return false;
    }

    // Read XEX signature
    uint32_t signature;
    file.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    
    // Check for "XEX2" (0x58455832) or "XEX1" signatures
    if (signature != 0x58455832 && signature != 0x5855582F) {
        XELOGE("Invalid XEX signature: %08X", signature);
        return false;
    }

    // Read basic XEX header
    XexHeader header;
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (header.magic != 0x58455832 && header.magic != 0x5855582F) {
        XELOGE("Invalid XEX magic: %08X", header.magic);
        return false;
    }

    // Parse optional headers for metadata
    file.seekg(sizeof(header));
    
    for (uint32_t i = 0; i < header.optional_header_count; ++i) {
        uint32_t key, value;
        file.read(reinterpret_cast<char*>(&key), sizeof(key));
        file.read(reinterpret_cast<char*>(&value), sizeof(value));
        
        if (!ReadXexOptionalHeader(file, key, value)) {
            break;
        }
    }

    // Extract title name from filename if not found in headers
    if (title_name_.empty()) {
        ExtractTitleNameFromPath();
    }

    // Detect region from title ID
    DetectRegionFromTitleId(title_id_);

    is_valid_ = true;
    file_type_ = GameFileType::XEX;
    
    XELOGI("Loaded XEX metadata - Title: %s, Title ID: %08X, Region: %s", 
           title_name_.c_str(), title_id_, GetRegionString().c_str());
    return true;
}

bool GameMetadata::LoadIsoMetadata() {
    std::ifstream file(game_path_, std::ios::binary);
    if (!file.is_open()) {
        XELOGE("Failed to open ISO file: %s", game_path_.c_str());
        return false;
    }

    // Try to read XGD header (offset 0x10000 for XBOX 360 ISOs)
    file.seekg(0x10000);
    
    char magic[20];
    file.read(magic, 20);
    
    if (std::memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
        XELOGE("Invalid XBOX 360 ISO magic");
        return false;
    }

    // Extract title name from filename
    ExtractTitleNameFromPath();
    
    // For ISO files, title ID might be in default.xex which we'd need to extract
    // For now, we'll use a placeholder
    title_id_ = 0;
    region_ = GameRegion::NTSC_U; // Default
    
    is_valid_ = true;
    file_type_ = GameFileType::ISO;
    
    XELOGI("Loaded ISO metadata - Title: %s", title_name_.c_str());
    return true;
}

bool GameMetadata::LoadZarMetadata() {
    // ZAR files are archives - extract basic info
    ExtractTitleNameFromPath();
    
    title_id_ = 0;
    region_ = GameRegion::NTSC_U;
    
    is_valid_ = true;
    file_type_ = GameFileType::ZAR;
    
    XELOGI("Loaded ZAR metadata - Title: %s", title_name_.c_str());
    return true;
}

bool GameMetadata::LoadConMetadata() {
    // CON files (content packages)
    ExtractTitleNameFromPath();
    
    title_id_ = 0;
    region_ = GameRegion::NTSC_U;
    
    is_valid_ = true;
    file_type_ = GameFileType::CON;
    
    XELOGI("Loaded CON metadata - Title: %s", title_name_.c_str());
    return true;
}

bool GameMetadata::LoadLiveMetadata() {
    // LIVE files (Xbox Live content)
    ExtractTitleNameFromPath();
    
    title_id_ = 0;
    region_ = GameRegion::NTSC_U;
    
    is_valid_ = true;
    file_type_ = GameFileType::LIVE;
    
    XELOGI("Loaded LIVE metadata - Title: %s", title_name_.c_str());
    return true;
}

bool GameMetadata::LoadPirsMetadata() {
    // PIRS files (pre-release content)
    ExtractTitleNameFromPath();
    
    title_id_ = 0;
    region_ = GameRegion::NTSC_U;
    
    is_valid_ = true;
    file_type_ = GameFileType::PIRS;
    
    XELOGI("Loaded PIRS metadata - Title: %s", title_name_.c_str());
    return true;
}

bool GameMetadata::AutoDetectAndLoadMetadata() {
    std::ifstream file(game_path_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read first few bytes to detect file type
    char magic[8];
    file.read(magic, 8);
    file.seekg(0);

    // Check for XEX signatures
    if (std::memcmp(magic, "XEX", 3) == 0 || std::memcmp(magic, "XEX", 3) == 0) {
        return LoadXexMetadata();
    }
    // Check for ISO signatures
    else if (file.seekg(0x8000), file.read(magic, 8), 
             std::memcmp(magic + 1, "CD001", 5) == 0) {
        return LoadIsoMetadata();
    }
    // Check for other formats
    else {
        file.seekg(-4, std::ios::end);
        char format_magic[4];
        file.read(format_magic, 4);
        
        if (std::memcmp(format_magic, "ZAR", 3) == 0) {
            return LoadZarMetadata();
        }
        else if (std::memcmp(magic, "CON", 3) == 0) {
            return LoadConMetadata();
        }
        else if (std::memcmp(magic, "LIVE", 4) == 0) {
            return LoadLiveMetadata();
        }
        else if (std::memcmp(magic, "PIRS", 4) == 0) {
            return LoadPirsMetadata();
        }
    }

    XELOGE("Could not auto-detect file type for: %s", game_path_.c_str());
    return false;
}

bool GameMetadata::ReadXexOptionalHeader(std::ifstream& file, uint32_t key, uint32_t value) {
    switch (key) {
        case 0x00040006: { // Execution info
            auto current_pos = file.tellg();
            file.seekg(value);
            
            XexExecutionInfo exec_info;
            file.read(reinterpret_cast<char*>(&exec_info), sizeof(exec_info));
            
            title_id_ = exec_info.title_id;
            media_id_ = exec_info.media_id;
            version_ = exec_info.version;
            base_version_ = exec_info.base_version;
            disc_number_ = exec_info.disc_number;
            disc_count_ = exec_info.disc_count;
            
            file.seekg(current_pos);
            break;
        }
        case 0x00040002: { // Original name
            auto current_pos = file.tellg();
            file.seekg(value);
            
            char name_buffer[256];
            file.read(name_buffer, sizeof(name_buffer));
            title_name_ = std::string(name_buffer);
            
            file.seekg(current_pos);
            break;
        }
        // Add more optional headers as needed
    }
    
    return true;
}

bool GameMetadata::ExtractTitleNameFromPath() {
    size_t last_slash = game_path_.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        title_name_ = game_path_.substr(last_slash + 1);
        
        // Remove extension
        size_t last_dot = title_name_.find_last_of(".");
        if (last_dot != std::string::npos) {
            title_name_ = title_name_.substr(0, last_dot);
        }
        
        // Convert to readable format
        std::replace(title_name_.begin(), title_name_.end(), '_', ' ');
        std::replace(title_name_.begin(), title_name_.end(), '-', ' ');
        
        return true;
    }
    
    title_name_ = game_path_;
    return false;
}

bool GameMetadata::DetectRegionFromTitleId(uint32_t title_id) {
    if (title_id == 0) {
        region_ = GameRegion::UNKNOWN;
        return false;
    }

    // Simple region detection based on title ID patterns
    uint8_t region_byte = (title_id >> 24) & 0xFF;
    
    switch (region_byte) {
        case 0x54: // Japanese titles often start with 54
            region_ = GameRegion::NTSC_J;
            break;
        case 0x45: // European titles often start with 45  
            region_ = GameRegion::PAL;
            break;
        case 0x4D: // North American titles often start with 4D
            region_ = GameRegion::NTSC_U;
            break;
        case 0x51: // Chinese titles often start with 51
            region_ = GameRegion::NTSC_C;
            break;
        default:
            region_ = GameRegion::UNKNOWN;
            break;
    }
    
    return true;
}

// Basic getters
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

std::string GameMetadata::GetFileName() const {
    size_t last_slash = game_path_.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        return game_path_.substr(last_slash + 1);
    }
    return game_path_;
}

uint64_t GameMetadata::GetFileSize() const {
    return file_size_;
}

std::string GameMetadata::GetVersionString() const {
    if (version_ == 0) return "Unknown";
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d",
             (version_ >> 24) & 0xFF,
             (version_ >> 16) & 0xFF, 
             (version_ >> 8) & 0xFF,
             version_ & 0xFF);
    return std::string(buffer);
}

uint32_t GameMetadata::GetDiscNumber() const {
    return disc_number_;
}

uint32_t GameMetadata::GetDiscCount() const {
    return disc_count_;
}

bool GameMetadata::IsValid() const {
    return is_valid_;
}

bool GameMetadata::IsXbox360Title() const {
    return file_type_ != GameFileType::UNKNOWN;
}

bool GameMetadata::IsDemo() const {
    return is_demo_;
}

// Formatted information
std::string GameMetadata::GetFormattedTitleId() const {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%08X", title_id_);
    return std::string(buffer);
}

std::string GameMetadata::GetRegionString() const {
    switch (region_) {
        case GameRegion::NTSC_U: return "NTSC-U";
        case GameRegion::NTSC_J: return "NTSC-J";
        case GameRegion::PAL: return "PAL";
        case GameRegion::NTSC_C: return "NTSC-C";
        case GameRegion::REGION_FREE: return "Region Free";
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

std::string GameMetadata::GetFormattedFileSize() const {
    if (file_size_ == 0) return "Unknown";
    
    const char* units[] = {"B", "KB", "MB", "GB"};
    double size = file_size_;
    int unit_index = 0;
    
    while (size >= 1024.0 && unit_index < 3) {
        size /= 1024.0;
        unit_index++;
    }
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unit_index]);
    return std::string(buffer);
}

// Save data management
std::string GameMetadata::GetSavePath(const std::string& content_root) const {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s/%08X", content_root.c_str(), title_id_);
    return std::string(buffer);
}

bool GameMetadata::HasSaveData() const {
    return has_save_data_;
}

// Static utility methods
std::vector<GameMetadata> GameMetadata::ScanDirectory(const std::string& directory_path) {
    std::vector<GameMetadata> games;
    
    XELOGI("Scanning directory for games: %s", directory_path.c_str());
    
    // Simple implementation - in real code, use proper directory traversal
    // This would iterate through files and create GameMetadata for supported types
    
    return games;
}

bool GameMetadata::IsSupportedFileType(const std::string& file_path) {
    if (file_path.empty()) {
        return false;
    }

    GameFileType file_type = DetectFileType(file_path);
    return file_type != GameFileType::UNKNOWN;
}

GameFileType GameMetadata::DetectFileType(const std::string& file_path) {
    if (file_path.empty()) {
        return GameFileType::UNKNOWN;
    }

    // Extract extension
    size_t dot_pos = file_path.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return GameFileType::UNKNOWN;
    }
    
    std::string extension = file_path.substr(dot_pos + 1);
    std::string extension_lower = extension;
    std::transform(extension_lower.begin(), extension_lower.end(), extension_lower.begin(), ::tolower);
    
    if (extension_lower == "xex" || extension_lower == "elf") {
        return GameFileType::XEX;
    } else if (extension_lower == "iso" || extension_lower == "xiso") {
        return GameFileType::ISO;
    } else if (extension_lower == "zar") {
        return GameFileType::ZAR;
    } else if (extension_lower == "con") {
        return GameFileType::CON;
    } else if (extension_lower == "live") {
        return GameFileType::LIVE;
    } else if (extension_lower == "pirs") {
        return GameFileType::PIRS;
    }
    
    return GameFileType::UNKNOWN;
}

std::string GameMetadata::GetFileTypeExtension(GameFileType file_type) {
    switch (file_type) {
        case GameFileType::XEX: return "xex";
        case GameFileType::ISO: return "iso";
        case GameFileType::ZAR: return "zar";
        case GameFileType::CON: return "con";
        case GameFileType::LIVE: return "live";
        case GameFileType::PIRS: return "pirs";
        default: return "unknown";
    }
}

std::vector<std::string> GameMetadata::GetSupportedExtensions() {
    return {"xex", "iso", "xiso", "zar", "con", "live", "pirs", "elf"};
}

bool GameMetadata::ValidateGameFile(const std::string& file_path) {
    GameMetadata metadata(file_path);
    return metadata.IsValid();
}

// File parsing utilities
uint32_t GameMetadata::ReadUint32(std::ifstream& file) {
    uint32_t value;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

uint64_t GameMetadata::ReadUint64(std::ifstream& file) {
    uint64_t value;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return value;
}

std::string GameMetadata::ReadString(std::ifstream& file, size_t length) {
    std::vector<char> buffer(length + 1);
    file.read(buffer.data(), length);
    buffer[length] = '\0';
    return std::string(buffer.data());
}

bool GameMetadata::SeekToOffset(std::ifstream& file, uint64_t offset) {
    file.seekg(offset);
    return file.good();
}

} // namespace xanite