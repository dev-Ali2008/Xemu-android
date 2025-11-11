#ifndef TOML_HPP_
#define TOML_HPP_

// Minimal stub for toml++ library - FIXED VERSION
// No function name conflicts

#include <string>
#include <map>
#include <vector>
#include <variant>
#include <optional>
#include <memory>
#include <cstdint>
#include <type_traits>

namespace toml {

// Forward declarations
class node;
class table;
class value_node;

// Base node type
class node {
public:
    virtual ~node() = default;
    
    virtual bool is_string() const { return false; }
    virtual bool is_integer() const { return false; }
    virtual bool is_floating_point() const { return false; }
    virtual bool is_boolean() const { return false; }
    virtual bool is_array() const { return false; }
    virtual bool is_table() const { return false; }
    virtual bool is_value() const { return false; }

    virtual std::optional<std::string> as_string() const { return std::nullopt; }
    virtual std::optional<int64_t> as_integer() const { return std::nullopt; }
    virtual std::optional<double> as_floating_point() const { return std::nullopt; }
    virtual std::optional<bool> as_boolean() const { return std::nullopt; }

    template<typename T>
    std::optional<T> value() const {
        if constexpr (std::is_same_v<T, std::string>) {
            return as_string();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return as_integer();
        } else if constexpr (std::is_same_v<T, int>) {
            auto val = as_integer();
            return val ? std::optional<int>(static_cast<int>(*val)) : std::nullopt;
        } else if constexpr (std::is_same_v<T, double>) {
            auto val = as_floating_point();
            if (val) return val;
            auto ival = as_integer();
            return ival ? std::optional<double>(static_cast<double>(*ival)) : std::nullopt;
        } else if constexpr (std::is_same_v<T, bool>) {
            return as_boolean();
        } else {
            return std::nullopt;
        }
    }

    template<typename T>
    T value_or(const T& default_val) const {
        auto val = value<T>();
        return val ? *val : default_val;
    }

    virtual table* as_table() { return nullptr; }
    virtual const table* as_table() const { return nullptr; }
    virtual value_node* as_value() { return nullptr; }
    virtual const value_node* as_value() const { return nullptr; }
};

class value_node : public node {
private:
    std::variant<std::string, int64_t, double, bool> data_;

public:
    value_node() = default;
    
    value_node(const std::string& val) : data_(val) {}
    value_node(const char* val) : data_(std::string(val)) {}
    value_node(int64_t val) : data_(val) {}
    value_node(int val) : data_(static_cast<int64_t>(val)) {}
    value_node(double val) : data_(val) {}
    value_node(bool val) : data_(val) {}

    bool is_value() const override { return true; }

    bool is_string() const override { 
        return std::holds_alternative<std::string>(data_); 
    }
    
    bool is_integer() const override { 
        return std::holds_alternative<int64_t>(data_); 
    }
    
    bool is_floating_point() const override { 
        return std::holds_alternative<double>(data_); 
    }
    
    bool is_boolean() const override { 
        return std::holds_alternative<bool>(data_); 
    }

    std::optional<std::string> as_string() const override {
        if (is_string()) return std::get<std::string>(data_);
        return std::nullopt;
    }

    std::optional<int64_t> as_integer() const override {
        if (is_integer()) return std::get<int64_t>(data_);
        return std::nullopt;
    }

    std::optional<double> as_floating_point() const override {
        if (is_floating_point()) return std::get<double>(data_);
        if (is_integer()) return static_cast<double>(std::get<int64_t>(data_));
        return std::nullopt;
    }

    std::optional<bool> as_boolean() const override {
        if (is_boolean()) return std::get<bool>(data_);
        return std::nullopt;
    }

    value_node* as_value() override { return this; }
    const value_node* as_value() const override { return this; }
};

class table : public node {
private:
    std::map<std::string, std::shared_ptr<node>> data_;

public:
    table() = default;

    bool is_table() const override { return true; }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto it = data_.find(key);
        if (it != data_.end() && it->second) {
            return it->second->value<T>();
        }
        return std::nullopt;
    }

    template<typename T>
    T value_or(const std::string& key, const T& default_value) const {
        auto val = get<T>(key);
        return val ? *val : default_value;
    }

    const node* get_node(const std::string& key) const {
        auto it = data_.find(key);
        return it != data_.end() ? it->second.get() : nullptr;
    }

    node* get_node(const std::string& key) {
        auto it = data_.find(key);
        return it != data_.end() ? it->second.get() : nullptr;
    }

    bool contains(const std::string& key) const {
        return data_.find(key) != data_.end();
    }

    table* as_table() override { return this; }
    const table* as_table() const override { return this; }

    void insert(const std::string& key, std::shared_ptr<node> value) {
        data_[key] = value;
    }

    void insert_value(const std::string& key, const value_node& val) {
        data_[key] = std::make_shared<value_node>(val);
    }

    auto begin() const { return data_.begin(); }
    auto end() const { return data_.end(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
};

class parse_result {
private:
    bool success_;
    std::string error_;
    table table_data_;

public:
    parse_result(bool success = false, const std::string& error = "", const table& tbl = table{})
        : success_(success), error_(error), table_data_(tbl) {}

    bool succeeded() const { return success_; }
    bool failed() const { return !success_; }
    
    // تم تغيير اسم الدوال لتجنب التضارب
    table& get_table() { return table_data_; }
    const table& get_table() const { return table_data_; }
    
    std::string error() const { return error_; }

    explicit operator bool() const { return success_; }

    // Table access operators
    const table& operator*() const { return table_data_; }
    table& operator*() { return table_data_; }
    
    const table* operator->() const { return &table_data_; }
    table* operator->() { return &table_data_; }
};

inline parse_result parse_file(const std::string& path) {
    return parse_result(true, "", table{});
}

inline parse_result parse(const std::string& content) {
    return parse_result(true, "", table{});
}

namespace patch {
    template<typename T>
    inline T value_or(const std::optional<T>& opt, const T& default_value) {
        return opt ? *opt : default_value;
    }
    
    template<typename T>
    inline T value_or(const node* n, const T& default_value) {
        if (!n) return default_value;
        return n->value_or<T>(default_value);
    }
}

} // namespace toml

#define TOML_MAJOR_VERSION 0
#define TOML_MINOR_VERSION 0
#define TOML_PATCH_VERSION 1

#endif // TOML_HPP_
