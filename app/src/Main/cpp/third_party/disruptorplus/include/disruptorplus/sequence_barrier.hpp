#pragma once

#include <cstdint>

namespace disruptorplus {

using sequence_t = uint64_t;

template<typename WaitStrategy>
class sequence_barrier {
public:
    sequence_barrier(WaitStrategy& wait_strategy) {}
    sequence_t wait_for(sequence_t sequence) { return sequence; }
};

}
