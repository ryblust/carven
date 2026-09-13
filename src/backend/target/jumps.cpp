module carven:backend.target.jumps.impl;

import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target.unit;
import :backend.target.verify;
import :support.visit;
import std;

namespace {

struct JumpSite final {
    std::string label;
    TargetJumpRole role;
    std::vector<std::size_t> barriers;
    std::size_t order;
};

struct LabelSite final {
    TargetJumpRole role;
    std::vector<std::size_t> barriers;
    std::size_t order;
};

struct ScopeFrame final {
    std::size_t barrier_size;
    std::size_t loop_depth;
};

struct CallableState final {
    std::flat_map<std::string, LabelSite> labels;
    std::vector<JumpSite> jumps;
    std::vector<std::size_t> barriers;
    std::vector<ScopeFrame> scopes;
    std::size_t loop_depth = 0;
    std::size_t next_barrier = 0;
    std::size_t next_statement_order = 0;
    std::size_t current_statement_order = 0;
};

class JumpVerifier final {
public:
    explicit JumpVerifier(const TargetUnitSections& source) noexcept;
    auto run() noexcept -> std::expected<void, TargetSealViolation>;
    auto enter_scope(TargetTraversalScope scope) noexcept -> bool;
    auto leave_scope(TargetTraversalScope scope) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;

private:
    auto fail(std::string message) noexcept -> bool;
    auto verify_callable(const CallableState& state) noexcept -> bool;

    const TargetUnitSections& sections;
    std::vector<CallableState> callables;
    std::optional<TargetSealViolation> failure;
};

JumpVerifier::JumpVerifier(const TargetUnitSections& source) noexcept
    : sections(source) {}

auto JumpVerifier::run() noexcept -> std::expected<void, TargetSealViolation> {
    if (!traverse_target_unit(sections, *this)) {
        return std::unexpected(std::move(*failure));
    }
    if (!callables.empty()) {
        return std::unexpected(
            TargetSealViolation {
                .kind = TargetSealViolationKind::InvalidControl,
                .message = "target traversal left a callable scope open",
            }
        );
    }
    return {};
}

auto JumpVerifier::enter_scope(TargetTraversalScope scope) noexcept -> bool {
    if (scope.kind == TargetTraversalScopeKind::Callable) {
        callables.emplace_back();
        return true;
    }
    if (callables.empty() || scope.kind == TargetTraversalScopeKind::Namespace) {
        return true;
    }
    auto& state = callables.back();
    state.scopes.push_back({
        .barrier_size = state.barriers.size(),
        .loop_depth = state.loop_depth,
    });
    if (scope.initialization_barrier) {
        state.barriers.push_back(state.next_barrier++);
    }
    if (scope.kind == TargetTraversalScopeKind::Loop) {
        ++state.loop_depth;
    }
    return true;
}

auto JumpVerifier::leave_scope(TargetTraversalScope scope) noexcept -> bool {
    if (scope.kind == TargetTraversalScopeKind::Callable) {
        if (callables.empty()) {
            return fail("target traversal left an unknown callable scope");
        }
        if (!verify_callable(callables.back())) {
            return false;
        }
        callables.pop_back();
        return true;
    }
    if (callables.empty() || scope.kind == TargetTraversalScopeKind::Namespace) {
        return true;
    }
    auto& state = callables.back();
    if (state.scopes.empty()) {
        return fail("target traversal left an unknown statement scope");
    }
    const auto frame = state.scopes.back();
    state.scopes.pop_back();
    state.barriers.resize(frame.barrier_size);
    state.loop_depth = frame.loop_depth;
    return true;
}

auto JumpVerifier::enter_statement(const TargetStmt& statement) noexcept -> bool {
    if (callables.empty()) {
        return fail("target statement is outside a callable body");
    }
    auto& state = callables.back();
    state.current_statement_order = state.next_statement_order++;
    return std::visit(
        Overloaded {
            [](const TargetExprStmt&) static noexcept { return true; },
            [](const TargetDiscardStmt&) static noexcept { return true; },
            [](const TargetReturnStmt&) static noexcept { return true; },
            [&](const TargetVariableStmt&) noexcept {
                state.barriers.push_back(state.next_barrier++);
                return true;
            },
            [](const TargetBlockStmt&) static noexcept { return true; },
            [](const TargetAssignmentStmt&) static noexcept { return true; },
            [](const TargetUpdateStmt&) static noexcept { return true; },
            [&](const TargetBreakStmt&) noexcept {
                return state.loop_depth != 0 || fail("target break is outside a loop");
            },
            [&](const TargetContinueStmt&) noexcept {
                return state.loop_depth != 0 || fail("target continue is outside a loop");
            },
            [](const TargetUnreachableStmt&) static noexcept { return true; },
            [](const TargetRuntimeTrapStmt&) static noexcept { return true; },
            [&](const TargetGotoStmt& value) noexcept {
                if (value.role == TargetJumpRole::ForLoopContinue && state.loop_depth == 0) {
                    return fail("normalized-for continue jump is outside a loop");
                }
                state.jumps.push_back({
                    .label = std::string(value.label.spelling()),
                    .role = value.role,
                    .barriers = state.barriers,
                    .order = state.current_statement_order,
                });
                return true;
            },
            [&](const TargetLabelStmt& value) noexcept {
                if (value.role == TargetJumpRole::ForLoopContinue && state.loop_depth == 0) {
                    return fail("normalized-for continue label is outside a loop");
                }
                const auto label = std::string(value.label.spelling());
                return state.labels
                           .emplace(
                               label,
                               LabelSite {
                                   .role = value.role,
                                   .barriers = state.barriers,
                                   .order = state.current_statement_order,
                               }
                           )
                           .second
                    || fail(std::format("synthetic target label '{}' is repeated", label));
            },
            [](const TargetIfStmt&) static noexcept { return true; },
            [](const TargetWhileStmt&) static noexcept { return true; },
            [](const TargetForStmt&) static noexcept { return true; },
            [](const TargetRangeForStmt&) static noexcept { return true; },
        },
        statement.value
    );
}

auto JumpVerifier::fail(std::string message) noexcept -> bool {
    failure = TargetSealViolation {
        .kind = TargetSealViolationKind::InvalidControl,
        .message = std::move(message),
    };
    return false;
}

auto JumpVerifier::verify_callable(const CallableState& state) noexcept -> bool {
    if (!state.scopes.empty()) {
        return fail("target callable ended with an open statement scope");
    }
    for (const auto& jump : state.jumps) {
        const auto found = state.labels.find(jump.label);
        if (found == state.labels.end()) {
            return fail(
                std::format("synthetic target jump references missing label '{}'", jump.label)
            );
        }
        if (jump.role != found->second.role) {
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

} // namespace

auto validate_jumps(const TargetUnitSections& sections) noexcept
    -> std::expected<void, TargetSealViolation> {
    return JumpVerifier(sections).run();
}
