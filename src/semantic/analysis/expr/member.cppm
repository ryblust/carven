module carven:semantic.analysis.expr.member;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.aggregate;
import :semantic.analysis.expr.projection;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scalar;
import :semantic.analysis.expr.scope;
import :semantic.analysis.expr.text;
import :semantic.analysis.operations;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import std;

template<typename Site>
auto expected_expression_enum(
    Site& site,
    std::optional<ConstructionTypeRef> expected,
    Span span
) noexcept -> ExpressionResult<TypeID> {
    const auto* type = expected.has_value() ? std::get_if<TypeID>(&*expected) : nullptr;
    if (type == nullptr
        || !std::holds_alternative<EnumTypeValue>(site.draft().type_copy(*type).value)) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeEnumContext,
            "contextual enum case requires an expected enum type"
        ));
    }
    return *type;
}

template<typename Site>
auto interpret_enum_case(
    Site& site,
    TypeID type,
    std::string_view name,
    Span name_span,
    std::span<const ASTCallArgument> arguments,
    Span span,
    bool called
) noexcept -> ExpressionTask<typename Site::Value> {
    auto selected = (co_await site.resolve_enum_case(type, name, name_span));
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    if (!called) {
        if (selected->payload_types.empty()) {
            co_return site.constant(*selected->constant, span);
        }
        co_return site.enum_constructor(type, *selected, span);
    }
    if (selected->payload_types.empty()) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeEnumCaseArity,
            "nullary enum case is a value and cannot be called"
        ));
    }
    if (arguments.size() != selected->payload_types.size()) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeEnumCaseArity,
            "enum case payload arity does not match"
        ));
    }
    auto payload = std::vector<typename Site::Value>();
    auto constants = std::vector<ConstantID>();
    for (const auto& [argument, type] : std::views::zip(arguments, selected->payload_types)) {
        auto value = (co_await site.read(argument.expression, type));
        if (!value.has_value()) {
            co_return std::unexpected(value.error());
        }
        auto converted =
            site.convert_argument(*value, type, site.syntax().expression(argument.expression).span);
        if (!converted.has_value()) {
            co_return std::unexpected(converted.error());
        }
        if (const auto known = site.known(*value)) {
            constants.push_back(*known);
        }
        payload.push_back(std::move(*value));
    }
    auto known = std::optional<ConstantID>();
    if (constants.size() == payload.size() && !(Site::mode == ExpressionMode::RequiredRoot)) {
        known = site.draft().intern_constant(
            {.type = type,
             .value =
                 PayloadEnumConstant {.enum_case = selected->id, .payload = std::move(constants)}}
        );
    }
    co_return construct_enum_value(site, type, selected->id, std::move(payload), known, span);
}

template<typename Site>
auto interpret_text(
    Site& site,
    TextIntrinsic intrinsic,
    typename Site::Value operand,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    const auto type =
        resolve_text_intrinsic_type(site.draft(), text_intrinsic_contract(intrinsic).result);
    auto known = fold_expression_constant(
        site,
        [&]() noexcept {
            return fold_text_intrinsic_constant(site.draft(), intrinsic, site.known(operand), type);
        },
        span
    );
    if (!known.has_value()) {
        return std::unexpected(known.error());
    }
    return construct_text_value(site, intrinsic, type, std::move(operand), *known, span);
}

template<typename Site>
auto text_qualifier(Site& site, ASTExprID expression) noexcept -> std::string_view {
    const auto* name = std::get_if<ASTNameExpr>(&site.syntax().expression(expression).value);
    if (name != nullptr) {
        const auto spelling = site.spelling(name->name_span);
        for (const auto candidate : {"String", "str", "char"}) {
            if (spelling == candidate) {
                return candidate;
            }
        }
    }
    return {};
}

template<typename Site>
auto interpret_member(Site& site, const ASTMemberExpr& source, Span span) noexcept
    -> ExpressionTask<typename Site::Selection> {
    if (source.op == ASTMemberOperator::Scope) {
        if (!text_qualifier(site, source.operand_id).empty()) {
            co_return std::unexpected(site.fail(
                source.name_span,
                DiagnosticCode::TypeMethodCall,
                "text factories must be called directly"
            ));
        }
        auto type = (co_await site.resolve_nominal_qualifier(source.operand_id));
        if (!type.has_value()) {
            co_return std::unexpected(type.error());
        }
        if (!type->has_value()) {
            co_return site.invalid_nominal_qualifier(
                site.syntax().expression(source.operand_id).span
            );
        }
        const auto canonical = site.draft().type_copy(**type);
        if (const auto* record = std::get_if<StructTypeValue>(&canonical.value)) {
            if constexpr (Site::mode == ExpressionMode::Body) {
                co_return (co_await site.associated_reference(record->structure, source.name_span));
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
            site.spelling(source.name_span),
            source.name_span,
            {},
            span,
            false
        ));
    }
    auto operand = (co_await site.read(source.operand_id, std::nullopt));
    if (!operand.has_value()) {
        co_return std::unexpected(operand.error());
    }
    const auto operand_type = site.type(*operand);
    const auto* concrete = std::get_if<TypeID>(&operand_type);
    if (concrete != nullptr
        && (site.draft().type_copy(*concrete).value
                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}}
            || site.draft().type_copy(*concrete).value
                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}})) {
        auto decision = decide_text_property(site.spelling(source.name_span));
        if (!decision.has_value()) {
            co_return std::unexpected(site.fail(
                source.name_span,
                decision.error().code,
                std::string(decision.error().message)
            ));
        }
        co_return interpret_text(site, *decision, std::move(*operand), span);
    }
    co_return construct_member_expression(site, source, std::move(*operand), span);
}
