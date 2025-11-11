#pragma once

#include <cstdint>

namespace disruptorplus {

using sequence_t = uint64_t;

template<typename WaitStrategy>
class multi_threaded_claim_strategy {
public:
    multi_threaded_claim_strategy(size_t capacity, WaitStrategy& wait_strategy) {}
    sequence_t claim() { return 0; }
    void publish(sequence_t sequence) {}
    bool is_available(sequence_t sequence) { return true; }
    void add_claim_barrier(sequence_barrier<WaitStrategy>& barrier) {}
};

}
