#ifndef DATE_TZ_H_
#define DATE_TZ_H_

// Minimal stub for date/tz.h library
// For Android builds, this provides basic compatibility
// Full date library functionality would require the actual library

#include <chrono>
#include <string>

namespace date {

// Basic date components
class year {
public:
    constexpr explicit year(int y) : y_(y) {}
    constexpr int operator*() const { return y_; }

    template<typename Rep, typename Period>
    constexpr auto time_since_epoch() const {
        return std::chrono::duration<Rep, Period>{0};
    }
private:
    int y_;
};

class month {
public:
    constexpr explicit month(unsigned m) : m_(m) {}
    constexpr unsigned operator*() const { return m_; }
private:
    unsigned m_;
};

class day {
public:
    constexpr explicit day(unsigned d) : d_(d) {}
    constexpr unsigned operator*() const { return d_; }
private:
    unsigned d_;
};

// Time duration types
using sys_days = std::chrono::duration<long long, std::ratio<86400>>;
using sys_seconds = std::chrono::duration<long long>;

// Time zone stub - minimal implementation
class time_zone {
public:
    static time_zone* locate_zone(const std::string& name) {
        return nullptr;
    }
};

// System clock time point
using sys_time = std::chrono::system_clock::time_point;

// Date operators - minimal implementation for chrono.h
constexpr year operator/(year y, month m) {
    return y;
}

constexpr year operator/(year y, day d) {
    return y;
}

} // namespace date

#endif // DATE_TZ_H_
