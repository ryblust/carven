module carven:driver.timings.impl;

import :driver.timings;
import :support.timing;
import std;

namespace {

auto format_duration(std::chrono::steady_clock::duration duration) noexcept -> std::string {
    const auto milliseconds = std::chrono::duration<double, std::milli>(duration).count();
    if (milliseconds < 0.1) {
        return "<0.1 ms";
    }
    if (milliseconds < 1000.0) {
        return std::format("{:.1f} ms", milliseconds);
    }
    return std::format("{:.2f} s", milliseconds / 1000.0);
}

auto format_share(
    std::chrono::steady_clock::duration duration,
    std::chrono::steady_clock::duration total
) noexcept -> std::string {
    const auto total_seconds = std::chrono::duration<double>(total).count();
    const auto share = total_seconds > 0.0
        ? 100.0 * std::chrono::duration<double>(duration).count() / total_seconds
        : 0.0;
    if (share < 0.1) {
        return "<0.1%";
    }
    return std::format("{:.1f}%", share);
}

} // namespace

CommandTimings::CommandTimings(bool enabled, std::string_view command) noexcept
    : command(command) {
    if (enabled) {
        started = std::chrono::steady_clock::now();
        recipient =
            [this](TimingStage stage, std::chrono::steady_clock::duration elapsed) noexcept {
                auto& value = durations[static_cast<std::size_t>(stage)];
                value = value.value_or(std::chrono::steady_clock::duration::zero()) + elapsed;
            };
    }
}

CommandTimings::~CommandTimings() {
    if (!started) {
        return;
    }
    const auto total = std::chrono::steady_clock::now() - *started;
    std::cout.flush();
    std::println(std::cerr, "\ncarven: {} {} in {}", command, outcome, format_duration(total));
    const auto labels = std::array {
        "Source collection",
        "Source loading",
        "Lexing",
        "Parsing",
        "Semantic analysis",
        "C++ generation",
        "Artifact writing",
        "Native compilation",
        "Execution"
    };
    static_assert(labels.size() == static_cast<std::size_t>(TimingStage::Count));
    std::println(std::cerr, "  {:<22} {:>12} {:>8}", "Stage", "Time", "% total");
    std::println(std::cerr, "  ---------------------- ------------ --------");
    for (auto index = 0uz; index < labels.size(); ++index) {
        if (const auto duration = durations[index]) {
            std::println(
                std::cerr,
                "  {:<22} {:>12} {:>8}",
                labels[index],
                format_duration(*duration),
                format_share(*duration, total)
            );
        }
    }
}

auto CommandTimings::output() const noexcept -> const TimingOutput& {
    return recipient;
}

auto CommandTimings::set_outcome(std::string_view value) noexcept -> void {
    if (started) {
        outcome = value;
    }
}
