module carven:backend.realization.pattern;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.composition;
import :backend.target.expr;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :support.task;
import std;

struct PatternSubject final {
    TargetLocalID root;
    bool dereference_root;
    std::optional<std::uint32_t> payload_index;
};

struct PatternBindingType final {
    LocalBindingID binding;
    TypeID type;
};

struct PatternBindings final {
    std::map<LocalBindingID, TargetLocalID> addresses;
};

// The caller keeps the subject storage alive through matching and selected
// binding preparation. Failed partial matches only write address slots.
using PatternBoundRealizer = std::function<
    ContinuationTask<std::optional<TargetExpr>>(PatternID, bool, LoweringStmtBuilder&)>;

class PatternRealizer final {
public:
    PatternRealizer(
        ModuleLowering& context,
        TargetNameAllocator& names,
        const SemIRBody& body,
        PatternBoundRealizer bound = {}
    ) noexcept;
    auto prepare(
        std::span<const PatternBindingType> bindings,
        LoweringStmtBuilder& destination
    ) noexcept -> PatternBindings;
    auto match(
        PatternID pattern_id,
        const PatternSubject& subject,
        const PatternBindings& bindings
    ) noexcept -> ContinuationTask<Lowered<LoweringPredicate>>;
    auto combine(
        ShortCircuitOperator operation,
        LoweringPredicate left,
        Lowered<LoweringPredicate> right,
        LoweringStmtBuilder& destination
    ) noexcept -> std::optional<LoweringPredicate>;

private:
    auto subject_expression(const PatternSubject& subject) noexcept -> TargetExpr;
    auto branch(
        TargetExpr condition,
        LoweringStmtBuilder selected,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    ModuleLowering& context;
    TargetNameAllocator& names;
    const SemIRBody& body;
    PatternBoundRealizer bound;
};
