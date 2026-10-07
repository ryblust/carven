module carven:support.timing;

import :support.function_ref;
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
    // Semantic details and SourceObservations are included in SemanticAnalysis.
    SemanticCatalog,
    SemanticDeclarations,
    SemanticBodies,
    SemanticSolving,
    SemanticValidation,
    SourceObservations,
    SourceIndex,
    Count,
};

// Receives completed intervals synchronously, including stages that return errors.
using TimingOutput = FunctionRef<void(TimingStage, std::chrono::steady_clock::duration) noexcept>;

// An empty recipient performs no clock reads. The callable outlives its scopes.
class TimingScope final {
public:
    TimingScope(TimingOutput output, TimingStage stage) noexcept;
    TimingScope(const TimingScope&) = delete;
    auto operator=(const TimingScope&) -> TimingScope& = delete;
    ~TimingScope();
    auto stop() noexcept -> void;

private:
    TimingOutput output;
    TimingStage stage;
    std::chrono::steady_clock::time_point started;
};
