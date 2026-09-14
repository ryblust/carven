module carven:semantic.evaluation.executor;

import :semantic.evaluation.execution;
import :semantic.evaluation.operation;
import :semantic.evaluation.shape;
import :semantic.semir.structured;
import std;

enum class ConstantFlow { Normal, Return, Break, Continue };

struct ConstantCompletion final {
    ConstantFlow flow = ConstantFlow::Normal;
    ConstantExecutionValue value = ConstantVoid {};
};

struct ConstantUninitialized final {};

struct ConstantTaken final {};

// An execution slot identifies an owner in this frame, including retained temporaries.
struct ConstantPlace final {
    std::size_t slot;
    std::vector<std::size_t> path;
};

using ConstantSlot =
    std::variant<ConstantUninitialized, ConstantExecutionValue, ConstantTaken, ConstantPlace>;

struct ConstantFrame final {
    const StructuredBodyDraft* body = nullptr;
    // Whole-binding assignment establishes a live value, including after Take.
    std::vector<ConstantSlot> slots;
};

using ConstantOperand = std::variant<ConstantExecutionValue, ConstantPlace>;

class ConstantExecutor final {
public:
    ConstantExecutor(
        ConstantValueAccess& values,
        ConstantExecutionContext& context,
        ConstantExecutionLimits limits
    ) noexcept;
    auto evaluate_test(const StructuredBodyDraft& body) noexcept -> ConstantExecutionResult<void>;
    auto evaluate_root(const SemanticExpression& source) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto invoke(
        FunctionID function,
        std::vector<ConstantExecutionValue> arguments,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantExecutionValue>;

private:
    auto fail(ProgramOriginID origin, DiagnosticCode code, std::string message) noexcept
        -> ConstantExecutionFailure;
    auto step(ProgramOriginID origin) noexcept -> ConstantExecutionResult<void>;
    auto account_text(std::size_t bytes, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<void>;
    auto account_aggregate(std::size_t elements, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<void>;
    auto read_borrows_storage(TypeID type) noexcept -> bool;
    auto own_storage(ConstantExecutionValue value, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto copy_value(const ConstantExecutionValue& value, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto check_aggregate_size(TypeID type, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<void>;
    auto type(ConstructionTypeRef type, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<TypeID>;
    auto read_fact(const ConstantExecutionValue& value, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantFact>;
    auto text(const ConstantExecutionValue& value, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<std::string_view>;
    auto equal(
        const ConstantExecutionValue& left,
        const ConstantExecutionValue& right,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<bool>;
    auto boolean(const ConstantExecutionValue& value, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<bool>;
    auto finish(
        std::expected<ConstantFact, ConstantEvaluationFailure> result,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantExecutionValue>;
    auto slot_value(ConstantFrame& frame, std::size_t slot, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue*>;
    static auto local_place(const SemanticExpression& expression) noexcept -> bool;
    auto place(ConstantFrame& frame, const SemanticExpression& expression) noexcept
        -> ConstantExecutionResult<ConstantPlace>;
    auto located(ConstantFrame& frame, const ConstantPlace& place, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue*>;
    auto offset(
        const ConstantExecutionValue& value,
        std::size_t extent,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<std::size_t>;
    auto value(ConstantFrame& frame, const SemanticExpression& expression) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto read_operand(ConstantFrame& frame, const SemanticExpression& expression) noexcept
        -> ConstantExecutionResult<ConstantOperand>;
    auto materialize(ConstantFrame& frame, ConstantOperand operand, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto expression(ConstantFrame& frame, const SemanticExpression& expression) noexcept
        -> ConstantExecutionResult<ConstantCompletion>;
    auto statement(ConstantFrame& frame, const SemanticStatement& statement) noexcept
        -> ConstantExecutionResult<ConstantCompletion>;
    auto region(ConstantFrame& frame, const SemanticRegion& region) noexcept
        -> ConstantExecutionResult<ConstantCompletion>;
    auto loop(ConstantFrame& frame, const SemLoop& loop, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantCompletion>;
    auto sequence_loop(
        ConstantFrame& frame,
        const SemRangeLoop& loop,
        const SemSequenceRange& sequence,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantCompletion>;
    auto range_loop(ConstantFrame& frame, const SemRangeLoop& loop, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantCompletion>;
    auto matches(
        ConstantFrame& frame,
        PatternID pattern,
        const ConstantExecutionValue& value
    ) noexcept -> ConstantExecutionResult<bool>;
    auto format(ConstantFrame& frame, const SemFormat& format, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto print(ConstantFrame& frame, const SemPrint& operation, ProgramOriginID origin) noexcept
        -> ConstantExecutionResult<ConstantExecutionValue>;
    auto test_report(
        ConstantFrame& frame,
        const SemTestReport& operation,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantExecutionValue>;
    auto text_storage(
        ConstantFrame& frame,
        const ConstantPlace& place,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantOwnedText*>;
    auto append_text(
        ConstantFrame& frame,
        const ConstantPlace& destination,
        std::string_view bytes,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantExecutionValue>;
    auto text_intrinsic(
        ConstantFrame& frame,
        const SemTextIntrinsic& operation,
        TypeID result_type,
        ProgramOriginID origin
    ) noexcept -> ConstantExecutionResult<ConstantExecutionValue>;

    ConstantValueAccess& values;
    ConstantExecutionContext& context;
    const ConstantExecutionLimits limits;
    ConstantTypeShapes shapes;
    std::map<TypeID, bool> storage_reads;
    std::vector<ProgramOriginID> calls;
    bool testing = false;
    bool test_failed = false;
    std::size_t steps = 0;
    std::size_t text_work = 0;
    std::size_t aggregate_work = 0;
};
