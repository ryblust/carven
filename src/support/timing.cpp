module carven:support.timing.impl;

import :support.timing;
import std;

TimingRecorder::TimingRecorder() noexcept
    : started(std::chrono::steady_clock::now()) {}

auto TimingRecorder::add(TimingStage stage, std::chrono::steady_clock::duration elapsed) noexcept
    -> void {
    auto& value = durations[static_cast<std::size_t>(stage)];
    value = value.value_or(std::chrono::steady_clock::duration::zero()) + elapsed;
}

auto TimingRecorder::duration(TimingStage stage) const noexcept
    -> std::optional<std::chrono::steady_clock::duration> {
    return durations[static_cast<std::size_t>(stage)];
}

auto TimingRecorder::elapsed() const noexcept -> std::chrono::steady_clock::duration {
    return std::chrono::steady_clock::now() - started;
}

TimingScope::TimingScope(TimingRecorder* recorder, TimingStage stage) noexcept
    : recorder(recorder),
      stage(stage),
      started(
          recorder != nullptr ? std::chrono::steady_clock::now()
                              : std::chrono::steady_clock::time_point {}
      ) {}

TimingScope::~TimingScope() {
    stop();
}

auto TimingScope::stop() noexcept -> void {
    if (recorder != nullptr) {
        recorder->add(stage, std::chrono::steady_clock::now() - started);
        recorder = nullptr;
    }
}
