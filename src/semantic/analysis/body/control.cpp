module carven:semantic.analysis.body.control.impl;

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

auto BodyElaborator::branch_block(
    ASTBranchBlockID id,
    bool consume_result,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisResult<std::optional<BuiltExpression>> {
    const auto& source = ast.branch_block(id);
    const auto enclosing_full_expression = active_full_expression;
    active_full_expression.reset();
    for (const auto statement_id : source.statements) {
        auto built = statement(statement_id);
        if (!built.has_value()) {
            active_full_expression = enclosing_full_expression;
            return std::unexpected(built.error());
        }
    }
    if (!source.result.has_value()) {
        active_full_expression = enclosing_full_expression;
        return std::optional<BuiltExpression>();
    }
    ensure_reachable_diagnostics(ast.expression(*source.result).span);
    auto result = expression(*source.result, expected);
    if (!result.has_value()) {
        active_full_expression = enclosing_full_expression;
        return std::unexpected(result.error());
    }
    if (consume_result) {
        auto consumed = consume_pending(*result, ast.expression(*source.result).span);
        if (!consumed.has_value()) {
            active_full_expression = enclosing_full_expression;
            return std::unexpected(consumed.error());
        }
    }
    active_full_expression = enclosing_full_expression;
    return std::optional<BuiltExpression>(std::move(*result));
}

auto BodyElaborator::build_branch(
    ASTBranchBlockID id,
    bool value_form,
    std::optional<ConstructionTypeRef>& merged_type,
    BodyPendingFailureTerms& pending
) noexcept -> AnalysisResult<SemanticRegion> {
    const auto span = ast.branch_block(id).span;
    regions.push_back(empty_region(span));
    if (value_form) {
        value_boundary_loop_depths.push_back(loops.size());
    }
    auto result = branch_block(id, !value_form, merged_type);
    if (value_form) {
        value_boundary_loop_depths.pop_back();
    }
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (result->has_value()) {
        auto& value = **result;
        if (value_form && (!does_not_complete(value) || !is_void_type(draft(), value.type()))) {
            collect_pending(pending, value);
            if (!merged_type.has_value()) {
                auto inferred = infer_value_type(value, span);
                if (!inferred.has_value()) {
                    return std::unexpected(inferred.error());
                }
                merged_type = *inferred;
            }
            auto coerced = coerce_to(value, *merged_type, span);
            if (!coerced.has_value()) {
                return std::unexpected(coerced.error());
            }
            auto read = consume_value(value, span, AccessMode::Read);
            if (!read.has_value()) {
                return std::unexpected(read.error());
            }
            regions.back().result = std::move(*read);
            regions.back().failures = BodyFailures(
                draft().add_union_failure_term(
                    {regions.back().failures.term(), regions.back().result->failures.term()}
                )
            );
            regions.back().exits_test |= regions.back().result->exits_test;
        } else {
            collect_pending(pending, value);
            append_expression(value, span);
        }
    } else if (value_form && reachable) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::FlowValueBranchResult,
            "value-control branch requires a result"
        ));
    }
    auto region = std::move(regions.back());
    regions.pop_back();
    return region;
}

auto BodyElaborator::build_arm(
    const ASTMatchArmBody& source,
    bool value_form,
    std::optional<ConstructionTypeRef>& type,
    BodyPendingFailureTerms& pending
) noexcept -> AnalysisResult<SemanticRegion> {
    if (const auto* branch = std::get_if<ASTBranchBlockID>(&source.value)) {
        return build_branch(*branch, value_form, type, pending);
    }
    regions.push_back(empty_region(source.span));
    if (value_form) {
        value_boundary_loop_depths.push_back(loops.size());
    }
    if (const auto* expression_id = std::get_if<ASTExprID>(&source.value)) {
        auto built = expression(*expression_id, value_form ? type : std::nullopt);
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        if (value_form && (!does_not_complete(*built) || !is_void_type(draft(), built->type()))) {
            collect_pending(pending, *built);
            if (!type.has_value()) {
                auto inferred = infer_value_type(*built, source.span);
                if (!inferred.has_value()) {
                    return std::unexpected(inferred.error());
                }
                type = *inferred;
            }
            auto coerced = coerce_to(*built, *type, source.span);
            if (!coerced.has_value()) {
                return std::unexpected(coerced.error());
            }
            auto checked = consume_value(*built, source.span, AccessMode::Read);
            if (!checked.has_value()) {
                return std::unexpected(checked.error());
            }
            regions.back().result = std::move(*checked);
            regions.back().failures = BodyFailures(
                draft().add_union_failure_term(
                    {regions.back().failures.term(), regions.back().result->failures.term()}
                )
            );
            regions.back().exits_test |= regions.back().result->exits_test;
        } else {
            auto consumed = consume_pending(*built, source.span);
            if (!consumed.has_value()) {
                return std::unexpected(consumed.error());
            }
            append_expression(*built, source.span);
        }
    } else {
        auto built = transfer_statement(std::get<ASTControlTransfer>(source.value));
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
    }
    if (value_form) {
        value_boundary_loop_depths.pop_back();
    }
    auto region = std::move(regions.back());
    regions.pop_back();
    return region;
}

auto BodyElaborator::build_if(
    const ASTIfForm& source,
    Span span,
    std::optional<ConstructionTypeRef> expected,
    bool value_form
) noexcept -> AnalysisResult<BuiltExpression> {
    auto branches = std::vector<SemConditionalBranch>();
    auto pending = BodyPendingFailureTerms();
    auto merged_type = expected;
    auto remaining = reachable && reference_path_reachable;
    auto normal = false;
    for (const auto& branch : source.branches) {
        const auto condition_span = ast.expression(branch.condition).span;
        if (!value_form) {
            begin_full_expression(condition_span);
        }
        auto condition = [&]() noexcept -> AnalysisResult<BuiltExpression> {
            [[maybe_unused]] const auto path =
                BodyReferencePathGuard(reference_path_reachable, remaining);
            return expression(branch.condition, draft().intern_builtin_type(BuiltinType::Bool));
        }();
        if (!condition.has_value()) {
            return std::unexpected(condition.error());
        }
        const auto known = known_boolean_constant(draft(), condition->constant());
        if (value_form) {
            collect_pending(pending, *condition);
        }
        auto check = require_bool(*condition, condition_span);
        if (!check.has_value()) {
            return std::unexpected(check.error());
        }
        auto condition_tree = std::move(*check);
        if (!value_form) {
            end_full_expression(condition_span);
        }
        const auto selected = remaining && condition->completes && (!known.has_value() || *known);
        push_frame(ast.branch_block(branch.body).span);
        reachable = true;
        auto body = [&]() noexcept -> AnalysisResult<SemanticRegion> {
            [[maybe_unused]] const auto path =
                BodyReferencePathGuard(reference_path_reachable, selected);
            return build_branch(branch.body, value_form, merged_type, pending);
        }();
        if (!body.has_value()) {
            return std::unexpected(body.error());
        }
        normal = normal || (selected && reachable);
        pop_frame();
        branches.push_back({std::move(condition_tree), std::move(*body)});
        remaining = remaining && condition->completes && (!known.has_value() || !*known);
        reachable = true;
    }
    auto otherwise = std::optional<OwnedSemanticRegion>();
    if (source.else_branch.has_value()) {
        const auto id = *source.else_branch;
        push_frame(ast.branch_block(id).span);
        reachable = true;
        auto body = [&]() noexcept -> AnalysisResult<SemanticRegion> {
            [[maybe_unused]] const auto path =
                BodyReferencePathGuard(reference_path_reachable, remaining);
            return build_branch(id, value_form, merged_type, pending);
        }();
        if (!body.has_value()) {
            return std::unexpected(body.error());
        }
        normal = normal || (remaining && reachable);
        otherwise.emplace(UniqueIndirect(std::move(*body)));
        pop_frame();
    } else {
        normal = normal || remaining;
    }
    reachable = normal;
    auto result = make_built(
        merged_type.value_or(draft().intern_builtin_type(BuiltinType::Void)),
        SemIf {std::move(branches), std::move(otherwise)},
        span,
        std::move(pending)
    );
    return normal ? std::move(result) : mark_noncompleting(std::move(result));
}

auto BodyElaborator::if_statement(const ASTIfForm& source, Span span) noexcept
    -> AnalysisResult<void> {
    auto expression = build_if(source, span, std::nullopt, false);
    if (!expression.has_value()) {
        return std::unexpected(expression.error());
    }
    append_expression(*expression, span);
    return {};
}

auto BodyElaborator::while_statement(const ASTWhileStmt& source, Span span) noexcept
    -> AnalysisResult<void> {
    const auto outer_reachable = reachable;
    push_frame(span);
    begin_full_expression(ast.expression(source.condition).span);
    auto condition = expression(source.condition, draft().intern_builtin_type(BuiltinType::Bool));
    if (!condition.has_value()) {
        return std::unexpected(condition.error());
    }
    const auto known = known_boolean_constant(draft(), condition->constant());
    auto check = require_bool(*condition, ast.expression(source.condition).span);
    if (!check.has_value()) {
        return std::unexpected(check.error());
    }
    auto condition_tree = std::move(*check);
    end_full_expression(span);
    auto initializer = empty_region(span);
    auto steps = empty_region(span);
    push_frame(ast.block(source.body).span);
    regions.push_back(empty_region(ast.block(source.body).span));
    loops.push_back({});
    reachable = true;
    auto result = [&]() noexcept {
        [[maybe_unused]] const auto path = BodyReferencePathGuard(
            reference_path_reachable,
            outer_reachable && condition->completes && (!known.has_value() || *known)
        );
        return block(source.body);
    }();
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    auto body = std::move(regions.back());
    regions.pop_back();
    const auto has_break = loops.back().has_break;
    loops.pop_back();
    pop_frame();
    pop_frame();
    reachable = outer_reachable && condition->completes && (known != true || has_break);
    append_statement(
        SemLoop {
            UniqueIndirect(std::move(initializer)),
            std::move(condition_tree),
            UniqueIndirect(std::move(body)),
            UniqueIndirect(std::move(steps))
        },
        origin(span)
    );
    return {};
}
