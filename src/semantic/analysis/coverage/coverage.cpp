module carven:semantic.analysis.coverage.impl;

import :semantic.analysis.coverage;
import :semantic.analysis.program;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

struct CoveragePattern;

struct CoverageAny final {
    ConstructionTypeRef type;
};

struct CoverageAtom final {
    ConstructionTypeRef type;
    // Floating patterns use numeric equality; constant identities preserve sign bits.
    std::variant<ConstantID, bool, float, double> value;
};

struct CoverageInterval final {
    ConstructionTypeRef type;
    std::uint64_t begin;
    std::uint64_t end;
    bool dynamic;
    bool empty;
};

struct CoverageCase final {
    ConstructionTypeRef type;
    EnumCaseID enum_case;
    std::vector<CoveragePattern> payload;
};

struct CoverageOr final {
    ConstructionTypeRef type;
    std::vector<CoveragePattern> alternatives;
};

struct CoveragePattern final {
    std::variant<CoverageAny, CoverageAtom, CoverageInterval, CoverageCase, CoverageOr> value;
};

// Rows borrow lowered patterns and constructor shapes for the active query.
using Row = std::vector<const CoveragePattern*>;
using Matrix = std::vector<Row>;

auto pattern_row(std::span<const CoveragePattern> patterns) noexcept -> Row {
    auto result = Row();
    result.reserve(patterns.size());
    for (const auto& pattern : patterns) {
        result.push_back(std::addressof(pattern));
    }
    return result;
}

auto pattern_type(const CoveragePattern& pattern) noexcept -> ConstructionTypeRef {
    return pattern.value.visit(
        []<typename Value>(const Value& value) static noexcept -> ConstructionTypeRef {
            static_assert(
                std::same_as<Value, CoverageAny>
                    || std::same_as<Value, CoverageAtom>
                    || std::same_as<Value, CoverageCase>
                    || std::same_as<Value, CoverageOr>
                    || std::same_as<Value, CoverageInterval>,
                "unhandled coverage pattern"
            );
            return value.type;
        }
    );
}

auto children(const CoveragePattern& pattern) noexcept -> std::span<const CoveragePattern> {
    return pattern.value.visit(
        Overloaded {
            [](const CoverageCase& value) static noexcept -> std::span<const CoveragePattern> {
                return value.payload;
            },
            [](const CoverageOr& value) static noexcept -> std::span<const CoveragePattern> {
                return value.alternatives;
            },
            [](const CoverageAny&) static noexcept -> std::span<const CoveragePattern> {
                return {};
            },
            [](const CoverageInterval&) static noexcept -> std::span<const CoveragePattern> {
                return {};
            },
            [](const CoverageAtom&) static noexcept -> std::span<const CoveragePattern> {
                return {};
            },
        }
    );
}

auto same_constructor(const CoveragePattern& left, const CoveragePattern& right) noexcept -> bool {
    if (const auto* left_case = std::get_if<CoverageCase>(&left.value)) {
        const auto* right_case = std::get_if<CoverageCase>(&right.value);
        return right_case != nullptr && left_case->enum_case == right_case->enum_case;
    }
    if (const auto* left_atom = std::get_if<CoverageAtom>(&left.value)) {
        const auto* right_atom = std::get_if<CoverageAtom>(&right.value);
        return right_atom != nullptr && left_atom->value == right_atom->value;
    }
    return false;
}

auto expand_head(std::span<const Row> source) noexcept -> Matrix {
    auto result = Matrix();
    const auto append = [&](this const auto& self, Row row) noexcept -> void {
        if (!row.empty()) {
            if (const auto* alternatives = std::get_if<CoverageOr>(&row.front()->value)) {
                for (const auto& alternative : alternatives->alternatives) {
                    auto expanded = row;
                    expanded.front() = std::addressof(alternative);
                    self(std::move(expanded));
                }
                return;
            }
        }
        result.push_back(std::move(row));
    };
    for (const auto& row : source) {
        append(row);
    }
    return result;
}

auto specialize(std::span<const Row> source, const CoveragePattern& constructor) noexcept
    -> Matrix {
    auto result = Matrix();
    for (const auto& row : expand_head(source)) {
        if (row.empty()) {
            continue;
        }
        auto tail = Row(row.begin() + 1, row.end());
        if (std::holds_alternative<CoverageAny>(row.front()->value)) {
            auto prefix = pattern_row(children(constructor));
            prefix.insert(prefix.end(), tail.begin(), tail.end());
            result.push_back(std::move(prefix));
        } else if (same_constructor(*row.front(), constructor)) {
            auto prefix = pattern_row(children(*row.front()));
            prefix.insert(prefix.end(), tail.begin(), tail.end());
            result.push_back(std::move(prefix));
        }
    }
    return result;
}

auto defaults(std::span<const Row> source) noexcept -> Matrix {
    auto result = Matrix();
    for (const auto& row : expand_head(source)) {
        if (!row.empty() && std::holds_alternative<CoverageAny>(row.front()->value)) {
            result.emplace_back(row.begin() + 1, row.end());
        }
    }
    return result;
}

// Partition only at written interval boundaries, never at every integer.
auto ordinal(IntegerConstant value, BuiltinType type) noexcept -> std::uint64_t {
    const auto width = *builtin_integer_width(type);
    if (!builtin_is_signed_integer(type)) {
        return value.magnitude();
    }
    const auto midpoint = std::uint64_t {1} << (width - 1u);
    return value.negative() ? midpoint - value.magnitude() : midpoint + value.magnitude();
}

auto interval_contains(const CoveragePattern& pattern, std::uint64_t point, bool query) noexcept
    -> bool {
    if (std::holds_alternative<CoverageAny>(pattern.value)) {
        return true;
    }
    if (const auto* range = std::get_if<CoverageInterval>(&pattern.value)) {
        return range->dynamic ? query
                              : !range->empty && range->begin <= point && point <= range->end;
    }
    return false;
}

auto interval_points(std::span<const Row> matrix, const CoveragePattern* query) noexcept
    -> std::vector<std::uint64_t> {
    auto points = std::vector<std::uint64_t> {0u};
    const auto add = [&](const CoveragePattern& pattern) noexcept {
        if (const auto* range = std::get_if<CoverageInterval>(&pattern.value);
            range && !range->dynamic && !range->empty) {
            points.push_back(range->begin);
            if (range->end != std::numeric_limits<std::uint64_t>::max()) {
                points.push_back(range->end + 1u);
            }
        }
    };
    for (const auto& row : matrix) {
        if (!row.empty()) {
            add(*row.front());
        }
    }
    if (query) {
        add(*query);
    }
    std::ranges::sort(points);
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

auto specialize_interval(std::span<const Row> matrix, std::uint64_t point) noexcept -> Matrix {
    auto result = Matrix();
    for (const auto& row : matrix) {
        if (!row.empty() && interval_contains(*row.front(), point, false)) {
            result.emplace_back(row.begin() + 1, row.end());
        }
    }
    return result;
}

template<typename Program, typename PatternTable>
class CoverageAnalyzer final {
public:
    CoverageAnalyzer(const Program& source, const PatternTable& patterns) noexcept
        : program(source),
          patterns(patterns) {}

    auto exhaustive(
        ConstructionTypeRef subject_type,
        std::span<const PatternCoverageArm> arms
    ) noexcept -> std::expected<bool, std::string> {
        auto lowered = lower_arms(subject_type, arms);
        if (!lowered) {
            return std::unexpected(std::move(lowered.error()));
        }
        auto matrix = Matrix();
        for (auto index = 0uz; index < arms.size(); ++index) {
            if (!arms[index].guarded) {
                for (const auto& pattern : (*lowered)[index]) {
                    matrix.push_back({std::addressof(pattern)});
                }
            }
        }
        const auto any = CoveragePattern {.value = CoverageAny {.type = subject_type}};
        return useful(matrix, Row {std::addressof(any)}).transform([](bool value) static noexcept {
            return !value;
        });
    }

    auto run(ConstructionTypeRef subject_type, std::span<const PatternCoverageArm> arms) noexcept
        -> std::expected<PatternCoverage, std::string> {
        auto lowered = lower_arms(subject_type, arms);
        if (!lowered) {
            return std::unexpected(std::move(lowered.error()));
        }
        auto result = PatternCoverage {
            .arm_usefulness = {},
            .alternative_usefulness = {},
            .redundant_alternatives = {},
            .exhaustive_after_arm = std::vector<bool>(arms.size(), false),
            .pattern_rejection = std::vector<bool>(arms.size(), true),
            .exhaustive = false,
            .missing_witness = "_",
        };
        auto matrix = Matrix();
        auto prefix_ends = std::vector<std::size_t>();
        for (auto arm = 0uz; arm < arms.size(); ++arm) {
            const auto& alternatives = (*lowered)[arm];
            auto redundant = redundant_within_arm(alternatives);
            if (!redundant) {
                return std::unexpected(std::move(redundant.error()));
            }
            auto usefulness = std::vector<bool>();
            auto arm_useful = false;
            for (auto index = 0uz; index < alternatives.size(); ++index) {
                auto found = useful(matrix, Row {std::addressof(alternatives[index])});
                if (!found) {
                    return std::unexpected(std::move(found.error()));
                }
                arm_useful |= *found;
                usefulness.push_back(*found && !(*redundant)[index]);
                if ((*redundant)[index]) {
                    result.redundant_alternatives.push_back({.arm = arm, .alternative = index});
                }
            }
            if (!arms[arm].guarded) {
                for (auto index = 0uz; index < alternatives.size(); ++index) {
                    if (usefulness[index]) {
                        matrix.push_back({std::addressof(alternatives[index])});
                    }
                }
            }
            result.arm_usefulness.push_back(arm_useful);
            result.alternative_usefulness.push_back(std::move(usefulness));
            prefix_ends.push_back(matrix.size());
        }
        const auto any = CoveragePattern {.value = CoverageAny {.type = subject_type}};
        const auto query = Row {std::addressof(any)};
        auto missing = residual_witness(matrix, query);
        if (!missing) {
            return std::unexpected(std::move(missing.error()));
        }
        result.exhaustive = !missing->has_value();
        if (missing->has_value()) {
            result.missing_witness = std::move((*missing)->front());
        } else {
            // Prefix coverage is monotone. Locate its boundary without repeating
            // an exhaustiveness search after every arm.
            auto begin = 0uz;
            auto end = arms.size();
            while (begin < end) {
                const auto middle = begin + (end - begin) / 2uz;
                auto found = useful(std::span(matrix).first(prefix_ends[middle]), query);
                if (!found) {
                    return std::unexpected(std::move(found.error()));
                }
                if (*found) {
                    begin = middle + 1uz;
                } else {
                    end = middle;
                }
            }
            std::fill(
                result.exhaustive_after_arm.begin() + static_cast<std::ptrdiff_t>(begin),
                result.exhaustive_after_arm.end(),
                true
            );
        }
        for (auto arm = 0uz; arm < arms.size(); ++arm) {
            if (!arms[arm].guarded || result.exhaustive_after_arm[arm]) {
                result.pattern_rejection[arm] = !result.exhaustive_after_arm[arm];
                continue;
            }
            // Guarded arms do not extend definite coverage. Test their pattern
            // against the existing prefix without lowering that prefix again.
            auto selected = Matrix(
                matrix.begin(),
                matrix.begin() + static_cast<std::ptrdiff_t>(prefix_ends[arm])
            );
            for (const auto& alternative : (*lowered)[arm]) {
                selected.push_back({std::addressof(alternative)});
            }
            auto rejected = useful(selected, query);
            if (!rejected) {
                return std::unexpected(std::move(rejected.error()));
            }
            result.pattern_rejection[arm] = *rejected;
        }
        return result;
    }

private:
    using WitnessRow = std::vector<std::string>;

    auto lower_arms(
        ConstructionTypeRef subject_type,
        std::span<const PatternCoverageArm> arms
    ) noexcept -> std::expected<std::vector<std::vector<CoveragePattern>>, std::string> {
        if (!owned(subject_type)) {
            return std::unexpected("coverage subject type belongs to another program");
        }
        auto result = std::vector<std::vector<CoveragePattern>>();
        result.reserve(arms.size());
        for (const auto& arm : arms) {
            if (arm.alternatives.empty()) {
                return std::unexpected("coverage arm has no alternatives");
            }
            auto lowered = std::vector<CoveragePattern>();
            lowered.reserve(arm.alternatives.size());
            for (const auto alternative : arm.alternatives) {
                if (!alternative.has_value()) {
                    lowered.push_back({.value = CoverageAny {.type = subject_type}});
                    continue;
                }
                auto pattern = lower(*alternative, subject_type);
                if (!pattern.has_value()) {
                    return std::unexpected(std::move(pattern.error()));
                }
                lowered.push_back(std::move(*pattern));
            }
            result.push_back(std::move(lowered));
        }
        return result;
    }

    // Redundancy is intrinsic to an arm, independent of earlier arms or guards.
    auto redundant_within_arm(std::span<const CoveragePattern> lowered) noexcept
        -> std::expected<std::vector<bool>, std::string> {
        auto redundant = std::vector<bool>(lowered.size(), false);
        for (auto candidate = 0uz; candidate < lowered.size(); ++candidate) {
            for (auto other = 0uz; other < lowered.size(); ++other) {
                if (candidate == other) {
                    continue;
                }
                auto candidate_after_other = useful(
                    Matrix {Row {std::addressof(lowered[other])}},
                    Row {std::addressof(lowered[candidate])}
                );
                if (!candidate_after_other.has_value()) {
                    return std::unexpected(std::move(candidate_after_other.error()));
                }
                auto other_after_candidate = useful(
                    Matrix {Row {std::addressof(lowered[candidate])}},
                    Row {std::addressof(lowered[other])}
                );
                if (!other_after_candidate.has_value()) {
                    return std::unexpected(std::move(other_after_candidate.error()));
                }
                const auto strictly_subsumed = !*candidate_after_other && *other_after_candidate;
                const auto repeated_after_first =
                    !*candidate_after_other && !*other_after_candidate && other < candidate;
                if (strictly_subsumed || repeated_after_first) {
                    redundant[candidate] = true;
                    break;
                }
            }
        }
        for (auto candidate = 0uz; candidate < lowered.size(); ++candidate) {
            if (redundant[candidate]) {
                continue;
            }
            auto other_active = Matrix();
            for (auto other = 0uz; other < lowered.size(); ++other) {
                if (candidate != other && !redundant[other]) {
                    other_active.push_back(Row {std::addressof(lowered[other])});
                }
            }
            auto candidate_useful = useful(other_active, Row {std::addressof(lowered[candidate])});
            if (!candidate_useful.has_value()) {
                return std::unexpected(std::move(candidate_useful.error()));
            }
            redundant[candidate] = !*candidate_useful;
        }
        return redundant;
    }

    auto owned(ConstructionTypeRef type) const noexcept -> bool {
        return type.visit([&]<typename ID>(ID id) noexcept {
            static_assert(std::same_as<ID, TypeID> || std::same_as<ID, TypeTermID>);
            return id.owner() == program.identity();
        });
    }

    template<typename SourcePattern>
    auto lower_pattern(const SourcePattern& pattern, ConstructionTypeRef expected_type) noexcept
        -> std::expected<CoveragePattern, std::string> {
        const auto source_type = ConstructionTypeRef {pattern.type};
        if (!owned(source_type)) {
            return std::unexpected("coverage pattern type belongs to another program");
        }
        if (source_type != expected_type) {
            return std::unexpected("coverage pattern type differs from its subject");
        }
        return pattern.value.visit(
            Overloaded {
                [&](const WildcardPattern&) -> std::expected<CoveragePattern, std::string> {
                    return CoveragePattern {.value = CoverageAny {.type = expected_type}};
                },
                [&](const BindingPattern&) -> std::expected<CoveragePattern, std::string> {
                    return CoveragePattern {.value = CoverageAny {.type = expected_type}};
                },
                [&](const ElaboratedTypeConstraintPattern&)
                    -> std::expected<CoveragePattern, std::string> {
                    return CoveragePattern {.value = CoverageAny {.type = expected_type}};
                },
                [&](const TypeConstraintPattern&) -> std::expected<CoveragePattern, std::string> {
                    return CoveragePattern {.value = CoverageAny {.type = expected_type}};
                },
                [&](const LiteralPattern& value) -> std::expected<CoveragePattern, std::string> {
                    if (value.constant.owner() != program.identity()) {
                        return std::unexpected(
                            "literal coverage constant belongs to another program"
                        );
                    }
                    const auto fact = constant(value.constant);
                    if (ConstructionTypeRef {fact.type} != expected_type) {
                        return std::unexpected(
                            "literal coverage constant type differs from its subject"
                        );
                    }
                    if (const auto* integer = std::get_if<IntegerConstant>(&fact.value)) {
                        const auto point = ordinal(*integer, *integer_type(expected_type));
                        return CoveragePattern {
                            .value = CoverageInterval {
                                .type = expected_type,
                                .begin = point,
                                .end = point,
                                .dynamic = false,
                                .empty = false
                            }
                        };
                    }
                    auto atom = decltype(CoverageAtom::value) {value.constant};
                    if (const auto* boolean = std::get_if<BooleanConstant>(&fact.value)) {
                        atom = boolean->value;
                    } else if (const auto* floating = std::get_if<F32Constant>(&fact.value)) {
                        atom = floating->value;
                    } else if (const auto* floating = std::get_if<F64Constant>(&fact.value)) {
                        atom = floating->value;
                    }
                    return CoveragePattern {
                        .value = CoverageAtom {
                            .type = expected_type,
                            .value = atom,
                        },
                    };
                },
                [&](const RangePattern& value) -> std::expected<CoveragePattern, std::string> {
                    const auto kind = integer_type(expected_type);
                    if (!kind) {
                        return std::unexpected("range pattern subject is not integer");
                    }
                    auto begin = std::uint64_t {0};
                    auto end = integer_max(*kind);
                    const auto dynamic = (value.begin && !value.begin->constant)
                        || (value.end && !value.end->constant);
                    if (value.begin && value.begin->constant) {
                        const auto fact = constant(*value.begin->constant);
                        begin = ordinal(std::get<IntegerConstant>(fact.value), *kind);
                    }
                    auto empty = false;
                    if (value.end && value.end->constant) {
                        const auto fact = constant(*value.end->constant);
                        end = ordinal(std::get<IntegerConstant>(fact.value), *kind);
                        if (!value.inclusive) {
                            empty = end == 0u;
                            if (!empty) {
                                --end;
                            }
                        }
                    }
                    return CoveragePattern {
                        .value = CoverageInterval {
                            .type = expected_type,
                            .begin = begin,
                            .end = end,
                            .dynamic = dynamic,
                            .empty = empty || begin > end
                        }
                    };
                },
                [&](const EnumCasePattern& value) -> std::expected<CoveragePattern, std::string> {
                    const auto* concrete = std::get_if<TypeID>(&expected_type);
                    if (concrete == nullptr) {
                        return std::unexpected("case coverage subject is not concrete");
                    }
                    const auto canonical = this->type(*concrete);
                    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
                    if (nominal == nullptr) {
                        return std::unexpected("case coverage subject is not an enum");
                    }
                    if (value.enum_case.owner() != program.identity()) {
                        return std::unexpected("coverage enum case belongs to another program");
                    }
                    const auto member = enum_case(value.enum_case);
                    if (member.owner != nominal->enumeration
                        || member.payload_types.size() != value.payload.size()) {
                        return std::unexpected(
                            "case coverage payload does not match its enum member"
                        );
                    }
                    auto payload = std::vector<CoveragePattern>();
                    payload.reserve(value.payload.size());
                    for (const auto [child, payload_type] :
                         std::views::zip(value.payload, member.payload_types)) {
                        auto lowered = lower(child, payload_type);
                        if (!lowered.has_value()) {
                            return std::unexpected(std::move(lowered.error()));
                        }
                        payload.push_back(std::move(*lowered));
                    }
                    return CoveragePattern {
                        .value = CoverageCase {
                            .type = expected_type,
                            .enum_case = value.enum_case,
                            .payload = std::move(payload),
                        },
                    };
                },
                [&](const OrPattern& value) -> std::expected<CoveragePattern, std::string> {
                    if (value.alternatives.empty()) {
                        return std::unexpected("coverage or-pattern has no alternatives");
                    }
                    auto alternatives = std::vector<CoveragePattern>();
                    alternatives.reserve(value.alternatives.size());
                    for (const auto child : value.alternatives) {
                        auto lowered = lower(child, expected_type);
                        if (!lowered.has_value()) {
                            return std::unexpected(std::move(lowered.error()));
                        }
                        alternatives.push_back(std::move(*lowered));
                    }
                    return CoveragePattern {
                        .value = CoverageOr {
                            .type = expected_type,
                            .alternatives = std::move(alternatives),
                        },
                    };
                },
            }
        );
    }

    auto lower(PatternID id, ConstructionTypeRef expected_type) noexcept
        -> std::expected<CoveragePattern, std::string> {
        if (id.owner().program() != program.identity()) {
            return std::unexpected("coverage pattern belongs to another semantic program");
        }
        if constexpr (std::same_as<PatternTable, MutableBodyTable<ElaboratedPattern, PatternID>>) {
            return lower_pattern(patterns.copy(id), expected_type);
        } else {
            return lower_pattern(patterns.get(id), expected_type);
        }
    }

    auto integer_type(ConstructionTypeRef reference) noexcept -> std::optional<BuiltinType> {
        const auto* id = std::get_if<TypeID>(&reference);
        if (!id) {
            return std::nullopt;
        }
        const auto canonical = type(*id);
        const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
        return builtin && builtin_is_integer(builtin->kind) ? std::optional(builtin->kind)
                                                            : std::nullopt;
    }

    static auto integer_max(BuiltinType kind) noexcept -> std::uint64_t {
        const auto width = *builtin_integer_width(kind);
        return width == 64u ? std::numeric_limits<std::uint64_t>::max()
                            : (std::uint64_t {1} << width) - 1u;
    }

    // Build one matching value. Product columns are independent when no
    // covering rows remain, so this never enumerates their Cartesian product.
    auto inhabitant(const Row& query) noexcept
        -> std::expected<std::optional<WitnessRow>, std::string> {
        auto result = WitnessRow();
        for (const auto* pattern : query) {
            auto value = std::optional<std::string>();
            if (const auto* alternatives = std::get_if<CoverageOr>(&pattern->value)) {
                for (const auto& alternative : alternatives->alternatives) {
                    auto found = inhabitant(Row {std::addressof(alternative)});
                    if (!found) {
                        return std::unexpected(std::move(found.error()));
                    }
                    if (found->has_value()) {
                        value = std::move((*found)->front());
                        break;
                    }
                }
            } else if (const auto* range = std::get_if<CoverageInterval>(&pattern->value)) {
                if (range->dynamic || !range->empty) {
                    value = render_integer(
                        range->dynamic ? 0u : range->begin,
                        *integer_type(range->type)
                    );
                }
            } else if (const auto* any = std::get_if<CoverageAny>(&pattern->value)) {
                if (const auto enumeration = enum_type(any->type)) {
                    for (const auto id : enum_cases(*enumeration)) {
                        auto constructor = case_constructor(any->type, id);
                        if (!constructor) {
                            return std::unexpected(std::move(constructor.error()));
                        }
                        auto found = inhabitant(Row {std::addressof(*constructor)});
                        if (!found) {
                            return std::unexpected(std::move(found.error()));
                        }
                        if (found->has_value()) {
                            value = std::move((*found)->front());
                            break;
                        }
                    }
                } else if (const auto kind = integer_type(any->type)) {
                    value = render_integer(0u, *kind);
                } else {
                    value = is_boolean(any->type) ? "false" : "_";
                }
            } else if (const auto* atom = std::get_if<CoverageAtom>(&pattern->value)) {
                // NaN is not equal to itself and matches no value.
                if (same_constructor(*pattern, *pattern)) {
                    const auto* boolean = std::get_if<bool>(&atom->value);
                    value = boolean != nullptr ? (*boolean ? "true" : "false") : "_";
                }
            } else {
                auto payload = inhabitant(pattern_row(children(*pattern)));
                if (!payload) {
                    return std::unexpected(std::move(payload.error()));
                }
                if (payload->has_value()) {
                    auto rendered = render_constructor(*pattern, **payload);
                    if (!rendered) {
                        return std::unexpected(std::move(rendered.error()));
                    }
                    value = std::move(*rendered);
                }
            }
            if (!value) {
                return std::optional<WitnessRow>();
            }
            result.push_back(std::move(*value));
        }
        return std::optional(std::move(result));
    }

    static auto render_integer(std::uint64_t point, BuiltinType kind) noexcept -> std::string {
        if (!builtin_is_signed_integer(kind)) {
            return std::to_string(point);
        }
        const auto midpoint = std::uint64_t {1} << (*builtin_integer_width(kind) - 1u);
        return point < midpoint ? "-" + std::to_string(midpoint - point)
                                : std::to_string(point - midpoint);
    }

    auto is_boolean(ConstructionTypeRef reference) const noexcept -> bool {
        const auto* concrete = std::get_if<TypeID>(&reference);
        if (concrete == nullptr) {
            return false;
        }
        const auto canonical = type(*concrete);
        const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
        return builtin != nullptr && builtin->kind == BuiltinType::Bool;
    }

    auto useful(std::span<const Row> matrix, const Row& query) noexcept
        -> std::expected<bool, std::string> {
        return residual_witness(matrix, query)
            .transform([](const std::optional<WitnessRow>& witness) static noexcept {
                return witness.has_value();
            });
    }

    // Find one value in query that is outside the unguarded covering rows.
    // The same set-difference query supplies usefulness, redundancy and coverage.
    auto residual_witness(std::span<const Row> matrix, const Row& query) noexcept
        -> std::expected<std::optional<WitnessRow>, std::string> {
        if (std::ranges::any_of(matrix, [](const Row& row) static noexcept {
                return std::ranges::all_of(row, [](const CoveragePattern* pattern) static noexcept {
                    return std::holds_alternative<CoverageAny>(pattern->value);
                });
            })) {
            return std::optional<WitnessRow>();
        }
        if (matrix.empty()) {
            return inhabitant(query);
        }
        const auto& head = *query.front();
        if (const auto* alternatives = std::get_if<CoverageOr>(&head.value)) {
            for (const auto& alternative : alternatives->alternatives) {
                auto expanded = query;
                expanded.front() = std::addressof(alternative);
                auto found = residual_witness(matrix, expanded);
                if (!found || found->has_value()) {
                    return found;
                }
            }
            return std::optional<WitnessRow>();
        }
        const auto tail = Row(query.begin() + 1, query.end());
        if (const auto kind = integer_type(pattern_type(head))) {
            const auto expanded = expand_head(matrix);
            for (const auto point : interval_points(expanded, std::addressof(head))) {
                if (point > integer_max(*kind) || !interval_contains(head, point, true)) {
                    continue;
                }
                auto found = residual_witness(specialize_interval(expanded, point), tail);
                if (!found) {
                    return found;
                }
                if (found->has_value()) {
                    (*found)->insert((*found)->begin(), render_integer(point, *kind));
                    return found;
                }
            }
            return std::optional<WitnessRow>();
        }
        if (!std::holds_alternative<CoverageAny>(head.value)) {
            if (std::holds_alternative<CoverageAtom>(head.value) && !same_constructor(head, head)) {
                return std::optional<WitnessRow>();
            }
            return residual_constructor(matrix, head, tail);
        }
        const auto expanded = expand_head(matrix);
        if (std::ranges::all_of(expanded, [](const Row& row) static noexcept {
                return std::holds_alternative<CoverageAny>(row.front()->value);
            })) {
            auto first = inhabitant(Row {std::addressof(head)});
            if (!first || !first->has_value()) {
                return first;
            }
            auto found = residual_witness(defaults(expanded), tail);
            if (found && found->has_value()) {
                (*found)->insert((*found)->begin(), std::move((*first)->front()));
            }
            return found;
        }
        if (const auto enumeration = enum_type(pattern_type(head))) {
            for (const auto id : enum_cases(*enumeration)) {
                auto constructor = case_constructor(pattern_type(head), id);
                if (!constructor) {
                    return std::unexpected(std::move(constructor.error()));
                }
                auto found = residual_constructor(expanded, *constructor, tail);
                if (!found || found->has_value()) {
                    return found;
                }
            }
            return std::optional<WitnessRow>();
        }
        if (is_boolean(pattern_type(head))) {
            for (const auto value : {false, true}) {
                const auto constructor = CoveragePattern {
                    .value = CoverageAtom {.type = pattern_type(head), .value = value},
                };
                auto found = residual_constructor(expanded, constructor, tail);
                if (!found || found->has_value()) {
                    return found;
                }
            }
            return std::optional<WitnessRow>();
        }
        auto found = residual_witness(defaults(expanded), tail);
        if (found && found->has_value()) {
            (*found)->insert((*found)->begin(), "_");
        }
        return found;
    }

    auto residual_constructor(
        std::span<const Row> matrix,
        const CoveragePattern& constructor,
        const Row& tail
    ) noexcept -> std::expected<std::optional<WitnessRow>, std::string> {
        auto query = pattern_row(children(constructor));
        query.insert(query.end(), tail.begin(), tail.end());
        auto shape = std::optional<CoveragePattern>();
        if (const auto* member = std::get_if<CoverageCase>(&constructor.value)) {
            auto canonical = case_constructor(member->type, member->enum_case);
            if (!canonical) {
                return std::unexpected(std::move(canonical.error()));
            }
            shape = std::move(*canonical);
        }
        auto found = residual_witness(specialize(matrix, shape ? *shape : constructor), query);
        if (!found || !found->has_value()) {
            return found;
        }
        const auto arity = children(constructor).size();
        auto& row = **found;
        auto head = render_constructor(constructor, std::span(row).first(arity));
        if (!head) {
            return std::unexpected(std::move(head.error()));
        }
        row.erase(row.begin(), row.begin() + static_cast<std::ptrdiff_t>(arity));
        row.insert(row.begin(), std::move(*head));
        return found;
    }

    auto render_constructor(
        const CoveragePattern& constructor,
        std::span<const std::string> payload
    ) noexcept -> std::expected<std::string, std::string> {
        if (const auto* atom = std::get_if<CoverageAtom>(&constructor.value)) {
            const auto* boolean = std::get_if<bool>(&atom->value);
            if (!payload.empty()) {
                return std::unexpected("atomic coverage constructor has a payload");
            }
            return boolean != nullptr ? (*boolean ? "true" : "false") : "_";
        }
        const auto* value = std::get_if<CoverageCase>(&constructor.value);
        if (value == nullptr || value->payload.size() != payload.size()) {
            return std::unexpected("enum coverage constructor has the wrong witness arity");
        }
        const auto enum_case = this->enum_case(value->enum_case);
        const auto name = [&]() noexcept -> std::string {
            if constexpr (std::same_as<Program, ProgramDraft>) {
                return program.spelling_copy(enum_case.name);
            } else {
                return std::string(program.provenance().spelling(enum_case.name));
            }
        }();
        auto witness = std::format(".{}", name);
        if (!payload.empty()) {
            witness += '(';
            for (auto index = 0uz; index < payload.size(); ++index) {
                if (index != 0uz) {
                    witness += ", ";
                }
                witness += payload[index];
            }
            witness += ')';
        }
        return witness;
    }

    auto enum_type(ConstructionTypeRef reference) const noexcept -> std::optional<EnumID> {
        const auto* concrete = std::get_if<TypeID>(&reference);
        if (concrete == nullptr) {
            return std::nullopt;
        }
        const auto canonical = type(*concrete);
        if (const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value)) {
            return nominal->enumeration;
        }
        return std::nullopt;
    }

    auto enum_cases(EnumID id) const noexcept -> std::span<const EnumCaseID> {
        if constexpr (std::same_as<PatternTable, MutableBodyTable<ElaboratedPattern, PatternID>>) {
            return program.enum_cases(id);
        } else {
            return program.declarations().enumeration(id).cases;
        }
    }

    auto case_constructor(ConstructionTypeRef type, EnumCaseID id) noexcept
        -> std::expected<CoveragePattern, std::string> {
        const auto member = enum_case(id);
        auto payload = std::vector<CoveragePattern>();
        payload.reserve(member.payload_types.size());
        for (const auto payload_type : member.payload_types) {
            if (!owned(payload_type)) {
                return std::unexpected("coverage enum payload type belongs to another program");
            }
            payload.push_back({.value = CoverageAny {.type = payload_type}});
        }
        return CoveragePattern {
            .value = CoverageCase {
                .type = type,
                .enum_case = id,
                .payload = std::move(payload),
            },
        };
    }

    auto type(TypeID id) const noexcept -> CanonicalType {
        if constexpr (std::same_as<PatternTable, MutableBodyTable<ElaboratedPattern, PatternID>>) {
            return program.type_copy(id);
        } else {
            return program.types().type(id);
        }
    }

    auto constant(ConstantID id) const noexcept -> ConstantFact {
        if constexpr (std::same_as<PatternTable, MutableBodyTable<ElaboratedPattern, PatternID>>) {
            return program.constant(id);
        } else {
            return program.constants().constant(id);
        }
    }

    auto enum_case(EnumCaseID id) const noexcept {
        if constexpr (std::same_as<PatternTable, MutableBodyTable<ElaboratedPattern, PatternID>>) {
            return program.construction_enum_case_declaration_copy(id);
        } else {
            return program.declarations().enum_case(id);
        }
    }

    const Program& program;
    const PatternTable& patterns;
};

} // namespace

auto compute_pattern_coverage(
    const ProgramDraft& draft,
    const MutableBodyTable<ElaboratedPattern, PatternID>& patterns,
    ConstructionTypeRef subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    return CoverageAnalyzer(draft, patterns).run(subject_type, arms);
}

auto patterns_exhaustive(
    const SemIRProgram& semantic,
    const ImmutableBodyTable<Pattern, PatternID>& patterns,
    TypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<bool, std::string> {
    return CoverageAnalyzer(semantic, patterns).exhaustive(subject_type, arms);
}

auto compute_pattern_coverage(
    const SemIRProgram& semantic,
    const ImmutableBodyTable<Pattern, PatternID>& patterns,
    TypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    return CoverageAnalyzer(semantic, patterns).run(subject_type, arms);
}
