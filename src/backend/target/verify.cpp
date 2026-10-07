module carven:backend.target.verify.impl;

import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import :support.visit;
import std;

namespace {

class TargetTypeVerifier final {
public:
    TargetTypeVerifier(
        TargetUnitIdentity unit_identity,
        std::span<const TargetType> type_values,
        const TargetUnitSections& unit_sections
    ) noexcept;
    auto run() noexcept -> std::expected<void, TargetSealViolation>;
    auto visit_type(TargetTypeID id) noexcept -> bool;

private:
    auto fail(TargetSealViolationKind kind, std::string message) noexcept -> bool;

    TargetUnitIdentity identity;
    std::span<const TargetType> types;
    const TargetUnitSections& sections;
    std::optional<TargetSealViolation> failure;
};

TargetTypeVerifier::TargetTypeVerifier(
    TargetUnitIdentity unit_identity,
    std::span<const TargetType> type_values,
    const TargetUnitSections& unit_sections
) noexcept
    : identity(unit_identity),
      types(type_values),
      sections(unit_sections) {}

auto TargetTypeVerifier::run() noexcept -> std::expected<void, TargetSealViolation> {
    if (!traverse_target_unit(sections, *this)) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

auto TargetTypeVerifier::visit_type(TargetTypeID id) noexcept -> bool {
    if (id.owner() != identity || id.index() >= types.size()) {
        return fail(
            TargetSealViolationKind::InvalidTypeReference,
            std::format("target type ID {} is foreign or out of range", id.index())
        );
    }
    return true;
}

auto TargetTypeVerifier::fail(TargetSealViolationKind kind, std::string message) noexcept -> bool {
    failure = TargetSealViolation {.kind = kind, .message = std::move(message)};
    return false;
}

class TargetLocalVerifier final {
public:
    TargetLocalVerifier(TargetUnitIdentity owner, std::size_t count) noexcept;
    auto run(const TargetUnitSections& sections) noexcept
        -> std::expected<void, TargetSealViolation>;
    auto enter_scope(TargetTraversalScope) noexcept -> bool;
    auto leave_scope(TargetTraversalScope) noexcept -> bool;
    auto visit_parameter_id(std::optional<TargetLocalID> id) noexcept -> bool;
    auto visit_local_parameter(TargetLocalID id) noexcept -> bool;

    auto visit_variable(const auto& variable) noexcept -> bool { return declare(variable.local); }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;

private:
    auto valid(TargetLocalID id) const noexcept -> bool;
    auto declare(TargetLocalID id) noexcept -> bool;

    TargetUnitIdentity identity;
    std::size_t local_count;
    std::vector<std::vector<TargetLocalID>> scopes;
    std::flat_set<TargetLocalID> declared;
    std::flat_set<TargetLocalID> visible;
    std::string failure;
};

TargetLocalVerifier::TargetLocalVerifier(TargetUnitIdentity owner, std::size_t count) noexcept
    : identity(owner),
      local_count(count) {}

auto TargetLocalVerifier::run(const TargetUnitSections& sections) noexcept
    -> std::expected<void, TargetSealViolation> {
    if (!traverse_target_unit(sections, *this)) {
        return std::unexpected(
            TargetSealViolation {
                .kind = TargetSealViolationKind::InvalidLocalReference,
                .message = std::move(failure),
            }
        );
    }
    return {};
}

auto TargetLocalVerifier::enter_scope(TargetTraversalScope) noexcept -> bool {
    scopes.emplace_back();
    return true;
}

auto TargetLocalVerifier::leave_scope(TargetTraversalScope) noexcept -> bool {
    for (const auto id : scopes.back()) {
        visible.erase(id);
    }
    scopes.pop_back();
    return true;
}

auto TargetLocalVerifier::visit_parameter_id(std::optional<TargetLocalID> id) noexcept -> bool {
    if (id.has_value() && !valid(*id)) {
        failure = "target parameter identity is foreign or invalid";
        return false;
    }
    return true;
}

auto TargetLocalVerifier::visit_local_parameter(TargetLocalID id) noexcept -> bool {
    return declare(id);
}

auto TargetLocalVerifier::enter_expression(
    const TargetExpr& expression,
    TargetExpressionRole
) noexcept -> bool {
    const auto* local = std::get_if<TargetLocalExpr>(&expression.value);
    if (local == nullptr) {
        return true;
    }
    if (!valid(local->local) || !visible.contains(local->local)) {
        failure = "target local reference is foreign, invalid, or outside its scope";
        return false;
    }
    return true;
}

auto TargetLocalVerifier::valid(TargetLocalID id) const noexcept -> bool {
    return id.owner() == identity && id.index() < local_count;
}

auto TargetLocalVerifier::declare(TargetLocalID id) noexcept -> bool {
    if (!valid(id) || scopes.empty() || !declared.insert(id).second) {
        failure = "target local declaration is foreign, invalid, or duplicated";
        return false;
    }
    scopes.back().push_back(id);
    visible.insert(id);
    return true;
}

class TargetCoroutineVerifier final {
public:
    auto run(const TargetUnitSections& sections) noexcept
        -> std::expected<void, TargetSealViolation>;
    auto enter_scope(TargetTraversalScope scope) noexcept -> bool;
    auto leave_scope(TargetTraversalScope scope) noexcept -> bool;
    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
    auto enter_statement(const TargetStmt& statement) noexcept -> bool;
    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;

private:
    auto fail(std::string message) noexcept -> bool;

    struct CallableFrame final {
        bool coroutine;
        bool native_operation;
    };

    std::vector<CallableFrame> callables;
    std::string failure;
};

auto TargetCoroutineVerifier::run(const TargetUnitSections& sections) noexcept
    -> std::expected<void, TargetSealViolation> {
    if (!traverse_target_unit(sections, *this)) {
        return std::unexpected(
            TargetSealViolation {
                .kind = TargetSealViolationKind::InvalidCoroutine,
                .message = std::move(failure),
            }
        );
    }
    return {};
}

auto TargetCoroutineVerifier::enter_scope(TargetTraversalScope scope) noexcept -> bool {
    if (scope.kind == TargetTraversalScopeKind::Callable
        || scope.kind == TargetTraversalScopeKind::CoroutineCallable) {
        callables.push_back(
            {.coroutine = scope.kind == TargetTraversalScopeKind::CoroutineCallable,
             .native_operation = false}
        );
    }
    return true;
}

auto TargetCoroutineVerifier::leave_scope(TargetTraversalScope scope) noexcept -> bool {
    if (scope.kind == TargetTraversalScopeKind::Callable
        || scope.kind == TargetTraversalScopeKind::CoroutineCallable) {
        if (callables.back().coroutine && !callables.back().native_operation) {
            return fail("coroutine body contains no native coroutine operation");
        }
        callables.pop_back();
    }
    return true;
}

auto TargetCoroutineVerifier::enter_expression(
    const TargetExpr& expression,
    TargetExpressionRole
) noexcept -> bool {
    if (!std::holds_alternative<TargetCoAwaitExpr>(expression.value)) {
        return true;
    }
    if (callables.empty() || !callables.back().coroutine) {
        return fail("co_await is outside a coroutine, including an ordinary generated lambda");
    }
    callables.back().native_operation = true;
    return true;
}

auto TargetCoroutineVerifier::enter_statement(const TargetStmt& statement) noexcept -> bool {
    if (std::holds_alternative<TargetCoReturnStmt>(statement.value)) {
        if (callables.empty() || !callables.back().coroutine) {
            return fail("co_return is outside a coroutine");
        }
        callables.back().native_operation = true;
        return true;
    }
    if (std::holds_alternative<TargetReturnStmt>(statement.value)
        && !callables.empty()
        && callables.back().coroutine) {
        return fail("ordinary return is inside a coroutine");
    }
    return true;
}

auto TargetCoroutineVerifier::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    if (function == nullptr) {
        return true;
    }
    const auto* definition = std::get_if<TargetFreeFunctionDefinition>(&function->form);
    return definition == nullptr
        || definition->execution != TargetCallableExecution::Coroutine
        || !function->constexpr_specifier
        || fail("a coroutine cannot be constexpr");
}

auto TargetCoroutineVerifier::fail(std::string message) noexcept -> bool {
    failure = std::move(message);
    return false;
}

auto validate_target_types(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept -> std::expected<void, TargetSealViolation> {
    return TargetTypeVerifier(identity, types, sections).run();
}

} // namespace

auto validate_target_unit(const TargetVerificationInput& input) noexcept
    -> std::expected<void, TargetSealViolation> {
    const auto type_result =
        validate_target_types(input.identity(), input.types(), input.sections());
    if (!type_result.has_value()) {
        return type_result;
    }
    const auto locals =
        TargetLocalVerifier(input.identity(), input.local_count()).run(input.sections());
    if (!locals.has_value()) {
        return locals;
    }
    const auto coroutines = TargetCoroutineVerifier().run(input.sections());
    if (!coroutines.has_value()) {
        return coroutines;
    }
    return validate_jumps(input.sections());
}

auto TargetVerificationInput::identity() const noexcept -> TargetUnitIdentity {
    return unit_identity;
}

auto TargetVerificationInput::types() const noexcept -> std::span<const TargetType> {
    return type_rows;
}

auto TargetVerificationInput::sections() const noexcept -> const TargetUnitSections& {
    return *unit_sections;
}

TargetVerificationInput::TargetVerificationInput(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections,
    std::size_t local_count
) noexcept
    : unit_identity(identity),
      type_rows(types),
      unit_sections(std::addressof(sections)),
      local_rows(local_count) {}

auto TargetVerificationInput::local_count() const noexcept -> std::size_t {
    return local_rows;
}
