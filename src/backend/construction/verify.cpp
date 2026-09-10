module carven:backend.construction.verify.impl;

import :backend.construction;
import :backend.construction.verify;
import :semantic.semir;
import :support.visit;
import std;

namespace {
enum class Visit { Unseen, Active, Complete };

struct Control final {
    std::optional<ConstructionRegionID> loop;
    std::optional<ConstructionExpressionID> handler;
    std::optional<ConstructionCaughtFailure> caught;
};

class ConstructionVerifier final {
public:
    ConstructionVerifier(const BodyConstruction& body, const SemIRBody& metadata) noexcept
        : body(body),
          metadata(metadata),
          expressions(body.expression_values().size(), Visit::Unseen),
          regions(body.region_values().size(), Visit::Unseen) {}

    auto finish() noexcept -> std::expected<void, ConstructionViolation> {
        if (body.body() != metadata.id()) {
            fail(
                ConstructionViolationKind::InvalidIdentity,
                {},
                "construction metadata belongs to another body"
            );
        }
        region(body.root(), {}, {});
        for (auto index = 0uz; index < expressions.size(); ++index) {
            if (expressions[index] == Visit::Unseen) {
                fail(
                    ConstructionViolationKind::UnownedExecution,
                    body.expression_values()[index].origin,
                    "construction expression has no execution owner"
                );
            }
        }
        for (auto index = 0uz; index < regions.size(); ++index) {
            if (regions[index] == Visit::Unseen) {
                fail(
                    ConstructionViolationKind::UnownedExecution,
                    body.region_values()[index].origin,
                    "construction region has no execution owner"
                );
            }
        }
        if (violation) {
            return std::unexpected(*violation);
        }
        return {};
    }

private:
    const BodyConstruction& body;
    const SemIRBody& metadata;
    std::vector<Visit> expressions;
    std::vector<Visit> regions;
    std::optional<ConstructionViolation> violation;

    void fail(
        ConstructionViolationKind kind,
        std::optional<ProgramOriginID> origin,
        std::string_view message
    ) noexcept {
        if (!violation) {
            violation = ConstructionViolation {kind, origin, message};
        }
    }

    template<typename Tag>
    auto valid(
        ConstructionID<Tag> id,
        std::size_t size,
        std::optional<ProgramOriginID> origin
    ) noexcept -> bool {
        if (violation) {
            return false;
        }
        if (id.owner() != body.body() || id.index() >= size) {
            fail(
                ConstructionViolationKind::InvalidIdentity,
                origin,
                "construction reference belongs to another body or is out of range"
            );
            return false;
        }
        return true;
    }

    auto enter(Visit& state, std::optional<ProgramOriginID> origin) noexcept -> bool {
        if (state != Visit::Unseen) {
            fail(
                state == Visit::Active ? ConstructionViolationKind::ExecutionCycle
                                       : ConstructionViolationKind::DuplicateExecution,
                origin,
                state == Visit::Active ? "construction execution contains a cycle"
                                       : "construction occurrence has multiple execution owners"
            );
            return false;
        }
        state = Visit::Active;
        return true;
    }

    void lifetime(LifetimeRegionID id, ProgramOriginID origin) noexcept {
        if (!metadata.lifetime_regions().contains(id)) {
            fail(
                ConstructionViolationKind::InvalidLifetime,
                origin,
                "construction lifetime belongs to another body or is out of range"
            );
        }
    }

    void pattern(PatternID pattern_id, ProgramOriginID origin) noexcept {
        if (!metadata.pattern_table().contains(pattern_id)) {
            fail(
                ConstructionViolationKind::InvalidIdentity,
                origin,
                "construction pattern belongs to another body or is out of range"
            );
        }
    }

    // Control references are checked without following them as execution edges.
    void failure(
        const ConstructionFailureExit& destination,
        const Control& control,
        ProgramOriginID origin
    ) noexcept {
        const auto* handler = std::get_if<ConstructionHandlerExit>(&destination);
        if (handler != nullptr) {
            if (!valid(handler->handler, expressions.size(), origin)) {
                return;
            }
            if (!std::holds_alternative<ConstructionTry>(body.expression(handler->handler).value)) {
                fail(
                    ConstructionViolationKind::InvalidControl,
                    origin,
                    "failure target is not a handler"
                );
                return;
            }
        }
        if (handler == nullptr ? control.handler.has_value()
                               : control.handler != handler->handler) {
            fail(
                ConstructionViolationKind::InvalidControl,
                origin,
                "failure target is not the enclosing protected-region handler"
            );
        }
    }

    void expression(
        ConstructionExpressionID id,
        const Control& control,
        std::optional<ProgramOriginID> parent
    ) noexcept {
        if (!valid(id, expressions.size(), parent)) {
            return;
        }
        const auto& source = body.expression(id);
        if (!enter(expressions[id.index()], source.origin)) {
            return;
        }
        lifetime(source.lifetime, source.origin);
        std::visit(
            Overloaded {
                [&](const ConstructionShortCircuit& value) noexcept {
                    expression(value.condition, control, source.origin);
                    expression(value.selected, control, source.origin);
                },
                [&](const ConstructionConditional& value) noexcept {
                    for (const auto& branch : value.branches) {
                        expression(branch.condition, control, source.origin);
                        region(branch.body, control, source.origin);
                    }
                    if (value.otherwise) {
                        region(*value.otherwise, control, source.origin);
                    }
                },
                [&](const ConstructionMatch& value) noexcept {
                    expression(value.subject, control, source.origin);
                    for (const auto& arm : value.arms) {
                        pattern(arm.pattern_id, source.origin);
                        if (arm.guard) {
                            expression(*arm.guard, control, source.origin);
                        }
                        region(arm.body, control, source.origin);
                    }
                },
                [&](const ConstructionTry& value) noexcept {
                    failure(value.residual_destination, control, source.origin);
                    auto protected_control = control;
                    protected_control.handler = id;
                    region(value.body, protected_control, source.origin);
                    for (const auto& arm : value.arms) {
                        for (const auto& alternative : arm.alternatives) {
                            if (const auto* typed =
                                    std::get_if<ConstructionTypedCatch>(&alternative.pattern)) {
                                pattern(typed->pattern_id, source.origin);
                            }
                        }
                        auto handler_control = control;
                        handler_control.caught =
                            ConstructionCaughtFailure {id, arm.accepted_failures};
                        if (arm.guard) {
                            expression(*arm.guard, handler_control, source.origin);
                        }
                        region(arm.body, handler_control, source.origin);
                    }
                },
                [&](const auto&) noexcept {
                    if (const auto* call = std::get_if<ConstructionOperation>(&source.value);
                        call != nullptr && call->failure) {
                        failure(call->failure->destination, control, source.origin);
                    }
                    for (const auto& input : construction_operands(source)) {
                        expression(input.expression, control, source.origin);
                    }
                }
            },
            source.value
        );
        expressions[id.index()] = Visit::Complete;
    }

    void statement(
        const ConstructionStatement& source,
        ConstructionRegionID owner,
        const Control& control
    ) noexcept {
        lifetime(source.lifetime, source.origin);
        const auto input = [&](ConstructionExpressionID id) noexcept {
            expression(id, control, source.origin);
        };
        std::visit(
            Overloaded {
                [&](const ConstructionReturn& value) noexcept {
                    if (value.value) {
                        input(*value.value);
                    }
                },
                [&](const ConstructionLoopTransfer& value) noexcept {
                    if (!valid(value.loop, regions.size(), source.origin)) {
                        return;
                    }
                    const auto& target = body.region(value.loop);
                    const auto is_loop = target.statements.size() == 1uz
                        && !target.result
                        && (std::holds_alternative<ConstructionLoop>(
                                target.statements.front().value
                            )
                            || std::holds_alternative<ConstructionRangeLoop>(
                                target.statements.front().value
                            ));
                    if (!is_loop || control.loop != value.loop) {
                        fail(
                            ConstructionViolationKind::InvalidControl,
                            source.origin,
                            "loop transfer does not target its enclosing loop body"
                        );
                    }
                },
                [&](const ConstructionThrow& value) noexcept {
                    failure(value.destination, control, source.origin);
                    input(value.value);
                },
                [&](const ConstructionRethrow& value) noexcept {
                    if (!valid(value.source.handler, expressions.size(), source.origin)) {
                        return;
                    }
                    if (!control.caught
                        || control.caught->handler != value.source.handler
                        || control.caught->failures != value.source.failures) {
                        fail(
                            ConstructionViolationKind::InvalidControl,
                            source.origin,
                            "rethrow does not reference the selected enclosing catch arm"
                        );
                    }
                    failure(value.destination, control, source.origin);
                },
                [&](const ConstructionDiscard& value) noexcept { input(value.expression); },
                [&](const ConstructionInitialize& value) noexcept { input(value.initializer); },
                [&](const ConstructionAssign& value) noexcept {
                    input(value.target);
                    input(value.value);
                },
                [&](const ConstructionScope& value) noexcept {
                    region(value.region, control, source.origin);
                },
                [&](const ConstructionLoop& value) noexcept {
                    loop_owner(owner, source.origin);
                    region(value.initializer, control, source.origin);
                    if (value.condition) {
                        input(*value.condition);
                    }
                    auto nested = control;
                    nested.loop = owner;
                    region(value.body, nested, source.origin);
                    region(value.steps, control, source.origin);
                },
                [&](const ConstructionRangeLoop& value) noexcept {
                    loop_owner(owner, source.origin);
                    lifetime(value.lifetime, source.origin);
                    std::visit(
                        Overloaded {
                            [&](const ConstructionIntegerRange& range) noexcept {
                                input(range.begin);
                                input(range.end);
                            },
                            [&](const ConstructionSequenceRange& range) noexcept {
                                input(range.value);
                            }
                        },
                        value.source
                    );
                    auto nested = control;
                    nested.loop = owner;
                    region(value.body, nested, source.origin);
                },
                [&](const ConstructionTestReport& value) noexcept {
                    if (value.condition) {
                        input(*value.condition);
                    }
                    if (value.message) {
                        input(*value.message);
                    }
                }
            },
            source.value
        );
    }

    void loop_owner(ConstructionRegionID id, ProgramOriginID origin) noexcept {
        const auto& owner = body.region(id);
        if (owner.statements.size() != 1uz || owner.result) {
            fail(
                ConstructionViolationKind::InvalidControl,
                origin,
                "loop identity must own exactly its loop statement"
            );
        }
    }

    void region(
        ConstructionRegionID id,
        const Control& control,
        std::optional<ProgramOriginID> parent
    ) noexcept {
        if (!valid(id, regions.size(), parent)) {
            return;
        }
        const auto& source = body.region(id);
        if (!enter(regions[id.index()], source.origin)) {
            return;
        }
        lifetime(source.lifetime, source.origin);
        for (const auto& item : source.statements) {
            if (violation) {
                break;
            }
            statement(item, id, control);
        }
        if (source.result) {
            expression(*source.result, control, source.origin);
        }
        regions[id.index()] = Visit::Complete;
    }
};
} // namespace

auto validate_construction(const BodyConstruction& body, const SemIRBody& metadata) noexcept
    -> std::expected<void, ConstructionViolation> {
    return ConstructionVerifier(body, metadata).finish();
}
