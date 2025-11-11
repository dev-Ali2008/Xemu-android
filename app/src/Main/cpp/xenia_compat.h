#ifndef XENIA_COMPAT_H_
#define XENIA_COMPAT_H_

// Compatibility layer for missing Xenia functions
// This provides wrapper functions to bridge API differences

#include <filesystem>
#include <string>
#include "xenia/base/filesystem.h"
#include "xenia/base/utf8.h"
#include "xenia/base/string_util.h"

namespace xe {
namespace filesystem {

// Wrapper functions for missing filesystem operations
inline bool PathExists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

inline bool PathExists(const std::string& path) {
    return std::filesystem::exists(path);
}

inline std::filesystem::path GetParentPath(const std::filesystem::path& path) {
    return path.parent_path();
}

inline std::filesystem::path GetParentPath(const std::string& path) {
    return std::filesystem::path(path).parent_path();
}

inline std::string GetFileExtension(const std::filesystem::path& path) {
    return path.extension().string();
}

inline std::string GetFileExtension(const std::string& path) {
    return std::filesystem::path(path).extension().string();
}

inline std::string GetFileExtension(const xe::filesystem::FileInfo& file) {
    return GetFileExtension(xe::path_to_utf8(file.path));
}

inline std::string GetFileBaseName(const std::filesystem::path& path) {
    return path.stem().string();
}

inline std::string GetFileBaseName(const std::string& path) {
    return std::filesystem::path(path).stem().string();
}

inline bool CopyFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

inline bool CopyFile(const std::string& from, const std::string& to) {
    return CopyFile(std::filesystem::path(from), std::filesystem::path(to));
}

} // namespace filesystem

namespace utf8 {

// Wrapper functions for missing utf8 operations
inline std::string trim(const std::string& str) {
    return xe::string_util::trim(str);
}

inline std::string to_lower_case(const std::string& str) {
    return lower_ascii(str);
}

inline std::string to_upper_case(const std::string& str) {
    return upper_ascii(str);
}

} // namespace utf8
} // namespace xe

#endif // XENIA_COMPAT_H_
