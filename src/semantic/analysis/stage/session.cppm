module carven:semantic.analysis.stage.session;

import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.evaluation.output;
import :semantic.evaluation.value;
import :semantic.semir.decl;
import :semantic.semir.structured;
import std;

enum class StageResource { Nodes, Iterations, Instances };

// The static stage of one program construction: it derives executable regions
// and instances and executes static code. Budgets span one outermost root and
// everything that root reaches.
// A body the static stage executes as a whole.
struct StaticBodyRoot final {
    BodyID body;
    BlockSource source;
};

class StaticStage final {
public:
    StaticStage(ProgramDraft& draft, ConstructionRequests& requests) noexcept;
    StaticStage(const StaticStage&) = delete;
    StaticStage(StaticStage&&) = delete;
    auto operator=(const StaticStage&) -> StaticStage& = delete;
    auto operator=(StaticStage&&) -> StaticStage& = delete;
    ~StaticStage() = default;

    auto draft() noexcept -> ProgramDraft&;
    auto requests() noexcept -> ConstructionRequests&;

    // Derives the executable region of a completed body without static
    // parameters. A request made while the body is being derived returns at
    // once; `realizing` tells the requester so.
    auto realize_body(BodyID body) noexcept -> AnalysisTask<void>;
    auto realizing(BodyID body) const noexcept -> bool;

    // Specializes a function for its static arguments on first use and returns
    // the callable of that instance. A request made while the instance is being
    // specialized returns its reserved callable.
    auto instance(
        FunctionID function,
        std::vector<ConstantID> arguments,
        ProgramOriginID origin
    ) noexcept -> AnalysisTask<CallableID>;

    // Executes an expression that reads no local binding.
    auto evaluate(
        const SemanticExpression& expression,
        ExecutionOutputMode output = ExecutionOutputMode::Write
    ) noexcept -> AnalysisTask<ExecutionValue>;
    // Executes a body that belongs to the static stage as a whole: a const
    // test or a module const block.
    auto run_body(BodyID body, BlockSource source) noexcept -> AnalysisTask<void>;
    // Executes a const block of a completed body. The region reads no binding
    // declared outside it.
    auto run_block(BodyID body, const SemanticRegion& region, BlockSource source) noexcept
        -> AnalysisTask<void>;

    auto charge(StageResource resource, ProgramOriginID origin) noexcept -> AnalysisResult<void>;
    // The calls whose instances are being specialized, outermost first.
    auto path() const noexcept -> std::span<const ProgramOriginID>;

private:
    class Root;

    auto limit_failure(ProgramOriginID origin, std::string_view resource) noexcept
        -> AnalysisFailure;

    ProgramDraft& program;
    ConstructionRequests& construction_requests;
    std::size_t roots = 0uz;
    std::vector<ProgramOriginID> calls;
    std::array<std::size_t, 3> work {};
    std::set<BodyID> active_bodies;
    // A failed body keeps its failure for every later requester.
    std::map<BodyID, std::optional<AnalysisFailure>> realized_bodies;
};
