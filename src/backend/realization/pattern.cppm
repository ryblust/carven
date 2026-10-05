module carven:backend.realization.pattern;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.composition;
import :backend.target.expr;
import :semantic.semir.completion;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :support.function_ref;
import :support.task;
import std;

struct PatternSubject final {
    TargetLocalID root;
    bool dereference_root;
    std::optional<std::uint32_t> payload_index;
    auto operator==(const PatternSubject&) const noexcept -> bool = default;
};

// Each test's prefix and its projections enclose the remaining successful
// tests. Binding sources are used inside that scope only after full acceptance.
struct PatternSelection final {
    std::list<Lowered<LoweringPredicate>> tests;
    std::map<LocalBindingID, PatternSubject> bindings;
    // Roots introduced by these tests become branch-local at an Or join.
    std::set<TargetLocalID> source_locals;
    bool accepted;
    bool rejected;
};

// The caller keeps the subject storage alive through matching and selected
// binding construction. Alternatives join only distinct realized sources.
// The bound callable outlives the realizer and its outstanding matching tasks.
using PatternBoundRealizer = FunctionRef<
    ContinuationTask<std::optional<TargetExpr>>(PatternID, bool, LoweringStmtBuilder&) noexcept>;

class PatternRealizer final {
public:
    PatternRealizer(
        ModuleLowering& context,
        TargetNameAllocator& names,
        const SemIRBody& body,
        std::span<const SemPatternBounds> bounds,
        PatternBoundRealizer bound = {}
    ) noexcept;
    auto test(Lowered<LoweringPredicate> predicate) noexcept -> PatternSelection;
    // Coverage can prove acceptance on this entry. Matching still evaluates
    // required bounds and chooses the first successful alternative's sources.
    auto match(
        PatternID pattern_id,
        const PatternSubject& subject,
        bool accepts_on_entry = false
    ) noexcept -> ContinuationTask<PatternSelection>;
    auto sequence(PatternSelection left, PatternSelection right) noexcept -> PatternSelection;
    auto alternatives(std::vector<PatternSelection> choices) noexcept -> PatternSelection;
    auto select(PatternSelection selection, LoweringStmtBuilder accepted) noexcept
        -> LoweringStmtBuilder;
    // Pure borrowed projections may be removed after all arm uses are known.
    auto projection_locals() const noexcept -> std::span<const TargetLocalID>;
    static auto subject_expression(const PatternSubject& subject) noexcept -> TargetExpr;

private:
    auto single_case(EnumCaseID id) const noexcept -> bool;
    auto branch(
        TargetExpr condition,
        LoweringStmtBuilder selected,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    ModuleLowering& context;
    TargetNameAllocator& names;
    const SemIRBody& body;
    CompletionQuery completion;
    PatternBoundRealizer bound;
    std::vector<TargetLocalID> projections;
};
