module carven:semantic.analysis.pattern.control;

import :semantic.analysis.coverage;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
import :support.invariant;
import :support.task;
import std;

// Propagates analysis state through pattern selection using type coverage.
// State domains provide evaluation and joins.
// A rejected enum tag skips its payload. Range comparisons reject only after
// both bounds complete, and every outward exit bypasses later alternatives.
template<typename State, typename Initial, typename Evaluate, typename Join>
auto analyze_pattern_condition(
    const SemIRProgram& program,
    const SemIRBody& body,
    PatternID id,
    std::span<const SemPatternBounds> bounds,
    State state,
    const Initial& initial,
    const Evaluate& evaluate,
    const Join& join
) noexcept -> ContinuationTask<std::invoke_result_t<const Initial&, State>> {
    auto result = initial(std::move(state));
    const auto& pattern = body.pattern(id).value;
    const auto sequence = [&](PatternID child) noexcept -> ContinuationTask<std::monostate> {
        if (!result.yes) {
            co_return {};
        }
        auto next = (co_await analyze_pattern_condition(
            program,
            body,
            child,
            bounds,
            std::move(result.yes->state),
            initial,
            evaluate,
            join
        ));
        result.yes = std::move(next.yes);
        join(result.no, next.no);
        result.exits.append_range(std::views::as_rvalue(next.exits));
        co_return {};
    };
    if (const auto* alternatives = std::get_if<OrPattern>(&pattern)) {
        result.no = std::move(result.yes);
        result.yes.reset();
        for (const auto child : alternatives->alternatives) {
            if (!result.no) {
                break;
            }
            auto next = (co_await analyze_pattern_condition(
                program,
                body,
                child,
                bounds,
                std::move(result.no->state),
                initial,
                evaluate,
                join
            ));
            join(result.yes, next.yes);
            result.no = std::move(next.no);
            result.exits.append_range(std::views::as_rvalue(next.exits));
        }
    } else if (const auto* enumeration = std::get_if<EnumCasePattern>(&pattern)) {
        const auto owner = program.declarations().enum_case(enumeration->enum_case).owner;
        if (program.declarations().enumeration(owner).cases.size() > 1uz) {
            result.no = result.yes;
        }
        for (const auto child : enumeration->payload) {
            (co_await sequence(child));
        }
    } else if (std::holds_alternative<LiteralPattern>(pattern)
               || std::holds_alternative<RangePattern>(pattern)) {
        const auto found = std::ranges::find(bounds, id, &SemPatternBounds::pattern);
        if (found != bounds.end()) {
            for (const auto* bound : {&found->begin, &found->end}) {
                if (!*bound || !result.yes) {
                    continue;
                }
                auto next = (co_await evaluate(**bound, std::move(result.yes->state)));
                result.yes = std::move(next.normal);
                result.exits.append_range(std::views::as_rvalue(next.exits));
            }
        }
        result.no = result.yes;
    } else if (!std::holds_alternative<WildcardPattern>(pattern)
               && !std::holds_alternative<BindingPattern>(pattern)
               && !std::holds_alternative<TypeConstraintPattern>(pattern)) {
        invariant_violation("pattern selection received an unsupported pattern");
    }
    if (result.no
        && (std::holds_alternative<LiteralPattern>(pattern)
            || std::holds_alternative<RangePattern>(pattern)
            || std::holds_alternative<OrPattern>(pattern))) {
        const auto arms = std::array {
            PatternCoverageArm {.alternatives = {id}, .guarded = false},
        };
        const auto exhaustive =
            patterns_exhaustive(program, body.pattern_table(), body.pattern(id).type, arms);
        if (!exhaustive) {
            invariant_violation(exhaustive.error());
        }
        if (*exhaustive) {
            result.no.reset();
        }
    }
    co_return result;
}
