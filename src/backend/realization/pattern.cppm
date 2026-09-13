module carven:backend.realization.pattern;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.composition;
import :backend.target.expr;
import :semantic.semir;
import std;

struct PatternSubject final {
    TargetIdentifier root;
    bool dereference_root;
    std::optional<std::uint32_t> payload_index;
};

struct PatternBindingType final {
    LocalBindingID binding;
    TypeID type;
};

struct PatternState final {
    TargetIdentifier matched;
    std::map<LocalBindingID, TargetIdentifier> addresses;
};

// The caller keeps the subject storage alive through matching and selected
// binding construction. Failed partial matches only write address slots.
class PatternRealizer final {
public:
    PatternRealizer(
        ModuleLowering& context,
        TargetNameAllocator& names,
        const SemIRBody& body
    ) noexcept;
    auto prepare(
        std::span<const PatternBindingType> bindings,
        LoweringStmtBuilder& destination
    ) noexcept -> PatternState;
    auto match(
        PatternID pattern_id,
        const PatternSubject& subject,
        const PatternState& state,
        LoweringStmtBuilder& destination
    ) noexcept -> void;

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
};
