module carven:semantic.analysis.coverage.impl;

import :semantic.analysis.body.builder;
import :semantic.analysis.coverage;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
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
    std::variant<ConstantID, bool> value;
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
    std::variant<CoverageAny, CoverageAtom, CoverageCase, CoverageOr> value;
};

using Row = std::vector<CoveragePattern>;
using Matrix = std::vector<Row>;

auto pattern_type(const CoveragePattern& pattern) noexcept -> ConstructionTypeRef {
    return std::visit(
        []<typename Value>(const Value& value) static noexcept -> ConstructionTypeRef {
            static_assert(
                std::same_as<Value, CoverageAny>
                    || std::same_as<Value, CoverageAtom>
                    || std::same_as<Value, CoverageCase>
                    || std::same_as<Value, CoverageOr>,
                "unhandled coverage pattern"
            );
            return value.type;
        },
        pattern.value
    );
}

auto children(const CoveragePattern& pattern) noexcept -> std::span<const CoveragePattern> {
    return std::visit(
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
            [](const CoverageAtom&) static noexcept -> std::span<const CoveragePattern> {
                return {};
            },
        },
        pattern.value
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

auto expand_head(const Matrix& source) noexcept -> Matrix {
    auto result = Matrix();
    const auto append = [&](this const auto& self, Row row) noexcept -> void {
        if (!row.empty()) {
            if (const auto* alternatives = std::get_if<CoverageOr>(&row.front().value)) {
                for (const auto& alternative : alternatives->alternatives) {
                    auto expanded = row;
                    expanded.front() = alternative;
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

auto specialize(const Matrix& source, const CoveragePattern& constructor) noexcept -> Matrix {
    auto result = Matrix();
    for (const auto& row : expand_head(source)) {
        if (row.empty()) {
            continue;
        }
        auto tail = Row(row.begin() + 1, row.end());
        if (std::holds_alternative<CoverageAny>(row.front().value)) {
            auto prefix = Row(children(constructor).begin(), children(constructor).end());
            prefix.insert(prefix.end(), tail.begin(), tail.end());
            result.push_back(std::move(prefix));
        } else if (same_constructor(row.front(), constructor)) {
            auto prefix = Row(children(row.front()).begin(), children(row.front()).end());
            prefix.insert(prefix.end(), tail.begin(), tail.end());
            result.push_back(std::move(prefix));
        }
    }
    return result;
}

auto defaults(const Matrix& source) noexcept -> Matrix {
    auto result = Matrix();
    for (const auto& row : expand_head(source)) {
        if (!row.empty() && std::holds_alternative<CoverageAny>(row.front().value)) {
            result.emplace_back(row.begin() + 1, row.end());
        }
    }
    return result;
}

class CoverageAnalyzer final {
public:
    CoverageAnalyzer(const ProgramDraft& source, const BodyBuilder& body_source) noexcept
        : draft(source),
          draft_body(std::addressof(body_source)) {}

    CoverageAnalyzer(const ProgramDraft& source, const SemIRBody& body_source) noexcept
        : draft(source),
          resolved_body(std::addressof(body_source)) {}

    auto run(ConstructionTypeRef subject_type, std::span<const PatternCoverageArm> arms) noexcept
        -> std::expected<PatternCoverage, std::string> {
        if (!owned(subject_type)) {
            return std::unexpected("coverage subject type belongs to another program");
        }

        auto matrix = Matrix();
        auto arm_usefulness = std::vector<bool>();
        auto alternative_usefulness = std::vector<std::vector<bool>>();
        auto redundant_alternatives = std::vector<CoverageRedundantAlternative>();
        auto exhaustive_after_arm = std::vector<bool>();
        arm_usefulness.reserve(arms.size());
        alternative_usefulness.reserve(arms.size());
        exhaustive_after_arm.reserve(arms.size());

        for (auto arm_index = 0uz; arm_index < arms.size(); ++arm_index) {
            const auto& arm = arms[arm_index];
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

            auto redundant = std::vector<bool>(lowered.size(), false);
            for (auto candidate = 0uz; candidate < lowered.size(); ++candidate) {
                for (auto other = 0uz; other < lowered.size(); ++other) {
                    if (candidate == other) {
                        continue;
                    }
                    auto candidate_after_other =
                        useful(Matrix {Row {lowered[other]}}, Row {lowered[candidate]});
                    if (!candidate_after_other.has_value()) {
                        return std::unexpected(std::move(candidate_after_other.error()));
                    }
                    auto other_after_candidate =
                        useful(Matrix {Row {lowered[candidate]}}, Row {lowered[other]});
                    if (!other_after_candidate.has_value()) {
                        return std::unexpected(std::move(other_after_candidate.error()));
                    }
                    const auto strictly_subsumed =
                        !*candidate_after_other && *other_after_candidate;
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
                        other_active.push_back(Row {lowered[other]});
                    }
                }
                auto candidate_useful = useful(other_active, Row {lowered[candidate]});
                if (!candidate_useful.has_value()) {
                    return std::unexpected(std::move(candidate_useful.error()));
                }
                redundant[candidate] = !*candidate_useful;
            }
            for (auto candidate = 0uz; candidate < lowered.size(); ++candidate) {
                if (redundant[candidate]) {
                    redundant_alternatives.push_back({
                        .arm = arm_index,
                        .alternative = candidate,
                    });
                }
            }

            auto source_usefulness = std::vector<bool>();
            source_usefulness.reserve(lowered.size());
            for (auto index = 0uz; index < lowered.size(); ++index) {
                auto alternative_useful = useful(matrix, Row {lowered[index]});
                if (!alternative_useful.has_value()) {
                    return std::unexpected(std::move(alternative_useful.error()));
                }
                source_usefulness.push_back(*alternative_useful && !redundant[index]);
            }
            alternative_usefulness.push_back(std::move(source_usefulness));

            auto candidate_pattern = lowered.size() == 1
                ? std::move(lowered.front())
                : CoveragePattern {
                      .value = CoverageOr {
                          .type = subject_type,
                          .alternatives = std::move(lowered),
                      },
                  };
            auto candidate = Row {std::move(candidate_pattern)};
            auto arm_useful = useful(matrix, candidate);
            if (!arm_useful.has_value()) {
                return std::unexpected(std::move(arm_useful.error()));
            }
            arm_usefulness.push_back(*arm_useful);
            if (!arm.guarded) {
                matrix.push_back(std::move(candidate));
            }

            auto missing =
                useful(matrix, Row {CoveragePattern {.value = CoverageAny {.type = subject_type}}});
            if (!missing.has_value()) {
                return std::unexpected(std::move(missing.error()));
            }
            exhaustive_after_arm.push_back(!*missing);
        }

        auto missing =
            useful(matrix, Row {CoveragePattern {.value = CoverageAny {.type = subject_type}}});
        if (!missing.has_value()) {
            return std::unexpected(std::move(missing.error()));
        }
        auto witness = missing_witness(matrix, subject_type, *missing);
        if (!witness.has_value()) {
            return std::unexpected(std::move(witness.error()));
        }
        return PatternCoverage {
            .arm_usefulness = std::move(arm_usefulness),
            .alternative_usefulness = std::move(alternative_usefulness),
            .redundant_alternatives = std::move(redundant_alternatives),
            .exhaustive_after_arm = std::move(exhaustive_after_arm),
            .exhaustive = !*missing,
            .missing_witness = std::move(*witness),
        };
    }

private:
    auto owned(ConstructionTypeRef type) const noexcept -> bool {
        return std::visit(
            [&]<typename ID>(ID id) noexcept {
                static_assert(std::same_as<ID, TypeID> || std::same_as<ID, TypeTermID>);
                return id.owner() == draft.identity();
            },
            type
        );
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
        return std::visit(
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
                    if (value.constant.owner() != draft.identity()) {
                        return std::unexpected(
                            "literal coverage constant belongs to another program"
                        );
                    }
                    const auto fact = draft.constant_copy(value.constant);
                    if (ConstructionTypeRef {fact.type} != expected_type) {
                        return std::unexpected(
                            "literal coverage constant type differs from its subject"
                        );
                    }
                    auto atom = std::variant<ConstantID, bool> {value.constant};
                    if (const auto* boolean = std::get_if<BooleanConstant>(&fact.value)) {
                        atom = boolean->value;
                    }
                    return CoveragePattern {
                        .value = CoverageAtom {
                            .type = expected_type,
                            .value = atom,
                        },
                    };
                },
                [&](const EnumCasePattern& value) -> std::expected<CoveragePattern, std::string> {
                    const auto* concrete = std::get_if<TypeID>(&expected_type);
                    if (concrete == nullptr) {
                        return std::unexpected("case coverage subject is not concrete");
                    }
                    const auto canonical = draft.type_copy(*concrete);
                    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
                    if (nominal == nullptr) {
                        return std::unexpected("case coverage subject is not an enum");
                    }
                    if (value.enum_case.owner() != draft.identity()) {
                        return std::unexpected("coverage enum case belongs to another program");
                    }
                    const auto member =
                        draft.construction_enum_case_declaration_copy(value.enum_case);
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
            },
            pattern.value
        );
    }

    auto lower(PatternID id, ConstructionTypeRef expected_type) noexcept
        -> std::expected<CoveragePattern, std::string> {
        if (id.owner().program() != draft.identity()) {
            return std::unexpected("coverage pattern belongs to another semantic program");
        }
        if (draft_body != nullptr) {
            return lower_pattern(draft_body->pattern_copy(id), expected_type);
        }
        if (resolved_body == nullptr) {
            return std::unexpected("coverage analyzer has no pattern authority");
        }
        return lower_pattern(resolved_body->pattern(id), expected_type);
    }

    auto constructors(ConstructionTypeRef type) noexcept
        -> std::expected<std::optional<std::vector<CoveragePattern>>, std::string> {
        if (!owned(type)) {
            return std::unexpected("coverage type belongs to another program");
        }
        const auto* concrete = std::get_if<TypeID>(&type);
        if (concrete == nullptr) {
            return std::optional<std::vector<CoveragePattern>>();
        }
        const auto canonical = draft.type_copy(*concrete);
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
            if (builtin->kind != BuiltinType::Bool) {
                return std::optional<std::vector<CoveragePattern>>();
            }
            return std::optional(
                std::vector<CoveragePattern> {
                    CoveragePattern {
                        .value = CoverageAtom {.type = type, .value = false},
                    },
                    CoveragePattern {
                        .value = CoverageAtom {.type = type, .value = true},
                    },
                }
            );
        }
        const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
        if (nominal == nullptr) {
            return std::optional<std::vector<CoveragePattern>>();
        }
        const auto enumeration = draft.construction_enum_declaration_copy(nominal->enumeration);
        auto result = std::vector<CoveragePattern>();
        result.reserve(enumeration.cases.size());
        for (const auto member_id : enumeration.cases) {
            const auto member = draft.construction_enum_case_declaration_copy(member_id);
            if (member.owner != nominal->enumeration) {
                return std::unexpected("coverage enum case has the wrong owner");
            }
            auto payload = std::vector<CoveragePattern>();
            payload.reserve(member.payload_types.size());
            for (const auto payload_type : member.payload_types) {
                if (!owned(payload_type)) {
                    return std::unexpected("coverage enum payload type belongs to another program");
                }
                payload.push_back({.value = CoverageAny {.type = payload_type}});
            }
            result.push_back({
                .value = CoverageCase {
                    .type = type,
                    .enum_case = member_id,
                    .payload = std::move(payload),
                },
            });
        }
        return std::optional(std::move(result));
    }

    auto useful(const Matrix& matrix, Row query) noexcept -> std::expected<bool, std::string> {
        if (query.empty()) {
            return matrix.empty();
        }
        if (const auto* alternatives = std::get_if<CoverageOr>(&query.front().value)) {
            for (const auto& alternative : alternatives->alternatives) {
                auto expanded = query;
                expanded.front() = alternative;
                auto found = useful(matrix, std::move(expanded));
                if (!found.has_value()) {
                    return found;
                }
                if (*found) {
                    return true;
                }
            }
            return false;
        }
        auto finite = constructors(pattern_type(query.front()));
        if (!finite.has_value()) {
            return std::unexpected(std::move(finite.error()));
        }
        if (std::holds_alternative<CoverageAny>(query.front().value)) {
            if (!finite->has_value()) {
                return useful(defaults(matrix), Row(query.begin() + 1, query.end()));
            }
            for (const auto& constructor : **finite) {
                auto specialized = Row(children(constructor).begin(), children(constructor).end());
                specialized.insert(specialized.end(), query.begin() + 1, query.end());
                auto found = useful(specialize(matrix, constructor), std::move(specialized));
                if (!found.has_value()) {
                    return found;
                }
                if (*found) {
                    return true;
                }
            }
            return false;
        }
        auto specialized = Row(children(query.front()).begin(), children(query.front()).end());
        specialized.insert(specialized.end(), query.begin() + 1, query.end());
        return useful(specialize(matrix, query.front()), std::move(specialized));
    }

    auto render_constructor(
        const CoveragePattern& constructor,
        std::span<const std::string> payload
    ) noexcept -> std::expected<std::string, std::string> {
        if (const auto* atom = std::get_if<CoverageAtom>(&constructor.value)) {
            const auto* boolean = std::get_if<bool>(&atom->value);
            if (boolean == nullptr) {
                return std::unexpected("finite coverage constructor is not renderable");
            }
            if (!payload.empty()) {
                return std::unexpected("atomic coverage constructor has a payload");
            }
            return *boolean ? "true" : "false";
        }
        const auto* value = std::get_if<CoverageCase>(&constructor.value);
        if (value == nullptr || value->payload.size() != payload.size()) {
            return std::unexpected("enum coverage constructor has the wrong witness arity");
        }
        const auto enum_case = draft.construction_enum_case_declaration_copy(value->enum_case);
        auto witness = std::format(".{}", draft.spelling_copy(enum_case.name));
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

    using WitnessRow = std::vector<std::string>;

    auto missing_row(const Matrix& matrix, std::span<const ConstructionTypeRef> types) noexcept
        -> std::expected<std::optional<WitnessRow>, std::string> {
        if (types.empty()) {
            if (matrix.empty()) {
                return std::optional(WitnessRow {});
            }
            return std::optional<WitnessRow>();
        }
        auto finite = constructors(types.front());
        if (!finite.has_value()) {
            return std::unexpected(std::move(finite.error()));
        }
        if (!finite->has_value()) {
            auto tail = missing_row(defaults(matrix), types.subspan(1uz));
            if (!tail.has_value() || !tail->has_value()) {
                return tail;
            }
            (*tail)->insert((*tail)->begin(), "_");
            return tail;
        }
        for (const auto& constructor : **finite) {
            auto specialized_types = std::vector<ConstructionTypeRef>();
            specialized_types.reserve(children(constructor).size() + types.size() - 1uz);
            for (const auto& child : children(constructor)) {
                specialized_types.push_back(pattern_type(child));
            }
            specialized_types.insert(specialized_types.end(), types.begin() + 1, types.end());
            auto specialized_witness =
                missing_row(specialize(matrix, constructor), specialized_types);
            if (!specialized_witness.has_value()) {
                return std::unexpected(std::move(specialized_witness.error()));
            }
            if (!specialized_witness->has_value()) {
                continue;
            }
            const auto arity = children(constructor).size();
            auto& row = **specialized_witness;
            if (row.size() < arity) {
                return std::unexpected("coverage witness row has the wrong arity");
            }
            auto head =
                render_constructor(constructor, std::span<const std::string>(row.data(), arity));
            if (!head.has_value()) {
                return std::unexpected(std::move(head.error()));
            }
            row.erase(row.begin(), row.begin() + static_cast<std::ptrdiff_t>(arity));
            row.insert(row.begin(), std::move(*head));
            return specialized_witness;
        }
        return std::optional<WitnessRow>();
    }

    auto missing_witness(
        const Matrix& matrix,
        ConstructionTypeRef subject_type,
        bool missing
    ) noexcept -> std::expected<std::string, std::string> {
        if (!missing) {
            return std::string("_");
        }
        const auto types = std::array {subject_type};
        auto witness = missing_row(matrix, types);
        if (!witness.has_value()) {
            return std::unexpected(std::move(witness.error()));
        }
        if (!witness->has_value() || (*witness)->size() != 1uz) {
            return std::unexpected("coverage witness search contradicted usefulness");
        }
        return std::move((*witness)->front());
    }

    const ProgramDraft& draft;
    const BodyBuilder* draft_body = nullptr;
    const SemIRBody* resolved_body = nullptr;
};

} // namespace

auto compute_pattern_coverage(
    const ProgramDraft& draft,
    const BodyBuilder& body,
    ConstructionTypeRef subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    return CoverageAnalyzer(draft, body).run(subject_type, arms);
}

auto compute_pattern_coverage(
    const ProgramDraft& draft,
    const SemIRBody& body,
    TypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    return CoverageAnalyzer(draft, body).run(ConstructionTypeRef {subject_type}, arms);
}
