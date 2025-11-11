#pragma once

#include <string>
#include <vector>

namespace utf8 {

// Basic UTF-8 conversion functions - stubs for Android build
template <typename octet_iterator>
octet_iterator append(uint32_t cp, octet_iterator result) {
    *result++ = static_cast<char>(cp & 0xFF);
    return result;
}

template <typename octet_iterator>
uint32_t next(octet_iterator& it, octet_iterator end) {
    uint32_t cp = *it++;
    return cp;
}

template <typename octet_iterator>
bool is_valid(octet_iterator start, octet_iterator end) {
    return true;
}

template <typename octet_iterator, typename u32bit_iterator>
octet_iterator utf32to8(u32bit_iterator start, u32bit_iterator end, octet_iterator result) {
    for (auto it = start; it != end; ++it) {
        result = append(*it, result);
    }
    return result;
}

template <typename octet_iterator, typename u32bit_iterator>
u32bit_iterator utf8to32(octet_iterator start, octet_iterator end, u32bit_iterator result) {
    for (auto it = start; it != end; ) {
        *result++ = next(it, end);
    }
    return result;
}

inline std::string utf16to8(const std::u16string& s) {
    std::string result;
    result.reserve(s.size());
    for (char16_t c : s) {
        if (c < 0x80) {
            result += static_cast<char>(c);
        } else {
            // Simple stub implementation
            result += '?';
        }
    }
    return result;
}

inline std::u16string utf8to16(const std::string& s) {
    std::u16string result;
    result.reserve(s.size());
    for (char c : s) {
        result += static_cast<char16_t>(c);
    }
    return result;
}

} // namespace utf8
