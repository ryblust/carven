module carven:semantic.evaluation.executor;

import :semantic.evaluation.execution;
import :semantic.evaluation.memory;
import :semantic.evaluation.operation;
import :semantic.evaluation.shape;
import :semantic.semir.structured;
import std;

enum class ExecutionFlow { Normal, Return, Break, Continue };

struct ExecutionCompletion final {
    ExecutionFlow flow;
    ExecutionValue value;
};

struct ExecutionConditionObservation final {
    const SemanticExpression* condition;
    std::array<ProgramSpellingID, 2> sources;
    std::string* explanation;
};

struct ExecutionLogicalTask;

struct ExecutionWork final {
    ExecutionLogicalTask* task;
    std::coroutine_handle<> continuation;
};

// Frames are owned here, never by another task or by a lexical child slot.
struct ExecutionLogicalTask final {
    ContinuationTaskLoop loop;
    std::optional<ExecutionTask<ExecutionValue>> operation;
    std::optional<ExecutionWork> receiver;
    ExecutionLogicalTask* parent;
    bool requested;
    std::vector<ProgramOriginID> calls;
    std::vector<BlockSource> blocks;
    std::optional<ExecutionConditionObservation> condition_observation;
};

struct ExecutionChild final {
    ExecutionLogicalTask* task;
};

struct ExecutionChildRegistration final {
    LifetimeRegionID lifetime;
    LocalBindingID binding;
    std::list<ExecutionLogicalTask>::iterator task;
};

struct ExecutionUninitialized final {};

struct ExecutionTaken final {};

struct ExecutionOwner final {
    ExecutionPlace place;
};

using ExecutionSlot = std::
    variant<ExecutionUninitialized, ExecutionTaken, ExecutionOwner, ExecutionPlace, ExecutionChild>;

struct ExecutionFrame final {
    std::optional<ExecutionBody> body;
    // Whole-binding assignment establishes a live value, including after Take.
    std::vector<ExecutionSlot> slots;
    std::vector<ExecutionSourceFailure> caught;
    std::vector<ExecutionPlace> temporaries;
    std::vector<ExecutionChildRegistration> children;
};

class SemanticExecutor final : private ExecutionArgumentAccess {
public:
    SemanticExecutor(
        const ExecutionValueAccess& values,
        SemanticExecutionContext& context,
        ExecutionLimits limits
    ) noexcept;
    SemanticExecutor(const SemanticExecutor&) = delete;
    SemanticExecutor(SemanticExecutor&&) = delete;
    auto operator=(const SemanticExecutor&) -> SemanticExecutor& = delete;
    auto operator=(SemanticExecutor&&) -> SemanticExecutor& = delete;
    ~SemanticExecutor() noexcept;
    auto evaluate_body(ExecutionBody body) noexcept -> ExecutionTask<void>;
    auto evaluate_root(const SemanticExpression& source) noexcept -> ExecutionTask<ExecutionValue>;
    auto detach_result(ExecutionValue value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    // Reports a typed failure that left a root no handler encloses.
    auto escaped(ExecutionFailure failure) noexcept -> ExecutionFailure;
    auto invoke(
        CallableID callable,
        std::vector<ExecutionOperand> arguments,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionValue>;

private:
    // Only these public roots drive the FIFO. Ordinary calls share the active task.
    auto drive(ExecutionTask<ExecutionValue> operation) noexcept -> ExecutionResult<ExecutionValue>;
    auto call(
        CallableID callable,
        std::vector<ExecutionOperand> arguments,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionValue>;
    auto root_expression(const SemanticExpression& source) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto root_body(ExecutionBody body) noexcept -> ExecutionTask<ExecutionValue>;
    auto consume(ExecutionColdOperation operation, ProgramOriginID origin) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto start_child(
        ExecutionFrame& frame,
        const SemAsyncLet& child,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionCompletion>;
    auto observe_child(
        ExecutionFrame& frame,
        LocalBindingID binding,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionValue>;
    auto close_scope(ExecutionFrame& frame, LifetimeRegionID lifetime, bool cancel) noexcept
        -> ExecutionTask<void>;
    auto close_region(
        ExecutionFrame& frame,
        LifetimeRegionID lifetime,
        ExecutionResult<ExecutionCompletion> result
    ) noexcept -> ExecutionTask<ExecutionCompletion>;
    auto cancellation_requested() const noexcept -> bool;
    auto stop(ExecutionFailure failure) noexcept -> ExecutionFailure;
    static auto fatal(const ExecutionFailure& failure) noexcept -> bool;

    class Parking final {
    public:
        Parking(SemanticExecutor& executor, ExecutionLogicalTask* dependency = nullptr) noexcept;
        auto await_ready() const noexcept -> bool;

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> continuation) noexcept -> void {
            executor.park(continuation.promise().task_loop(), continuation, dependency);
        }

        auto await_resume() const noexcept -> void;

    private:
        SemanticExecutor& executor;
        ExecutionLogicalTask* dependency;
    };

    auto park(
        ContinuationTaskLoop& loop,
        std::coroutine_handle<> continuation,
        ExecutionLogicalTask* dependency
    ) noexcept -> void;
    auto detach_argument(ExecutionOperand operand, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue> override;
    auto bind(ExecutionFrame& frame, std::size_t slot, ExecutionValue value) noexcept -> void;
    auto release(ExecutionFrame& frame, std::size_t slot) noexcept -> void;
    auto release_frame(ExecutionFrame& frame) noexcept -> void;
    auto release_temporaries(ExecutionFrame& frame, std::size_t begin) noexcept -> void;
    auto release_region(ExecutionFrame& frame, LifetimeRegionID lifetime) noexcept -> void;
    auto simd(
        ExecutionFrame& frame,
        const SemIntrinsic& operation,
        const SemanticExpression& source
    ) noexcept -> ExecutionTask<ExecutionValue>;
    auto default_value(TypeID type, ProgramOriginID origin) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto fail(
        ProgramOriginID origin,
        ExecutionReason reason,
        std::string message,
        std::vector<ExecutionReportField> fields = {}
    ) noexcept -> ExecutionFailure;
    auto halt(ExecutionEvent event) noexcept -> ExecutionFailure;
    auto trap(
        ProgramOriginID origin,
        ExecutionReason reason,
        std::string message,
        std::vector<ExecutionReportField> fields = {}
    ) noexcept -> ExecutionFailure;
    auto operation_failure(
        ConstantEvaluationFailure failure,
        ProgramOriginID origin,
        std::string_view fallback
    ) noexcept -> ExecutionFailure;
    auto step(ProgramOriginID origin) noexcept -> ExecutionResult<void>;
    auto account_text(std::size_t bytes, ProgramOriginID origin) noexcept -> ExecutionResult<void>;
    auto account_aggregate(std::size_t elements, ProgramOriginID origin) noexcept
        -> ExecutionResult<void>;
    auto read_borrows_storage(ConstructionTypeRef type) noexcept -> bool;
    auto own_storage(ExecutionValue value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto copy_value(const ExecutionValue& value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    auto retained_slice(ConstantID id, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionSlice>;
    auto sequence_view(
        ExecutionFrame& frame,
        const SemanticExpression& expression,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionSlice>;
    auto slice_element(
        const ExecutionSlice& slice,
        std::size_t index,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionPlace>;
    auto detach_views(
        ExecutionValue value,
        ProgramOriginID origin,
        std::size_t depth = 0uz
    ) noexcept -> ExecutionResult<ExecutionValue>;
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
    static auto has_bound_storage(const SemanticExpression& expression) noexcept -> bool;
    auto place(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionTask<ExecutionPlace>;
    auto located(const ExecutionPlace& place, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue*>;
    auto offset(const ExecutionValue& value, std::size_t extent, ProgramOriginID origin) noexcept
        -> ExecutionResult<std::size_t>;
    auto finish_unary_value(
        const SemanticExpression& source,
        UnaryOperator operation,
        const ExecutionValue& operand
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto finish_cast_value(
        const SemanticExpression& source,
        CastKind kind,
        ExecutionValue&& operand
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto finish_binary_value(
        const SemanticExpression& source,
        const SemBinary& operation,
        const ExecutionValue& left,
        const ExecutionValue& right
    ) noexcept -> ExecutionResult<ExecutionValue>;
    template<typename Result>
    static auto normal_result(ExecutionResult<ExecutionValue>&& result) noexcept
        -> std::conditional_t<
            std::same_as<Result, ExecutionValue>,
            ExecutionResult<ExecutionValue>&&,
            ExecutionResult<Result>>;
    template<typename Result>
    auto control_result(
        ExecutionResult<ExecutionCompletion>&& result,
        ProgramOriginID origin
    ) noexcept
        -> std::conditional_t<
            std::same_as<Result, ExecutionCompletion>,
            ExecutionResult<ExecutionCompletion>&&,
            ExecutionResult<Result>>;
    auto read_operand(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionTask<ExecutionOperand>;
    // Read follows Carven's type policy; Borrow retains selected storage regardless of type.
    enum class OperandUse { Value, Read, Write, Borrow };
    static auto argument_use(AccessMode access) noexcept -> OperandUse;
    auto operand(
        ExecutionFrame& frame,
        const SemanticExpression& expression,
        OperandUse use,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionOperand>;
    auto materialize(ExecutionOperand operand, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionValue>;
    // Detaches a value from execution storage and freezes it into a constant.
    auto freeze(ExecutionValue value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ConstantID>;
    template<typename Result>
    auto evaluate_expression(ExecutionFrame& frame, const SemanticExpression& source) noexcept
        -> ExecutionTask<Result>;
    template<typename Result, typename Operation>
    auto expression_operation(
        ExecutionFrame& frame,
        const SemanticExpression& source,
        const Operation& operation
    ) noexcept -> ExecutionTask<Result>;
    template<typename Operation>
    auto expression_value(
        ExecutionFrame& frame,
        const SemanticExpression& source,
        const Operation& operation
    ) noexcept -> ExecutionTask<ExecutionValue>;
    auto unsupported_expression(
        ExecutionFrame& frame,
        const SemanticExpression& source,
        std::string_view reason
    ) noexcept -> ExecutionTask<ExecutionCompletion>;
    auto value(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto expression(ExecutionFrame& frame, const SemanticExpression& expression) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto try_expression(ExecutionFrame& frame, const SemTry& attempt) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto if_expression(ExecutionFrame& frame, const SemIf& conditional) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto match_expression(ExecutionFrame& frame, const SemMatch& match) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto statement(ExecutionFrame& frame, const SemanticStatement& statement) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto region(ExecutionFrame& frame, const SemanticRegion& region, bool cleanup = true) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto loop(ExecutionFrame& frame, const SemLoop& loop, ProgramOriginID origin) noexcept
        -> ExecutionTask<ExecutionCompletion>;
    auto range_loop(
        ExecutionFrame& frame,
        const SemRangeLoop& loop,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionCompletion>;
    auto matches(
        ExecutionFrame& frame,
        PatternID pattern,
        const ExecutionValue& value,
        std::span<const SemPatternBounds> pattern_bounds
    ) noexcept -> ExecutionTask<bool>;
    auto format(ExecutionFrame& frame, const SemFormat& format, ProgramOriginID origin) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto print(ExecutionFrame& frame, const SemPrint& operation, ProgramOriginID origin) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto report(ExecutionFrame& frame, const SemReport& operation, ProgramOriginID origin) noexcept
        -> ExecutionTask<ExecutionValue>;
    auto text_handle(const ExecutionValue& value, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionText>;
    auto text_storage(const ExecutionPlace& place, ProgramOriginID origin) noexcept
        -> ExecutionResult<ExecutionOwnedText*>;
    auto append_text(
        const ExecutionPlace& destination,
        std::string_view bytes,
        ProgramOriginID origin
    ) noexcept -> ExecutionResult<ExecutionValue>;
    auto text_intrinsic(
        ExecutionFrame& frame,
        const SemIntrinsic& operation,
        TypeID result_type,
        ProgramOriginID origin
    ) noexcept -> ExecutionTask<ExecutionValue>;

    auto observe_condition(
        const SemanticExpression& source,
        const ExecutionValue& left,
        const ExecutionValue* right,
        bool passed
    ) noexcept -> void;
    const ExecutionValueAccess& values;
    SemanticExecutionContext& context;
    const ExecutionLimits limits;
    ExecutionTypeShapes shapes;
    ExecutionMemory memory;
    std::map<ConstantID, ExecutionPlace> retained_slice_backings;
    std::map<TypeID, bool> storage_reads;
    std::map<ProgramSpellingID, ExecutionText> retained_text;
    // The arena outlives every slot and frame; source memory outlives arena frames.
    std::list<ExecutionLogicalTask> tasks;
    std::deque<ExecutionWork> ready;
    ExecutionLogicalTask* current = nullptr;
    std::optional<ExecutionFailure> stopped;
    bool testing = false;
    std::size_t steps = 0;
    std::size_t text_work = 0;
    std::size_t aggregate_work = 0;
};
