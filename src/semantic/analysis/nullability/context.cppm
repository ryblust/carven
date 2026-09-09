module carven:semantic.analysis.nullability.context;

import :semantic.analysis.nullability;
import :semantic.semir.structured;
import std;

// These paths stop at indirect and native storage. Unknown facts are absent.
using NullPath = std::vector<std::uint64_t>;
enum class NullFact { Null, NonNull };

struct NullPlace final {
    LocalBindingID root;
    NullPath path;
    auto operator<=>(const NullPlace&) const noexcept = default;
};

using NullValue = std::flat_map<NullPath, NullFact>;

struct NullState final {
    std::flat_map<NullPlace, NullFact> facts;
    std::flat_set<LocalBindingID> exposed;
};

struct NullNormal final {
    NullState state;
    NullValue value;
};
enum class NullExitKind { Return, Failure, Break, Continue };

struct NullExit final {
    NullExitKind kind;
    std::optional<TypeID> failure;
    NullState state;
};

struct NullFlow final {
    std::optional<NullNormal> normal;
    std::vector<NullExit> exits;
};

struct NullCondition final {
    std::optional<NullNormal> yes;
    std::optional<NullNormal> no;
    std::vector<NullExit> exits;
};

auto null_path_contains(
    std::span<const std::uint64_t> outer,
    std::span<const std::uint64_t> inner
) noexcept -> bool;
auto join_null_normal(
    std::optional<NullNormal>& target,
    const std::optional<NullNormal>& source
) noexcept -> void;
auto append_null_exits(std::vector<NullExit>& target, std::vector<NullExit> source) noexcept
    -> void;
auto project_null_value(const NullValue& value, std::uint64_t index) noexcept -> NullValue;

class NullabilityBodyAnalyzer final {
public:
    NullabilityBodyAnalyzer(
        const SemIRProgram& program,
        const SemIRBody& body,
        AnalysisDiagnostics diagnostics
    ) noexcept;
    auto run() noexcept -> void;

private:
    auto location(const SemanticExpression& source, bool enclosing = false) const noexcept
        -> std::optional<NullPlace>;
    auto value_at(const NullState& state, const NullPlace& place) const noexcept -> NullValue;
    auto constant_value(const SemanticExpression& source) const noexcept -> NullValue;
    auto truth(const SemanticExpression& source) const noexcept -> std::optional<bool>;
    auto invalidate(NullState& state, const std::optional<NullPlace>& place) const noexcept -> void;
    auto invalidate_exposed(NullState& state) const noexcept -> void;
    auto store(NullState& state, const NullPlace& place, const NullValue& value) const noexcept
        -> void;
    auto expose(NullState& state, const SemanticExpression& source) const noexcept -> void;
    auto refine(
        std::optional<NullNormal>& branch,
        const NullPlace& place,
        NullFact fact
    ) const noexcept -> void;
    auto scan_writes(NullState& state, const SemanticRegion& region) noexcept -> void;
    auto scan_writes(NullState& state, const SemanticExpression& expression) noexcept -> void;
    auto add_range_aliases(const SemRangeLoop& source) noexcept -> void;
    auto scan_write(NullState& state, const SemanticExpression& expression) const noexcept -> void;
    auto expression(const SemanticExpression& source, NullState state) noexcept -> NullFlow;
    auto condition(const SemanticExpression& source, NullState state) noexcept -> NullCondition;
    auto region(const SemanticRegion& source, NullState state) noexcept -> NullFlow;
    auto statement(const SemanticStatement& source, NullState state) noexcept -> NullFlow;
    auto conditional(const SemIf& source, NullState state) noexcept -> NullFlow;
    auto match(const SemMatch& source, NullState state) noexcept -> NullFlow;
    auto attempt(const SemTry& source, NullState state) noexcept -> NullFlow;
    auto loop(const SemLoop& source, NullState state) noexcept -> NullFlow;
    auto range(const SemRangeLoop& source, NullState state) noexcept -> NullFlow;
    auto bind_pattern(NullState& state, PatternID pattern, const NullValue& value) const noexcept
        -> void;
    auto irrefutable(PatternID pattern) const noexcept -> bool;
    auto failures(NullFlow& flow, FailureSetID failures) const noexcept -> void;
    auto require_nonnull(ProgramOriginID origin, const NullValue& value) noexcept -> void;

    const SemIRProgram& program;
    const SemIRBody& body;
    AnalysisDiagnostics diagnostics;
    std::flat_set<LocalBindingID> input_aliases;
    std::flat_set<LocalBindingID> range_aliases;
    std::vector<TypeID> caught;
};
