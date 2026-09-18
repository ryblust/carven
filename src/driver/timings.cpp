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

} // namespace

CommandTimings::CommandTimings(bool enabled, std::string_view command) noexcept
    : command(command) {
    if (enabled) {
        measurements.emplace();
    }
}

CommandTimings::~CommandTimings() {
    if (!measurements) {
        return;
    }
    const auto total = measurements->elapsed();
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
        "Program execution"
    };
    static_assert(labels.size() == static_cast<std::size_t>(TimingStage::Count));
    for (auto index = 0uz; index < labels.size(); ++index) {
        if (const auto duration = measurements->duration(static_cast<TimingStage>(index))) {
            std::println(std::cerr, "  {:<22} {:>12}", labels[index], format_duration(*duration));
        }
    }
}

auto CommandTimings::recorder() noexcept -> TimingRecorder* {
    return measurements ? std::addressof(*measurements) : nullptr;
}

auto CommandTimings::set_outcome(std::string_view value) noexcept -> void {
    if (measurements) {
        outcome = value;
    }
}
