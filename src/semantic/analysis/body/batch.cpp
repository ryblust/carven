module carven:semantic.analysis.body.batch.impl;

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
import :semantic.analysis.constant.proof;
import :semantic.analysis.coverage;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {

auto BodyElaborator::block(ASTBlockID id) noexcept -> AnalysisResult<void> {
    for (const auto statement_id : ast.block(id).statements) {
        auto built = statement(statement_id);
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
    }
    return {};
}

auto BodyElaborator::run(ASTBlockID source_body) noexcept -> AnalysisResult<StructuredBodyDraft> {
    auto built = block(source_body);
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    if (!result_type.has_value()) {
        result_type = draft().intern_builtin_type(BuiltinType::Void);
    }
    if (reachable) {
        if (!is_void_type(draft(), *result_type)) {
            return std::unexpected(fail(
                ast.block(source_body).span,
                DiagnosticCode::FlowMissingReturn,
                "reachable path of value-returning callable has no return"
            ));
        }
        append_statement(
            SemReturn<ConstructionTypeRef, FailureTermID> {std::nullopt},
            ast.block(source_body).span
        );
    }
    if (is_test) {
        draft().require_empty_failures(
            outward_failure_term_id,
            origin(ast.block(source_body).span),
            EmptyFailureRequirementKind::RootBoundary
        );
    }
    diagnose_unused(frames.front());
    regions.front().failures = outward_failure_term_id;
    return std::move(body_builder).finish(std::move(regions.front()));
}

auto BatchElaborator::run() noexcept -> AnalysisResult<void> {
    for (const auto& source_module : catalog_data.modules()) {
        const auto ast = draft->syntax_tree(source_module.module_id).view();
        for (const auto& source_item : source_module.items) {
            const auto& item = ast.item(source_item.item_id);
            if (const auto* function_id = std::get_if<FunctionID>(&source_item.form)) {
                const auto& function = std::get<ASTFunctionDecl>(item.value);
                const auto* implementation = std::get_if<ASTFunctionBody>(&function.implementation);
                if (implementation == nullptr) {
                    continue;
                }
                const auto declaration = draft->function_declaration_copy(*function_id);
                const auto contract =
                    draft->construction_callable_contract_copy(declaration.callable);
                if (contract.parameters.size() != function.parameters.size()) {
                    invariant_violation(
                        "source function parameters differ from resolved callable contract"
                    );
                }
                auto reservation = draft->reserve_body(BodyKind::Function);
                const auto body_id = reservation.id();
                draft->complete_callable(
                    declaration.callable,
                    FunctionBodyImplementation {.body = body_id}
                );
                const auto function_origin = draft->append_source_origin(
                    draft->module_source(source_module.module_id),
                    item.span
                );
                const auto actual_failures =
                    create_body_failure_term(*draft, contract, function_origin);
                if (declaration.entry_point.has_value()) {
                    draft->require_empty_failures(
                        actual_failures,
                        function_origin,
                        EmptyFailureRequirementKind::RootBoundary
                    );
                }
                auto elaborator = BodyElaborator(
                    *this,
                    source_module.module_id,
                    source_module.declaration,
                    ast,
                    std::move(reservation),
                    contract.result,
                    actual_failures,
                    contract.policy != FailureContractPolicy::UndeclaredPublished,
                    false
                );
                for (auto index = 0uz; index < function.parameters.size(); ++index) {
                    auto parameter = elaborator.add_parameter(
                        function.parameters[index],
                        contract.parameters[index]
                    );
                    if (!parameter.has_value()) {
                        return std::unexpected(parameter.error());
                    }
                }
                auto body = elaborator.run(implementation->body);
                if (!body.has_value()) {
                    return std::unexpected(body.error());
                }
                draft->add_body_draft(std::move(*body));
                continue;
            }
            const auto* test_form = std::get_if<CatalogTestForm>(&source_item.form);
            if (test_form == nullptr) {
                continue;
            }
            const auto& test = std::get<ASTTestDecl>(item.value);
            auto reservation = draft->reserve_body(BodyKind::Test);
            const auto body_id = reservation.id();
            draft->define_test(
                test_form->test,
                TestDeclaration {
                    .module_id = source_module.declaration,
                    .name = draft->intern_spelling(test.name),
                    .origin = draft->append_source_origin(
                        draft->module_source(source_module.module_id),
                        test.name_span
                    ),
                    .body = body_id,
                }
            );
            const auto failures = draft->add_empty_failure_term();
            auto elaborator = BodyElaborator(
                *this,
                source_module.module_id,
                source_module.declaration,
                ast,
                std::move(reservation),
                draft->intern_builtin_type(BuiltinType::Void),
                failures,
                false,
                true
            );
            auto body = elaborator.run(test.body);
            if (!body.has_value()) {
                return std::unexpected(body.error());
            }
            draft->add_body_draft(std::move(*body));
        }
    }
    return {};
}


} // namespace body_elaboration
