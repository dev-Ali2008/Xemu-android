#ifndef CXXOPTS_HPP_
#define CXXOPTS_HPP_

// Minimal stub for cxxopts library
// For Android builds, this provides basic compatibility
// Full cxxopts functionality would require the actual library

#include <string>
#include <map>
#include <vector>
#include <memory>

namespace cxxopts {

// Value type template
template<typename T>
class value {
public:
    value() {}
    T as() const { return T{}; }
};

// Factory function for value<T> - must be in namespace scope
template<typename T>
value<T> make_value() {
    return value<T>{};
}

// Helper class for ParseResult operator[]
class ParseResultValue {
public:
    ParseResultValue(const std::string& val) : value_(val) {}

    template<typename T>
    T as() const {
        return T{};
    }

private:
    std::string value_;
};

// Parse result stub
class ParseResult {
public:
    ParseResultValue operator[](const std::string& key) const {
        return ParseResultValue("");
    }

    template<typename T>
    T as(const std::string& key) const {
        return T{};
    }

    bool count(const std::string& key) const {
        return false;
    }

    std::vector<std::string> arguments() const {
        return {};
    }
};

class Options {
public:
    Options(const std::string& name, const std::string& description)
        : name_(name), description_(description) {}

    Options& add_options(const std::string& group = "") { return *this; }
    Options& operator()(const std::string&, const std::string&) { return *this; }

    template<typename T>
    Options& operator()(const std::string& name, const std::string& desc) {
        return *this;
    }

    template<typename T>
    Options& operator()(const std::string& name, const std::string& desc, value<T> val) {
        return *this;
    }

    ParseResult parse(int argc, char** argv) {
        return ParseResult{};
    }

    std::string help(const std::vector<std::string>&) const {
        return description_;
    }

private:
    std::string name_;
    std::string description_;
};

} // namespace cxxopts

#endif // CXXOPTS_HPP_
