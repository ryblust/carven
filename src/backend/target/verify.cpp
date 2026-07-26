module carven:backend.target.verify.impl;

import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.verify;
import :support.visit;
import std;

namespace {

struct JumpSite final {
    std::string label;
    TargetSyntheticControlKind kind;
    std::vector<std::size_t> barriers;
    std::size_t order;
};

struct LabelSite final {
    TargetSyntheticControlKind kind;
    std::vector<std::size_t> barriers;
    std::size_t order;
};

class SyntheticControlVerifier final {
public:
    explicit SyntheticControlVerifier(const TargetUnit& unit) noexcept
        : unit(unit) {}

    auto run() noexcept -> std::expected<void, TargetUnitError> {
        for (const auto& item : unit.items()) {
            if (const auto* declaration = std::get_if<TargetDecl>(&item.value);
                declaration != nullptr && !visit_declaration(*declaration)) {
                return std::unexpected(std::move(*failure));
            }
        }
        for (const auto& expression : unit.expressions()) {
            if (const auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value)) {
                if (!verify_body(lambda->body)) {
                    return std::unexpected(std::move(*failure));
                }
            } else if (const auto* closure = std::get_if<TargetClosureExpr>(&expression.value)) {
                if (!verify_body(closure->body)) {
                    return std::unexpected(std::move(*failure));
                }
            }
        }
        return {};
    }

private:
    auto fail(std::string message) noexcept -> bool {
        failure = TargetUnitError {.message = std::move(message)};
        return false;
    }

    auto verify_body(std::span<const TargetStmtID> body) noexcept -> bool {
        labels.clear();
        jumps.clear();
        next_barrier = 0;
        next_statement_order = 0;
        auto barriers = std::vector<std::size_t>();
        if (!walk_sequence(body, barriers, 0)) {
            return false;
        }
        for (const auto& jump : jumps) {
            const auto found = labels.find(jump.label);
            if (found == labels.end()) {
                return fail(
                    std::format("synthetic target jump references missing label '{}'", jump.label)
                );
            }
            if (jump.kind != found->second.kind) {
                return fail(
                    std::format(
                        "synthetic target jump '{}' and its label have different roles",
                        jump.label
                    )
                );
            }
            if (jump.order >= found->second.order) {
                return fail(std::format("synthetic target jump '{}' is not forward", jump.label));
            }
            if (!std::ranges::starts_with(jump.barriers, found->second.barriers)) {
                return fail(
                    std::format(
                        "synthetic target jump '{}' enters a scope or crosses an initialization",
                        jump.label
                    )
                );
            }
        }
        return true;
    }

    auto scoped(std::vector<std::size_t> barriers) noexcept -> std::vector<std::size_t> {
        barriers.push_back(next_barrier++);
        return barriers;
    }

    auto walk_sequence(
        std::span<const TargetStmtID> body,
        std::vector<std::size_t>& barriers,
        std::size_t loop_depth
    ) noexcept -> bool {
        for (const auto id : body) {
            if (!walk_statement(unit.statement(id), barriers, loop_depth)) {
                return false;
            }
        }
        return true;
    }

    auto walk_statement(
        const TargetStmt& statement,
        std::vector<std::size_t>& barriers,
        std::size_t loop_depth
    ) noexcept -> bool {
        const auto statement_order = next_statement_order++;
        return std::visit(
            Overloaded {
                [](const TargetExprStmt&) static noexcept { return true; },
                [](const TargetDiscardStmt&) static noexcept { return true; },
                [](const TargetReturnStmt&) static noexcept { return true; },
                [&](const TargetVariableStmt&) noexcept {
                    barriers.push_back(next_barrier++);
                    return true;
                },
                [&](const TargetBlockStmt& value) noexcept {
                    auto nested = value.scoped ? scoped(barriers) : barriers;
                    return walk_sequence(value.statements, nested, loop_depth);
                },
                [](const TargetAssignmentStmt&) static noexcept { return true; },
                [](const TargetUpdateStmt&) static noexcept { return true; },
                [&](const TargetBreakStmt&) noexcept {
                    return loop_depth != 0 || fail("target break is outside a loop");
                },
                [&](const TargetContinueStmt&) noexcept {
                    return loop_depth != 0 || fail("target continue is outside a loop");
                },
                [&](const TargetGotoStmt& value) noexcept {
                    if (value.kind == TargetSyntheticControlKind::NormalizedForContinue
                        && loop_depth == 0) {
                        return fail("normalized-for continue jump is outside a loop");
                    }
                    jumps.push_back({
                        .label = std::string(value.label.spelling()),
                        .kind = value.kind,
                        .barriers = barriers,
                        .order = statement_order,
                    });
                    return true;
                },
                [&](const TargetLabelStmt& value) noexcept {
                    if (value.kind == TargetSyntheticControlKind::NormalizedForContinue
                        && loop_depth == 0) {
                        return fail("normalized-for continue label is outside a loop");
                    }
                    const auto label = std::string(value.label.spelling());
                    return labels
                               .emplace(
                                   label,
                                   LabelSite {
                                       .kind = value.kind,
                                       .barriers = barriers,
                                       .order = statement_order,
                                   }
                               )
                               .second
                        || fail(std::format("synthetic target label '{}' is repeated", label));
                },
                [&](const TargetIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        auto nested = scoped(barriers);
                        if (!walk_sequence(branch.body, nested, loop_depth)) {
                            return false;
                        }
                    }
                    if (value.else_body.has_value()) {
                        auto nested = scoped(barriers);
                        if (!walk_sequence(*value.else_body, nested, loop_depth)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const TargetWhileStmt& value) noexcept {
                    auto nested = scoped(barriers);
                    return walk_sequence(value.body, nested, loop_depth + 1);
                },
                [&](const TargetForStmt& value) noexcept {
                    auto nested = scoped(barriers);
                    if (value.initializer.has_value()
                        && !walk_statement(
                            unit.statement(*value.initializer),
                            nested,
                            loop_depth + 1
                        )) {
                        return false;
                    }
                    for (const auto step : value.steps) {
                        if (!walk_statement(unit.statement(step), nested, loop_depth + 1)) {
                            return false;
                        }
                    }
                    return walk_sequence(value.body, nested, loop_depth + 1);
                },
                [&](const TargetRangeForStmt& value) noexcept {
                    auto nested = scoped(barriers);
                    nested.push_back(next_barrier++);
                    return walk_sequence(value.body, nested, loop_depth + 1);
                },
                [](const TargetRawFragment&) static noexcept { return true; },
            },
            statement.value
        );
    }

    auto visit_member(const TargetRecordMember& member) noexcept -> bool {
        const auto* function = std::get_if<TargetMemberFunctionDecl>(&member);
        return function == nullptr
            || function->declaration_only
            || function->defaulted
            || verify_body(function->body);
    }

    auto visit_declaration(const TargetDecl& declaration) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetFunctionDecl& value) noexcept {
                    return value.declaration_only || verify_body(value.body);
                },
                [&](const TargetStructDecl& value) noexcept {
                    return std::ranges::all_of(value.members, [&](const auto& member) noexcept {
                        return visit_member(member);
                    });
                },
                [](const TargetStructForwardDecl&) static noexcept { return true; },
                [](const TargetEnumDecl&) static noexcept { return true; },
                [](const TargetEnumForwardDecl&) static noexcept { return true; },
                [&](const TargetClassDecl& value) noexcept {
                    for (const auto& section : value.sections) {
                        for (const auto& member : section.members) {
                            const auto valid = std::visit(
                                Overloaded {
                                    [&](const TargetNestedRecord& record) noexcept {
                                        return std::ranges::all_of(
                                            record.members,
                                            [&](const auto& nested) noexcept {
                                                return visit_member(nested);
                                            }
                                        );
                                    },
                                    [&](const TargetMemberFunctionDecl& function) noexcept {
                                        return function.declaration_only
                                            || function.defaulted
                                            || verify_body(function.body);
                                    },
                                    [](const auto&) static noexcept { return true; },
                                },
                                member
                            );
                            if (!valid) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                [](const TargetClassForwardDecl&) static noexcept { return true; },
                [](const TargetVariableDecl&) static noexcept { return true; },
            },
            declaration
        );
    }

    const TargetUnit& unit;
    std::flat_map<std::string, LabelSite> labels;
    std::vector<JumpSite> jumps;
    std::size_t next_barrier = 0;
    std::size_t next_statement_order = 0;
    std::optional<TargetUnitError> failure;
};

} // namespace

auto verify_target_unit(const TargetUnit& unit) noexcept -> std::expected<void, TargetUnitError> {
    return SyntheticControlVerifier(unit).run();
}
