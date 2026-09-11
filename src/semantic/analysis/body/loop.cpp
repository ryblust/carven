module carven:semantic.analysis.body.loop.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::c_style_for_statement(
    const ASTForStmt& source,
    const ASTCStyleForHeader& header,
    Span span
) noexcept -> AnalysisResult<void> {
    push_frame(source.header.span);
    regions.push_back(empty_region(source.header.span));
    if (!std::holds_alternative<std::monostate>(header.initializer.value)) {
        begin_full_expression(header.initializer.span);
        auto initialized = std::visit(
            Overloaded {
                [](std::monostate) static noexcept -> AnalysisResult<void> { return {}; },
                [&](const ASTVariableDecl& value) noexcept { return variable_statement(value); },
                [&](const ASTAssignment& value) noexcept { return assignment_statement(value); },
                [&](ASTExprID id) noexcept -> AnalysisResult<void> {
                    auto value = expression(id);
                    if (!value.has_value()) {
                        return std::unexpected(value.error());
                    }
                    auto consumed = consume_pending(*value, ast.expression(id).span);
                    if (!consumed.has_value()) {
                        return std::unexpected(consumed.error());
                    }
                    append_expression(*value, ast.expression(id).span);
                    return {};
                }
            },
            header.initializer.value
        );
        if (!initialized.has_value()) {
            return std::unexpected(initialized.error());
        }
        end_full_expression(header.initializer.span);
    }
    auto initializer = std::move(regions.back());
    regions.pop_back();
    auto condition = std::optional<SemanticExpression>();
    auto known = std::optional<bool>(true);
    if (header.condition.has_value()) {
        const auto id = *header.condition;
        begin_full_expression(ast.expression(id).span);
        auto value = expression(id, draft().intern_builtin_type(BuiltinType::Bool));
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        known = known_boolean_constant(draft(), value->constant());
        auto checked = require_bool(*value, ast.expression(id).span);
        if (!checked.has_value()) {
            return std::unexpected(checked.error());
        }
        condition = std::move(*checked);
        end_full_expression(ast.expression(id).span);
    }
    const auto condition_reachable = reachable;
    push_frame(ast.block(source.body).span);
    regions.push_back(empty_region(ast.block(source.body).span));
    loops.push_back({});
    reachable = true;
    auto body_result = [&]() noexcept {
        [[maybe_unused]] const auto path = BodyReferencePathGuard(
            reference_path_reachable,
            condition_reachable && (!known.has_value() || *known)
        );
        return block(source.body);
    }();
    if (!body_result.has_value()) {
        return std::unexpected(body_result.error());
    }
    const auto step_reachable = reachable || loops.back().has_continue;
    auto body = std::move(regions.back());
    regions.pop_back();
    const auto has_break = loops.back().has_break;
    loops.pop_back();
    pop_frame();
    reachable = true;
    regions.push_back(empty_region(source.header.span));
    for (const auto& step : header.steps) {
        [[maybe_unused]] const auto path = BodyReferencePathGuard(
            reference_path_reachable,
            condition_reachable && step_reachable && (!known.has_value() || *known)
        );
        begin_full_expression(step.span);
        auto result = std::visit(
            Overloaded {
                [&](const ASTAssignment& value) noexcept { return assignment_statement(value); },
                [&](const ASTUpdate& value) noexcept { return update_statement(value); },
                [&](ASTExprID id) noexcept -> AnalysisResult<void> {
                    auto value = expression(id);
                    if (!value.has_value()) {
                        return std::unexpected(value.error());
                    }
                    auto consumed = consume_pending(*value, ast.expression(id).span);
                    if (!consumed.has_value()) {
                        return std::unexpected(consumed.error());
                    }
                    append_expression(*value, ast.expression(id).span);
                    return {};
                }
            },
            step.value
        );
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        end_full_expression(step.span);
    }
    auto steps = std::move(regions.back());
    regions.pop_back();
    pop_frame();
    reachable = condition_reachable && (known != true || has_break);
    append_statement(
        SemLoop {
            UniqueIndirect(std::move(initializer)),
            std::move(condition),
            UniqueIndirect(std::move(body)),
            UniqueIndirect(std::move(steps))
        },
        origin(span)
    );
    return {};
}

auto BodyElaborator::range_for_statement(
    const ASTForStmt& source,
    const ASTRangeForHeader& header,
    Span span
) noexcept -> AnalysisResult<void> {
    push_frame(source.header.span);
    const auto loop_lifetime = frames.back().lifetime;
    auto declared = std::optional<ConstructionTypeRef>();
    if (header.type.has_value()) {
        auto resolved = resolve_type(*header.type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        declared = *resolved;
    }
    auto begin = std::optional<SemanticExpression>();
    auto end = std::optional<SemanticExpression>();
    auto element_type = std::optional<ConstructionTypeRef>();
    if (const auto* range = std::get_if<ASTHalfOpenRange>(&header.iterable)) {
        if (header.write_marker.has_value()) {
            return std::unexpected(fail(
                *header.write_marker,
                DiagnosticCode::AccessRangeBinding,
                "integer range bindings cannot use Write access"
            ));
        }
        auto value = expression(range->begin, declared);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        const auto* type = std::get_if<TypeID>(&value->type());
        if (type == nullptr) {
            return std::unexpected(fail(
                ast.expression(range->begin).span,
                DiagnosticCode::TypeRangeInteger,
                "integer range bound must have a concrete integer type"
            ));
        }
        const auto canonical = draft().type_copy(*type);
        const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
        if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
            return std::unexpected(fail(
                ast.expression(range->begin).span,
                DiagnosticCode::TypeRangeInteger,
                "integer range bounds must be integers"
            ));
        }
        element_type = value->type();
        auto checked = consume_value(*value, ast.expression(range->begin).span, AccessMode::Read);
        if (!checked.has_value()) {
            return std::unexpected(checked.error());
        }
        begin = std::move(*checked);
        auto limit = expression(range->end, element_type);
        if (!limit.has_value()) {
            return std::unexpected(limit.error());
        }
        if (!compatible(*element_type, limit->type())) {
            return std::unexpected(fail(
                ast.expression(range->end).span,
                DiagnosticCode::TypeRangeBounds,
                "integer range bounds must have one compatible type"
            ));
        }
        auto limit_checked =
            consume_value(*limit, ast.expression(range->end).span, AccessMode::Read);
        if (!limit_checked.has_value()) {
            return std::unexpected(limit_checked.error());
        }
        end = std::move(*limit_checked);
    } else {
        const auto id = std::get<ASTExprID>(header.iterable);
        auto value = expression(id);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        auto read_only = false;
        if (const auto* type = std::get_if<TypeID>(&value->type())) {
            const auto canonical = draft().type_copy(*type);
            if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
                element_type = array->element;
            } else if (const auto* slice = std::get_if<SliceTypeValue>(&canonical.value)) {
                element_type = slice->element;
                read_only = true;
            } else if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
                       builtin != nullptr && builtin->kind == BuiltinType::StrCharsView) {
                read_only = true;
                element_type = draft().intern_builtin_type(BuiltinType::Char);
            }
        } else {
            const auto construction =
                draft().construction_type_copy(std::get<TypeTermID>(value->type()));
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
                element_type = array->element;
            } else if (const auto* slice =
                           std::get_if<ConstructionSliceTypeValue>(&construction.value)) {
                element_type = slice->element;
                read_only = true;
            }
        }
        if (!element_type.has_value()) {
            return std::unexpected(fail(
                ast.expression(id).span,
                DiagnosticCode::TypeRangeIterable,
                "range iterable must be an array, slice, or character view"
            ));
        }
        if (header.write_marker.has_value()) {
            if (read_only) {
                return std::unexpected(fail(
                    *header.write_marker,
                    DiagnosticCode::AccessViewRangeBinding,
                    "view range bindings are read-only"
                ));
            }
            const auto* place = std::get_if<PlaceExpression>(&value->storage);
            auto stable = false;
            for (const auto& frame : frames) {
                for (const auto& [name, local] : frame.names) {
                    static_cast<void>(name);
                    if (const auto* bound = std::get_if<BoundStorage>(&local.storage)) {
                        stable |= place != nullptr
                            && bound->binding == place->root
                            && std::holds_alternative<SemBinding>(place->expression.value);
                    }
                }
            }
            if (!stable) {
                return std::unexpected(fail(
                    ast.expression(id).span,
                    DiagnosticCode::AccessRangeIterable,
                    "Write array iteration requires a stable whole local array place"
                ));
            }
        } else {
            auto checked = consume_value(*value, ast.expression(id).span, AccessMode::Read);
            if (!checked.has_value()) {
                return std::unexpected(checked.error());
            }
            begin = std::move(*checked);
        }
        if (header.write_marker.has_value()) {
            begin = take_built(*value, ast.expression(id).span);
        }
    }
    if (declared.has_value() && !compatible(*declared, *element_type)) {
        return std::unexpected(fail(
            source.header.span,
            DiagnosticCode::TypeRangeBinding,
            "range binding annotation differs from the element type"
        ));
    }
    const auto header_reachable = reachable;
    const auto type = declared.value_or(*element_type);
    auto binding = std::optional<LocalBindingID>();
    push_frame(ast.block(source.body).span);
    if (const auto* named = std::get_if<ASTNamedBindingTarget>(&header.target)) {
        const auto storage = body_builder.add_owner_binding(
            draft().intern_spelling(spelling(named->name_span)),
            type,
            frames.back().lifetime,
            header.write_marker.has_value(),
            origin(named->name_span)
        );
        binding = storage.binding;
        auto bound = bind_local(
            named->name_span,
            BodyLocalStorage {
                .storage = storage,
                .type = type,
                .takeable = false,
                .role = header.write_marker.has_value() ? BodyLocalRole::Local
                                                        : BodyLocalRole::RangeRead,
                .unused_candidate = std::nullopt
            },
            DiagnosticCode::NameDuplicateLocal
        );
        if (!bound.has_value()) {
            return std::unexpected(bound.error());
        }
    }
    regions.push_back(empty_region(ast.block(source.body).span));
    loops.push_back({});
    reachable = true;
    auto result = [&]() noexcept {
        [[maybe_unused]] const auto path =
            BodyReferencePathGuard(reference_path_reachable, header_reachable);
        return block(source.body);
    }();
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    auto body = std::move(regions.back());
    regions.pop_back();
    loops.pop_back();
    pop_frame();
    pop_frame();
    reachable = header_reachable;
    append_statement(
        SemRangeLoop {
            loop_lifetime,
            header.write_marker.has_value() ? AccessMode::Write : AccessMode::Read,
            binding,
            end ? SemRangeSource(SemIntegerRange {std::move(*begin), std::move(*end)})
                : SemRangeSource(SemSequenceRange {std::move(*begin)}),
            UniqueIndirect(std::move(body))
        },
        origin(span)
    );
    return {};
}

auto BodyElaborator::for_statement(const ASTForStmt& source, Span span) noexcept
    -> AnalysisResult<void> {
    return std::visit(
        Overloaded {
            [&](const ASTCStyleForHeader& header) noexcept {
                return c_style_for_statement(source, header, span);
            },
            [&](const ASTRangeForHeader& header) noexcept {
                return range_for_statement(source, header, span);
            },
        },
        source.header.value
    );
}

auto BodyElaborator::statement(ASTStmtID id) noexcept -> AnalysisResult<void> {
    const auto& source = ast.statement(id);
    const auto was_reachable = reachable;
    const auto previous_failures = regions.back().failures;
    const auto previous_test_exit = regions.back().exits_test;
    ensure_reachable_diagnostics(source.span);
    [[maybe_unused]] const auto reference_path =
        BodyReferencePathGuard(reference_path_reachable, reachable);
    const auto owns_full_expression = std::visit(
        Overloaded {
            [](const ASTVariableDecl&) static noexcept { return true; },
            [](const ASTAssignment&) static noexcept { return true; },
            [](const ASTUpdate&) static noexcept { return true; },
            [](const ASTExprStatement&) static noexcept { return true; },
            [](const ASTTestOperationStmt&) static noexcept { return true; },
            [](const ASTControlTransfer&) static noexcept { return true; },
            [](const ASTMatchForm&) static noexcept { return true; },
            [](const ASTTryForm&) static noexcept { return true; },
            [](const ASTWhileStmt&) static noexcept { return false; },
            [](const ASTForStmt&) static noexcept { return false; },
            [](const ASTIfForm&) static noexcept { return false; },
        },
        source.value
    );
    if (owns_full_expression) {
        begin_full_expression(source.span);
    }
    auto result = std::visit(
        Overloaded {
            [&](const ASTVariableDecl& value) noexcept { return variable_statement(value); },
            [&](const ASTAssignment& value) noexcept { return assignment_statement(value); },
            [&](const ASTUpdate& value) noexcept { return update_statement(value); },
            [&](const ASTExprStatement& value) noexcept -> AnalysisResult<void> {
                auto built = expression(value.expression);
                if (!built.has_value()) {
                    return std::unexpected(built.error());
                }
                auto consumed = consume_pending(*built, ast.expression(value.expression).span);
                if (!consumed.has_value()) {
                    return std::unexpected(consumed.error());
                }
                append_expression(*built, ast.expression(value.expression).span);
                return {};
            },
            [&](const ASTTestOperationStmt& value) noexcept {
                return test_statement(value, source.span);
            },
            [&](const ASTControlTransfer& value) noexcept { return transfer_statement(value); },
            [&](const ASTWhileStmt& value) noexcept { return while_statement(value, source.span); },
            [&](const ASTIfForm& value) noexcept { return if_statement(value, source.span); },
            [&](const ASTForStmt& value) noexcept -> AnalysisResult<void> {
                return for_statement(value, source.span);
            },
            [&](const ASTMatchForm& value) noexcept -> AnalysisResult<void> {
                return match_statement(value, source.span);
            },
            [&](const ASTTryForm& value) noexcept -> AnalysisResult<void> {
                return try_statement(value, source.span);
            },
        },
        source.value
    );
    if (owns_full_expression && active_full_expression.has_value()) {
        if (result.has_value()) {
            end_full_expression(source.span);
        } else {
            active_full_expression.reset();
        }
    }
    if (!was_reachable) {
        regions.back().failures = previous_failures;
        regions.back().exits_test = previous_test_exit;
        reachable = false;
    }
    return result;
}
