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

// Receives completed intervals synchronously, including stages that return errors.
using TimingOutput = std::function<void(TimingStage, std::chrono::steady_clock::duration)>;

// An empty recipient performs no clock reads. The recipient outlives its scopes.
class TimingScope final {
public:
    TimingScope(const TimingOutput& output, TimingStage stage) noexcept;
    TimingScope(const TimingOutput&& output, TimingStage stage) = delete;
    TimingScope(const TimingScope&) = delete;
    auto operator=(const TimingScope&) -> TimingScope& = delete;
    ~TimingScope();
    auto stop() noexcept -> void;

private:
    const TimingOutput* output;
    TimingStage stage;
    std::chrono::steady_clock::time_point started;
};
