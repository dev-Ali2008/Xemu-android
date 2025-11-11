#ifndef FMT_FORMAT_H_
#define FMT_FORMAT_H_

// Minimal stub for fmt library
// For Android builds, this provides basic compatibility
// Full fmt functionality would require the actual library

#include <string>
#include <sstream>
#include <cstdio>
#include <iterator>
#include <utility>

namespace fmt {

// Runtime format string wrapper
template<typename Char>
class basic_runtime {
public:
    basic_runtime(const Char* str) : str_(str) {}
    basic_runtime(const std::basic_string_view<Char>& sv) : str_(sv.data()) {}
    const Char* c_str() const { return str_; }
private:
    const Char* str_;
};

using runtime = basic_runtime<char>;

// Format result for format_to_n
template<typename OutputIt>
struct format_to_n_result {
    OutputIt out;
    std::size_t size;
};

// Basic format function
template<typename... Args>
inline std::string format(const char* fmt_str, Args... args) {
    char buffer[4096];
    std::snprintf(buffer, sizeof(buffer), fmt_str, args...);
    return std::string(buffer);
}

// format_to_n - formats to output iterator with size limit
template<typename OutputIt, typename... Args>
inline format_to_n_result<OutputIt> format_to_n(OutputIt out, std::size_t n, const char* fmt_str, Args... args) {
    char buffer[4096];
    int written = std::snprintf(buffer, std::min(n, sizeof(buffer)), fmt_str, args...);
    if (written > 0) {
        for (int i = 0; i < written && i < static_cast<int>(n); ++i) {
            *out++ = buffer[i];
        }
    }
    return {out, static_cast<std::size_t>(written > 0 ? written : 0)};
}

// format_to_n with runtime format
template<typename OutputIt, typename... Args>
inline format_to_n_result<OutputIt> format_to_n(OutputIt out, std::size_t n, runtime fmt, Args... args) {
    return format_to_n(out, n, fmt.c_str(), args...);
}

// print function
template<typename... Args>
inline void print(const char* fmt_str, Args... args) {
    std::printf(fmt_str, args...);
}

} // namespace fmt

#endif // FMT_FORMAT_H_
