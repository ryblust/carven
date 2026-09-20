module carven:semantic.analysis.expr.call;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.interpolation;
import :semantic.analysis.expr.member;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.text;
import :semantic.analysis.operations;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import std;

template<typename Site>
auto interpret_call(
    Site& site,
    const ASTCallExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionTask<typename Site::Value> {
    auto callee_id = source.callee;
    while (const auto* group =
               std::get_if<ASTGroupExpr>(&site.syntax().expression(callee_id).value)) {
        callee_id = group->expression;
    }
    const auto& callee = site.syntax().expression(callee_id);
    if (const auto* contextual = std::get_if<ASTContextualCaseExpr>(&callee.value)) {
        auto type = expected_expression_enum(site, expected, contextual->name_span);
        if (!type.has_value()) {
            co_return std::unexpected(type.error());
        }
        co_return (co_await interpret_enum_case(
            site,
            *type,
            site.spelling(contextual->name_span),
            contextual->name_span,
            source.arguments,
            span,
            true
        ));
    }
    if (const auto* member = std::get_if<ASTMemberExpr>(&callee.value)) {
        if (member->op == ASTMemberOperator::Scope) {
            if (const auto qualifier = text_qualifier(site, member->operand_id);
                !qualifier.empty()) {
                const auto name = site.spelling(member->name_span);
                auto selected = std::optional<TextIntrinsic>();
                if (qualifier == "String" && name == "from_str") {
                    selected = TextIntrinsic::FromStr;
                }
                if (qualifier == "str" && name == "from_utf8_unchecked") {
                    selected = TextIntrinsic::FromUTF8Unchecked;
                }
                if (qualifier == "char" && name == "from_u32_unchecked") {
                    selected = TextIntrinsic::FromU32Unchecked;
                }
                if (!selected) {
                    co_return std::unexpected(site.fail(
                        member->name_span,
                        DiagnosticCode::TypeMethodCall,
                        "text type has no such factory"
                    ));
                }
                const auto intrinsic = *selected;
                if (source.arguments.size()
                    != text_intrinsic_contract(intrinsic).parameters.size()) {
                    co_return std::unexpected(site.fail(
                        span,
                        DiagnosticCode::TypeMethodCallArity,
                        "text factory argument count does not match"
                    ));
                }
                co_return (co_await construct_text_call(
                    site,
                    intrinsic,
                    std::nullopt,
                    source.arguments,
                    span
                ));
            }
            auto type = (co_await site.resolve_nominal_qualifier(member->operand_id));
            if (!type.has_value()) {
                co_return std::unexpected(type.error());
            }
            if (!type->has_value()) {
                co_return site.invalid_nominal_qualifier(
                    site.syntax().expression(member->operand_id).span
                );
            }
            const auto canonical = site.draft().type_copy(**type);
            if (const auto* record = std::get_if<StructTypeValue>(&canonical.value)) {
                if constexpr (Site::mode == ExpressionMode::Body) {
                    co_return (
                        co_await site.associated_call(source, *member, record->structure, span)
                    );
                } else {
                    co_return std::unexpected(site.fail(
                        span,
                        DiagnosticCode::ConstAdmission,
                        "class operations are not admitted in required constant expressions"
                    ));
                }
            }
            co_return (co_await interpret_enum_case(
                site,
                **type,
                site.spelling(member->name_span),
                member->name_span,
                source.arguments,
                span,
                true
            ));
        }
        auto operand = (co_await site.read(member->operand_id, std::nullopt));
        if (!operand.has_value()) {
            co_return std::unexpected(operand.error());
        }
        const auto slice = decide_slice_method(
            site.draft(),
            site.type(*operand),
            site.spelling(member->name_span),
            source.arguments.size()
        );
        if (!slice) {
            co_return std::unexpected(
                site.fail(span, slice.error().code, std::string(slice.error().message))
            );
        }
        if (slice->has_value()) {
            co_return (co_await construct_slice_call(
                site,
                **slice,
                std::move(*operand),
                source.arguments,
                span
            ));
        }
        if (site.spelling(member->name_span) == "append_format"
            && site.type(*operand)
                == ConstructionTypeRef(site.draft().builtin_type(BuiltinType::String))) {
            if (source.arguments.size() != 1uz) {
                co_return std::unexpected(site.fail(
                    span,
                    DiagnosticCode::TypeMethodCallArity,
                    "String.append_format requires one interpolation argument"
                ));
            }
            auto argument = source.arguments.front().expression;
            while (const auto* group =
                       std::get_if<ASTGroupExpr>(&site.syntax().expression(argument).value)) {
                argument = group->expression;
            }
            const auto* interpolation =
                std::get_if<ASTInterpolationExpr>(&site.syntax().expression(argument).value);
            if (interpolation == nullptr) {
                co_return std::unexpected(site.fail(
                    site.syntax().expression(argument).span,
                    DiagnosticCode::TypeMethodCall,
                    "String.append_format requires a direct interpolation"
                ));
            }
            co_return (co_await construct_interpolation(
                site,
                *interpolation,
                span,
                std::optional(std::move(*operand))
            ));
        }
        const auto decision = decide_text_method(
            site.draft(),
            site.type(*operand),
            site.spelling(member->name_span),
            source.arguments.size()
        );
        if (!decision.has_value()) {
            co_return std::unexpected(site.fail(
                decision.error().code == DiagnosticCode::TypeMethodCallArity ? span
                                                                             : member->name_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        if (decision->has_value()) {
            if (**decision == TextIntrinsic::Len || **decision == TextIntrinsic::IsEmpty) {
                co_return interpret_text(site, **decision, std::move(*operand), span);
            }
            co_return (co_await construct_text_call(
                site,
                **decision,
                std::move(*operand),
                source.arguments,
                span
            ));
        }
        co_return (co_await site.member_call(source, *member, std::move(*operand), span));
    }
    co_return (co_await site.call(source, span));
}
