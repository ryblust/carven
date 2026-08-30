module carven:semantic.analysis.elaboration.patterns;

import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import std;

auto constraint_type(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstraintOperand& operand
) noexcept -> HIRTypeID;

auto build_pattern(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTPatternID id,
    HIRTypeID subject_type
) noexcept -> HIRPatternID;

struct PatternSubject final {
    ASTPatternID pattern;
    HIRTypeID type;
};

struct WildcardPatternSubject final {
    Span span;
};

using PatternAlternativeSubject = std::variant<PatternSubject, WildcardPatternSubject>;

auto pattern_alternatives(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    std::span<const PatternAlternativeSubject> subjects
) noexcept -> std::vector<std::optional<HIRPatternID>>;

auto analyze_match(
    ModuleAnalysis& module_analysis,
    HIRTypeID subject_type,
    std::span<const HIRMatchArm> arms,
    Span match_span
) noexcept -> bool;

auto analyze_catch_pattern(
    ModuleAnalysis& module_analysis,
    std::span<const HIRCatchPatternAlternative> alternatives
) noexcept -> void;
