#pragma once

namespace disruptorplus {
class spin_wait_strategy {
public:
    spin_wait_strategy() {}
    void signal_all_when_blocking() {}
};
}
