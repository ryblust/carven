module carven:backend.lowering.body.composition.impl;

import :backend.lowering.body.composition;
import :backend.target;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :support.invariant;
import :support.unique_indirect;
import std;

namespace body_lowering {

auto StatementBuilder::emit(TargetStmt statement, bool continues) noexcept -> void {
    if (!this->continues()) {
        return;
    }
    lowered.has_declarations |= std::holds_alternative<TargetVariableStmt>(statement.value);
    lowered.statements.push_back(std::move(statement));
    if (!continues) {
        lowered.normal.reset();
    }
}

auto StatementBuilder::terminate(TargetStmt statement, ExitTarget target) noexcept -> void {
    if (!continues()) {
        return;
    }
    emit(std::move(statement), false);
    lowered.exits.add(target);
}

auto StatementBuilder::append(StatementBuilder source) noexcept -> void {
    if (!continues()) {
        return;
    }
    lowered.statements.insert(
        lowered.statements.end(),
        std::make_move_iterator(source.lowered.statements.begin()),
        std::make_move_iterator(source.lowered.statements.end())
    );
    lowered.normal = source.lowered.normal;
    lowered.exits.merge(source.lowered.exits);
    lowered.has_declarations |= source.lowered.has_declarations;
}

auto StatementBuilder::attribute(const TargetAttribution& attribution) noexcept -> void {
    for (auto& statement : lowered.statements) {
        if (std::holds_alternative<TargetGeneratedExpansionAttribution>(statement.attribution)) {
            statement.attribution = attribution;
        }
    }
}

auto StatementBuilder::scope(StatementBuilder source, TargetAttribution attribution) noexcept
    -> void {
    if (!continues()) {
        return;
    }
    source.attribute(attribution);
    if (!source.owns_storage()) {
        append(std::move(source));
        return;
    }
    const auto normal = source.continues();
    lowered.exits.merge(source.exits());
    emit(
        TargetStmt {
            .value = TargetBlockStmt {.statements = std::move(source).finish()},
            .attribution = std::move(attribution)
        },
        normal
    );
}

auto StatementBuilder::resume(
    TargetIdentifier label,
    TargetJumpRole role,
    ExitTarget target
) noexcept -> void {
    if (!consume_exit(target)) {
        invariant_violation("continuation does not own a pending exit");
    }
    lowered.normal = Unit {};
    emit(
        TargetStmt {
            .value = TargetLabelStmt {.label = std::move(label), .role = role},
            .attribution = TargetGeneratedExpansionAttribution {
                .reason = TargetExpansionReason::LoweringSupport
            }
        }
    );
}

auto StatementBuilder::result_region(TargetTypeID type, ExitTarget yield) && noexcept
    -> TargetExpr {
    if (continues()) {
        invariant_violation("value region has an undelivered normal result");
    }
    for (const auto target : exits().targets) {
        if (target != yield && target.kind != ExitKind::Unreachable) {
            invariant_violation("value region contains an external control exit");
        }
    }
    return TargetExpr {
        .value = TargetCallExpr {
            .callee = UniqueIndirect(
                TargetExpr {
                    .value =
                        TargetLambdaExpr {
                            .parameters = {},
                            .result = type,
                            .body = std::move(lowered.statements)
                        }
                }
            ),
            .template_argument_type_ids = {},
            .arguments = {}
        }
    };
}

} // namespace body_lowering
