module carven:support.timing;

import std;

enum class TimingStage {
    SourceCollection,
    SourceLoading,
    Lexing,
    Parsing,
    SemanticAnalysis,
    CppGeneration,
    ArtifactWriting,
    NativeCompilation,
    Execution,
    Count,
};

// Invocation-owned measurements, accumulated across source files. No global state.
class TimingRecorder final {
public:
    TimingRecorder() noexcept;
    auto add(TimingStage stage, std::chrono::steady_clock::duration elapsed) noexcept -> void;
    auto duration(TimingStage stage) const noexcept
        -> std::optional<std::chrono::steady_clock::duration>;
    auto elapsed() const noexcept -> std::chrono::steady_clock::duration;

private:
    std::chrono::steady_clock::time_point started;
    std::array<
        std::optional<std::chrono::steady_clock::duration>,
        static_cast<std::size_t>(TimingStage::Count)>
        durations {};
};

// A null recorder performs no clock reads. The recorder outlives all its scopes.
class TimingScope final {
public:
    TimingScope(TimingRecorder* recorder, TimingStage stage) noexcept;
    TimingScope(const TimingScope&) = delete;
    auto operator=(const TimingScope&) -> TimingScope& = delete;
    ~TimingScope();
    auto stop() noexcept -> void;

private:
    TimingRecorder* recorder;
    TimingStage stage;
    std::chrono::steady_clock::time_point started;
};
