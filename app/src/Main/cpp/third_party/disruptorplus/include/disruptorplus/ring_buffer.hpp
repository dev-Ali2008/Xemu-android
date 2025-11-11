#pragma once

#include <vector>
#include <memory>

namespace disruptorplus {
template<typename T>
class ring_buffer {
public:
    ring_buffer(size_t capacity) : capacity_(capacity) {}
    T& operator[](size_t index) { return data_[index % capacity_]; }
    size_t size() const { return capacity_; }

private:
    size_t capacity_;
    std::vector<T> data_;
};
}
