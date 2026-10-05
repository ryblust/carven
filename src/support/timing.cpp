module carven:support.timing.impl;

import :support.timing;
import std;

TimingScope::TimingScope(const TimingOutput& output, TimingStage stage) noexcept
    : output(output ? std::addressof(output) : nullptr),
      stage(stage),
      started(
          output ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {}
      ) {}

TimingScope::~TimingScope() {
    stop();
}

auto TimingScope::stop() noexcept -> void {
    if (output != nullptr) {
        const auto elapsed = std::chrono::steady_clock::now() - started;
        const auto recipient = std::exchange(output, nullptr);
        (*recipient)(stage, elapsed);
    }
}
