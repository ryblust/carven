module carven:semantic.analysis.elaboration.stmt.control.impl;

import :frontend.ast.ids;
import :frontend.ast.stmt;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :support.invariant;
import std;

auto build_branch(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBranchBlockID block_id,
    BodyControl control
) noexcept -> HIRBlockID {
    const auto ast = module_analysis.syntax();
    const auto& block = ast.branch_block(block_id);
    return build_block_contents(
               module_analysis,
               scopes,
               block.span,
               block.statements,
               block.result,
               control,
               std::nullopt
    )
        .block;
}

auto build_value_branch(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    ASTBranchBlockID block_id,
    BodyControl control,
    ValueBranchKind kind
) noexcept -> BuiltValueBranch {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& source = ast.branch_block(block_id);
    const auto built = build_block_contents(
        module_analysis,
        scopes,
        source.span,
        source.statements,
        source.result,
        control,
        kind
    );
    const auto& block = builder.block(built.block);

    if (!block.result.has_value()) {
        invariant_violation("expression block was built without a result expression");
    }

    return {
        .block = built.block,
        .result = *block.result,
        .rejected_transfer = built.rejected_transfer,
    };
}

auto infer_return_type(
    ModuleAnalysis& module_analysis,
    const ReturnTypeInference& inference,
    Span fallback_span
) noexcept -> HIRTypeID {
    const auto& builder = module_analysis.builder();
    auto inferred = std::optional<HIRTypeID>();
    auto incompatible = false;
    for (const auto& observation : inference.observations()) {
        if (!observation.type.has_value()) {
            continue;
        }
        if (!inferred.has_value()) {
            inferred = observation.type;
        } else if (!compatible(module_analysis, *inferred, *observation.type)) {
            incompatible = true;
        }
    }
    auto result = inferred.has_value()
        ? *inferred
        : builtin(module_analysis, fallback_span, HIRBuiltinType::Void);
    if (incompatible) {
        module_analysis.emit(
            fallback_span,
            "lambda return values do not infer one compatible result type",
            DiagnosticCode::LambdaSignatureInference
        );
        result = error_type(module_analysis, fallback_span);
    }
    const auto foreign_result =
        std::holds_alternative<HIRForeignTypeValue>(builder.type(result).value);
    for (const auto& observation : inference.observations()) {
        const auto span = builder.provenance().origin(observation.origin).span;
        if (is_void(module_analysis, result) && observation.type.has_value()) {
            module_analysis.emit(
                span,
                "a void function cannot return a value",
                DiagnosticCode::TypeReturnValue
            );
        } else if (!is_void(module_analysis, result)
                   && !foreign_result
                   && !observation.type.has_value()) {
            module_analysis.emit(
                span,
                "a value-returning function must return a value",
                DiagnosticCode::TypeMissingReturnValue
            );
        } else if (observation.type.has_value()
                   && !compatible(module_analysis, result, *observation.type)) {
            module_analysis.emit(
                span,
                "returned value has an incompatible type",
                DiagnosticCode::TypeReturnMismatch
            );
        }
    }
    return result;
}

auto block_value(ModuleAnalysis& module_analysis, HIRBlockID id) noexcept -> HIRExprID {
    auto& builder = module_analysis.builder();
    if (const auto& block = builder.block(id); block.result) {
        return *block.result;
    } else {
        return module_analysis.recover_expression(builder.provenance().origin(block.origin).span);
    }
}
