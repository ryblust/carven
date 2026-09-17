module carven:semantic.evaluation.executor;

import :semantic.evaluation.execution;
import :semantic.evaluation.operation;
import :semantic.evaluation.shape;
import :semantic.semir.structured;
import std;

enum class ExecutionFlow { Normal, Return, Break, Continue };

struct ExecutionCompletion final {
    ExecutionFlow flow;
    ExecutionValue value;
};

struct ExecutionUninitialized final {};

struct ExecutionTaken final {};

// An execution slot identifies an owner in this frame, including retained temporaries.
struct ExecutionPlace final {
    std::size_t slot;
    std::vector<std::size_t> path;
};

using ExecutionSlot =
    std::variant<ExecutionUninitialized, ExecutionValue, ExecutionTaken, ExecutionPlace>;

struct ExecutionFrame final {
    std::optional<ExecutionBody> body;
    // Whole-binding assignment establishes a live value, including after Take.
    std::vector<ExecutionSlot> slots;
    std::vector<ExecutionSourceFailure> caught;
};

using ExecutionOperand = std::variant<ExecutionValue, ExecutionPlace>;

class SemanticExecutor final {
public:
    SemanticExecutor(
        ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        ExecutionLimits limits
    ) noexcept;
    auto evaluate_test(const StructuredBodyDraft& body) noexcept -> ExecutionResult<void>;
    auto evaluate_root(const SemanticExpression& source) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto invoke(
        FunctionID function,
        std::vector<ExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;

private:
    auto fail(ProgramOriginID origin, DiagnosticCode code, std::string message) noexcept
        -> ExecutionFailure;
    auto step(ProgramOriginID origin) noexcept -> ExecutionResult<void>;
    auto account_text(std::size_t bytes, ProgramOriginID origin) noexcept -> ExecutionResult<void>;
    auto account_aggregate(std::size_t elements, ProgramOriginID origin) noexcept
        -> ExecutionResult<void>;
    auto read_borrows_storage(TypeID type) noexcept -> bool;
    auto own_storage(ExecutionValue value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto copy_value(const ExecutionValue& value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto check_aggregate_size(TypeID type, ProgramOriginID origin) noexcept
        -> ExecutionResult<void>;
    auto type(ConstructionTypeRef type, ProgramOriginID origin) noexcept -> ExecutionResult<TypeID>;
    auto read_fact(const ExecutionValue& value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ConstantFact>;
    auto text(const ExecutionValue& value, ProgramOriginID origin) noexcept
        -> ExecutionResult<std::string_view>;
    auto equal(
        const ExecutionValue& left,
        const ExecutionValue& right,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<bool>;
    auto boolean(const ExecutionValue& value, ProgramOriginID origin) noexcept
        -> ExecutionResult<bool>;
    auto finish(
        std::expected<ConstantFact, ConstantEvaluationFailure> result,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto slot_value(ExecutionFrame& frame, std::size_t slot, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue*>;
    static auto local_place(const SemanticExpression& expression) noexcept -> bool;
    auto place(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionResult<ExecutionPlace>;
    auto located(
        ExecutionFrame& frame,
        const ExecutionPlace& place,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue*>;
    auto offset(const ExecutionValue& value, std::size_t extent, ProgramOriginID origin) noexcept
        -> ExecutionResult<std::size_t>;
    auto value(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto read_operand(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionResult<ExecutionOperand>;
    auto materialize(
        ExecutionFrame& frame,
        ExecutionOperand operand,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto expression(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionResult<ExecutionCompletion>;
    auto statement(ExecutionFrame& frame, const SemanticStatement& statement) noexcept
        -> ExecutionResult<ExecutionCompletion>;
    auto region(ExecutionFrame& frame, const SemanticRegion& region) noexcept
        -> ExecutionResult<ExecutionCompletion>;
    auto loop(ExecutionFrame& frame, const SemLoop& loop, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionCompletion>;
    auto range_loop(
        ExecutionFrame& frame,
        const SemRangeLoop& loop,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionCompletion>;
    auto matches(
        ExecutionFrame& frame,
        PatternID pattern,
        const ExecutionValue& value,
        std::span<const SemPatternBounds> pattern_bounds
    ) noexcept -> ExecutionResult<bool>;
    auto format(ExecutionFrame& frame, const SemFormat& format, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto print(ExecutionFrame& frame, const SemPrint& operation, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto test_report(
        ExecutionFrame& frame,
        const SemTestReport& operation,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto text_storage(
        ExecutionFrame& frame,
        const ExecutionPlace& place,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionOwnedText*>;
    auto append_text(
        ExecutionFrame& frame,
        const ExecutionPlace& destination,
        std::string_view bytes,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto text_intrinsic(
        ExecutionFrame& frame,
        const SemTextIntrinsic& operation,
        TypeID result_type,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;

    ExecutionValueAccess& values;
    SemanticExecutionContext& context;
    const ExecutionLimits limits;
    ExecutionTypeShapes shapes;
    std::map<TypeID, bool> storage_reads;
    std::vector<ProgramOriginID> calls;
    bool testing = false;
    bool test_failed = false;
    std::size_t steps = 0;
    std::size_t text_work = 0;
    std::size_t aggregate_work = 0;
};
