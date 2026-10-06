module carven:driver.timings;

import :support.timing;
import std;

// Declared before command resources so the final report includes their cleanup.
class CommandTimings final {
public:
    CommandTimings(bool enabled, std::string_view command) noexcept;
    CommandTimings(const CommandTimings&) = delete;
    auto operator=(const CommandTimings&) -> CommandTimings& = delete;
    ~CommandTimings();
    auto output() noexcept -> TimingOutput;
    auto set_outcome(std::string_view outcome) noexcept -> void;
    auto operator()(TimingStage stage, std::chrono::steady_clock::duration elapsed) noexcept
        -> void;

private:
    std::optional<std::chrono::steady_clock::time_point> started;
    std::array<
        std::optional<std::chrono::steady_clock::duration>,
        static_cast<std::size_t>(TimingStage::Count)>
        durations {};
    std::string_view command;
    std::string outcome = "failed";
};
