module carven:semantic.analysis.coverage.impl;

import :semantic.analysis.builder;
import :semantic.analysis.coverage;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {
struct CoverageEnumView final {
    std::span<const EnumCaseID> cases;
};

struct CoverageEnumCaseView final {
    EnumID owner;
    ProgramSpellingID name;
    std::span<const HIRTypeID> payload_types;
};

class PublishedCoverageDeclarations final {
public:
    explicit PublishedCoverageDeclarations(const SemanticConstruction& source) noexcept
        : hir(source) {}

    auto enum_count() const noexcept -> std::size_t { return hir.enumerations().size(); }
    auto enum_case_count() const noexcept -> std::size_t { return hir.enum_cases().size(); }

    auto enumeration(EnumID id) const noexcept -> CoverageEnumView {
        return {.cases = hir.enumeration(id).cases};
    }

    auto enum_case(EnumCaseID id) const noexcept -> CoverageEnumCaseView {
        const auto& value = hir.enum_case(id);
        return {
            .owner = value.owner,
            .name = value.name,
            .payload_types = value.payload_types,
        };
    }

private:
    const SemanticConstruction& hir;
};

class SessionCoverageDeclarations final {
public:
    explicit SessionCoverageDeclarations(const DeclarationSessionView& source) noexcept
        : session(source) {}

    auto enum_count() const noexcept -> std::size_t { return session.enum_count(); }
    auto enum_case_count() const noexcept -> std::size_t { return session.enum_case_count(); }

    auto enumeration(EnumID id) const noexcept -> CoverageEnumView {
        return {.cases = session.enumeration(id).cases};
    }

    auto enum_case(EnumCaseID id) const noexcept -> CoverageEnumCaseView {
        const auto value = session.enum_case(id);
        return {
            .owner = value.owner,
            .name = value.name,
            .payload_types = value.payload_types,
        };
    }

private:
    const DeclarationSessionView& session;
};

struct CoverageInteger final {
    std::uint64_t magnitude;
    bool negative;
    constexpr auto operator==(const CoverageInteger&) const noexcept -> bool = default;
};

using CoverageAtom =
    std::variant<CoverageInteger, double, bool, char32_t, ProgramSpellingID, HIRTypeID>;

struct CoveragePattern;

struct CoverageAny final {
    HIRTypeID type;
};
struct CoverageAtomPattern final {
    HIRTypeID type;
    CoverageAtom value;
};
struct CoverageCase final {
    HIRTypeID type;
    EnumCaseID enum_case;
    std::vector<CoveragePattern> payload;
};
struct CoverageOr final {
    HIRTypeID type;
    std::vector<CoveragePattern> alternatives;
};

struct CoveragePattern final {
    std::variant<CoverageAny, CoverageAtomPattern, CoverageCase, CoverageOr> value;
};

using Row = std::vector<CoveragePattern>;
using Matrix = std::vector<Row>;

auto pattern_type(const CoveragePattern& pattern) noexcept -> HIRTypeID {
    return std::visit([](const auto& value) static noexcept { return value.type; }, pattern.value);
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
            [](const auto&) static noexcept -> std::span<const CoveragePattern> { return {}; },
        },
        pattern.value
    );
}

auto same_atom(const CoverageAtom& left, const CoverageAtom& right) noexcept -> bool {
    if (left.index() != right.index()) {
        return false;
    }
    return std::visit(
        [&]<typename Value>(const Value& value) noexcept {
            return value == std::get<Value>(right);
        },
        left
    );
}

auto same_constructor(const CoveragePattern& left, const CoveragePattern& right) noexcept -> bool {
    if (const auto* left_case = std::get_if<CoverageCase>(&left.value)) {
        const auto* right_case = std::get_if<CoverageCase>(&right.value);
        return right_case != nullptr && left_case->enum_case == right_case->enum_case;
    }
    if (const auto* left_atom = std::get_if<CoverageAtomPattern>(&left.value)) {
        const auto* right_atom = std::get_if<CoverageAtomPattern>(&right.value);
        return right_atom != nullptr && same_atom(left_atom->value, right_atom->value);
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
template<typename Declarations>
auto compute_pattern_coverage_impl(
    const SemanticConstruction& hir,
    const Declarations& declarations,
    HIRTypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    const auto valid_type = [&](HIRTypeID id) noexcept {
        return id.index() < hir.types().size();
    };
    const auto valid_pattern = [&](HIRPatternID id) noexcept {
        return id.index() < hir.patterns().size();
    };
    const auto valid_enum_case = [&](EnumCaseID id) noexcept {
        return id.index() < declarations.enum_case_count();
    };
    if (!valid_type(subject_type)) {
        return std::unexpected("coverage subject type is out of range");
    }

    const auto lower = [&](this const auto& self,
                           HIRPatternID id,
                           HIRTypeID expected_type) -> std::expected<CoveragePattern, std::string> {
        if (!valid_pattern(id)) {
            return std::unexpected("coverage pattern is out of range");
        }
        return std::visit(
            Overloaded {
                [&](const HIRWildcardPattern&) -> std::expected<CoveragePattern, std::string> {
                    return CoveragePattern {
                        .value = CoverageAny {.type = expected_type},
                    };
                },
                [&](const HIRBindingPattern& value) -> std::expected<CoveragePattern, std::string> {
                    if (!valid_type(value.type)) {
                        return std::unexpected("binding coverage type is out of range");
                    }
                    return CoveragePattern {
                        .value = CoverageAny {.type = value.type},
                    };
                },
                [&](const HIRLiteralPattern& value) -> std::expected<CoveragePattern, std::string> {
                    if (!valid_type(value.type)) {
                        return std::unexpected("literal coverage type is out of range");
                    }
                    const auto atom = std::visit(
                        Overloaded {
                            [](const HIRIntegerLiteralValue& literal) static noexcept
                                -> CoverageAtom {
                                return CoverageInteger {
                                    .magnitude = literal.magnitude,
                                    .negative = literal.negative,
                                };
                            },
                            [](const HIRF32LiteralValue& literal) static noexcept -> CoverageAtom {
                                return static_cast<double>(literal.value);
                            },
                            [](const HIRF64LiteralValue& literal) static noexcept -> CoverageAtom {
                                return literal.value;
                            },
                            [](const HIRBooleanLiteralValue& literal) static noexcept
                                -> CoverageAtom { return literal.value; },
                            [](const HIRCharacterLiteralValue& literal) static noexcept
                                -> CoverageAtom { return literal.scalar; },
                            [](const HIRStrLiteralValue& literal) static noexcept -> CoverageAtom {
                                return literal.bytes;
                            },
                        },
                        value.literal
                    );
                    return CoveragePattern {
                        .value = CoverageAtomPattern {
                            .type = value.type,
                            .value = atom,
                        },
                    };
                },
                [&](const HIRTypeConstraintPattern& value)
                    -> std::expected<CoveragePattern, std::string> {
                    if (!valid_type(value.type)) {
                        return std::unexpected("constraint coverage type is out of range");
                    }
                    return CoveragePattern {
                        .value = CoverageAtomPattern {
                            .type = value.type,
                            .value = CoverageAtom {value.type},
                        },
                    };
                },
                [&](const HIRCasePattern& value) -> std::expected<CoveragePattern, std::string> {
                    if (!valid_type(expected_type) || !valid_enum_case(value.enum_case)) {
                        return std::unexpected("case coverage pattern is malformed");
                    }
                    const auto* enumeration =
                        std::get_if<HIREnumTypeValue>(&hir.type(expected_type).value);
                    if (enumeration == nullptr
                        || enumeration->enumeration.index() >= declarations.enum_count()) {
                        return std::unexpected("case coverage type is not an enum");
                    }
                    const auto member = declarations.enum_case(value.enum_case);
                    if (member.owner != enumeration->enumeration
                        || member.payload_types.size() != value.payload.size()) {
                        return std::unexpected("case coverage payload does not match its member");
                    }
                    auto payload = std::vector<CoveragePattern>();
                    for (auto index = 0uz; index < value.payload.size(); ++index) {
                        auto lowered = self(value.payload[index], member.payload_types[index]);
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
                [&](const HIROrPattern& value) -> std::expected<CoveragePattern, std::string> {
                    if (!valid_type(value.type)) {
                        return std::unexpected("or-pattern coverage type is out of range");
                    }
                    auto alternatives = std::vector<CoveragePattern>();
                    for (const auto child : value.alternatives) {
                        auto lowered = self(child, value.type);
                        if (!lowered.has_value()) {
                            return std::unexpected(std::move(lowered.error()));
                        }
                        alternatives.push_back(std::move(*lowered));
                    }
                    return CoveragePattern {
                        .value = CoverageOr {
                            .type = value.type,
                            .alternatives = std::move(alternatives),
                        },
                    };
                },
            },
            hir.pattern(id).value
        );
    };

    const auto constructors =
        [&](HIRTypeID type) -> std::expected<std::vector<CoveragePattern>, std::string> {
        if (!valid_type(type)) {
            return std::unexpected("coverage type is out of range");
        }
        const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&hir.type(type).value);
        if (builtin != nullptr && builtin->kind == HIRBuiltinType::Bool) {
            return std::vector<CoveragePattern> {
                CoveragePattern {
                    .value =
                        CoverageAtomPattern {
                            .type = type,
                            .value = CoverageAtom {false},
                        },
                },
                CoveragePattern {
                    .value = CoverageAtomPattern {
                        .type = type,
                        .value = CoverageAtom {true},
                    },
                },
            };
        }
        const auto* nominal = std::get_if<HIREnumTypeValue>(&hir.type(type).value);
        if (nominal == nullptr) {
            return std::vector<CoveragePattern>();
        }
        if (nominal->enumeration.index() >= declarations.enum_count()) {
            return std::unexpected("coverage enum identity is out of range");
        }
        const auto enumeration = declarations.enumeration(nominal->enumeration);
        auto result = std::vector<CoveragePattern>();
        for (const auto member_id : enumeration.cases) {
            if (!valid_enum_case(member_id)) {
                return std::unexpected("coverage enum case identity is out of range");
            }
            const auto member = declarations.enum_case(member_id);
            if (member.owner != nominal->enumeration) {
                return std::unexpected("coverage enum case has the wrong owner");
            }
            auto payload = std::vector<CoveragePattern>();
            for (const auto payload_type : member.payload_types) {
                if (!valid_type(payload_type)) {
                    return std::unexpected("coverage enum payload type is out of range");
                }
                payload.push_back(
                    CoveragePattern {
                        .value = CoverageAny {.type = payload_type},
                    }
                );
            }
            result.push_back(
                CoveragePattern {
                    .value = CoverageCase {
                        .type = type,
                        .enum_case = member_id,
                        .payload = std::move(payload),
                    },
                }
            );
        }
        return result;
    };

    const auto useful = [&](this const auto& self,
                            const Matrix& matrix,
                            Row query) -> std::expected<bool, std::string> {
        if (query.empty()) {
            return matrix.empty();
        }
        if (const auto* alternatives = std::get_if<CoverageOr>(&query.front().value)) {
            for (const auto& alternative : alternatives->alternatives) {
                auto expanded = query;
                expanded.front() = alternative;
                auto found = self(matrix, std::move(expanded));
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
            if (finite->empty()) {
                return self(defaults(matrix), Row(query.begin() + 1, query.end()));
            }
            for (const auto& constructor : *finite) {
                auto specialized = Row(children(constructor).begin(), children(constructor).end());
                specialized.insert(specialized.end(), query.begin() + 1, query.end());
                auto found = self(specialize(matrix, constructor), std::move(specialized));
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
        return self(specialize(matrix, query.front()), std::move(specialized));
    };

    auto matrix = Matrix();
    auto arm_usefulness = std::vector<bool>();
    auto alternative_usefulness = std::vector<std::vector<bool>>();
    auto redundant_alternatives = std::vector<CoverageRedundantAlternative>();
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
        auto source_usefulness = std::vector<bool>();
        source_usefulness.reserve(lowered.size());
        for (const auto& alternative : lowered) {
            auto alternative_useful = useful(matrix, Row {alternative});
            if (!alternative_useful.has_value()) {
                return std::unexpected(std::move(alternative_useful.error()));
            }
            source_usefulness.push_back(*alternative_useful);
        }
        alternative_usefulness.push_back(std::move(source_usefulness));

        for (auto candidate = 0uz; candidate < lowered.size(); ++candidate) {
            for (auto covering = 0uz; covering < lowered.size(); ++covering) {
                if (candidate == covering) {
                    continue;
                }
                auto candidate_useful =
                    useful(Matrix {Row {lowered[covering]}}, Row {lowered[candidate]});
                if (!candidate_useful.has_value()) {
                    return std::unexpected(std::move(candidate_useful.error()));
                }
                if (*candidate_useful) {
                    continue;
                }
                auto covering_useful =
                    useful(Matrix {Row {lowered[candidate]}}, Row {lowered[covering]});
                if (!covering_useful.has_value()) {
                    return std::unexpected(std::move(covering_useful.error()));
                }
                const auto strictly_subsumed = *covering_useful;
                const auto repeated_after_first = !strictly_subsumed && covering < candidate;
                if (strictly_subsumed || repeated_after_first) {
                    redundant_alternatives.push_back({
                        .arm = arm_index,
                        .alternative = candidate,
                    });
                    break;
                }
            }
        }
        auto candidate_pattern = lowered.size() == 1 ? std::move(lowered.front())
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
    }

    auto missing = useful(
        matrix,
        Row {CoveragePattern {
            .value = CoverageAny {.type = subject_type},
        }}
    );
    if (!missing.has_value()) {
        return std::unexpected(std::move(missing.error()));
    }
    auto witness = std::string("_");
    if (*missing) {
        auto finite = constructors(subject_type);
        if (!finite.has_value()) {
            return std::unexpected(std::move(finite.error()));
        }
        for (const auto& constructor : *finite) {
            auto query = Row(children(constructor).begin(), children(constructor).end());
            auto constructor_missing = useful(specialize(matrix, constructor), std::move(query));
            if (!constructor_missing.has_value()) {
                return std::unexpected(std::move(constructor_missing.error()));
            }
            if (!*constructor_missing) {
                continue;
            }
            if (const auto* atom = std::get_if<CoverageAtomPattern>(&constructor.value)) {
                if (const auto* boolean = std::get_if<bool>(&atom->value)) {
                    witness = *boolean ? "true" : "false";
                }
            } else if (const auto* value = std::get_if<CoverageCase>(&constructor.value)) {
                if (!valid_enum_case(value->enum_case)) {
                    return std::unexpected("coverage witness enum case is out of range");
                }
                const auto enum_case = declarations.enum_case(value->enum_case);
                if (enum_case.name.index() >= hir.provenance().spellings().size()) {
                    return std::unexpected("coverage witness symbol name is out of range");
                }
                witness = std::format(".{}", hir.provenance().spelling(enum_case.name));
                if (!value->payload.empty()) {
                    witness += '(';
                    for (auto index = 0uz; index < value->payload.size(); ++index) {
                        if (index != 0) {
                            witness += ", ";
                        }
                        witness += '_';
                    }
                    witness += ')';
                }
            }
            break;
        }
    }
    return PatternCoverage {
        .arm_usefulness = std::move(arm_usefulness),
        .alternative_usefulness = std::move(alternative_usefulness),
        .redundant_alternatives = std::move(redundant_alternatives),
        .exhaustive = !*missing,
        .missing_witness = std::move(witness),
    };
}

} // namespace

namespace {

auto coverage_arms(const SemanticConstruction& hir, std::span<const HIRMatchArm> arms) noexcept
    -> std::vector<PatternCoverageArm> {
    auto result = std::vector<PatternCoverageArm>();
    result.reserve(arms.size());
    for (const auto& arm : arms) {
        auto alternatives = std::vector<std::optional<HIRPatternID>>();
        if (const auto* pattern = std::get_if<HIROrPattern>(&hir.pattern(arm.pattern).value)) {
            alternatives.reserve(pattern->alternatives.size());
            for (const auto alternative : pattern->alternatives) {
                alternatives.push_back(alternative);
            }
        } else {
            alternatives.push_back(arm.pattern);
        }
        result.push_back({
            .alternatives = std::move(alternatives),
            .guarded = arm.guard.has_value(),
        });
    }
    return result;
}

} // namespace

auto compute_pattern_coverage(
    const SemanticConstruction& hir,
    HIRTypeID subject_type,
    std::span<const HIRMatchArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    const auto queries = coverage_arms(hir, arms);
    return compute_pattern_coverage_impl(
        hir,
        PublishedCoverageDeclarations(hir),
        subject_type,
        queries
    );
}

auto compute_pattern_coverage(
    const SemanticConstruction& hir,
    HIRTypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    return compute_pattern_coverage_impl(
        hir,
        PublishedCoverageDeclarations(hir),
        subject_type,
        arms
    );
}

auto compute_pattern_coverage(
    const SemanticConstruction& hir,
    const DeclarationSessionView& declarations,
    HIRTypeID subject_type,
    std::span<const HIRMatchArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    const auto queries = coverage_arms(hir, arms);
    return compute_pattern_coverage_impl(
        hir,
        SessionCoverageDeclarations(declarations),
        subject_type,
        queries
    );
}

auto compute_pattern_coverage(
    const SemanticConstruction& hir,
    const DeclarationSessionView& declarations,
    HIRTypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string> {
    return compute_pattern_coverage_impl(
        hir,
        SessionCoverageDeclarations(declarations),
        subject_type,
        arms
    );
}
