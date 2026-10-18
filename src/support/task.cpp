module carven:support.task.impl;

import :support.task;
import std;

auto ContinuationTaskLoop::resume_one() noexcept -> void {
    std::exchange(next, {}).resume();
}
