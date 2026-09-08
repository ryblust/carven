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
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.interop;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::block(ASTBlockID id) noexcept -> AnalysisResult<void> {
    for (const auto statement_id : ast.block(id).statements) {
        auto built = statement(statement_id);
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
    }
    return {};
}

auto BodyElaborator::run(const ASTCallableBody& source_body) noexcept
    -> AnalysisResult<StructuredBodyDraft> {
    const auto* block_body = std::get_if<ASTBlockID>(&source_body);
    const auto span = block_body != nullptr
        ? ast.block(*block_body).span
        : Span::from_bounds(
              std::get<ASTExpressionBody>(source_body).arrow_span.start(),
              ast.expression(std::get<ASTExpressionBody>(source_body).expression).span.end()
          );
    auto built = AnalysisResult<void>();
    if (block_body != nullptr) {
        built = block(*block_body);
    } else {
        const auto& expression_body = std::get<ASTExpressionBody>(source_body);
        begin_full_expression(span);
        built =
            return_statement(expression_body.expression, span, expression_body.arrow_span, true);
        end_full_expression(span);
    }
    if (!built.has_value()) {
        return std::unexpected(built.error());
    }
    if (!result_type.has_value()) {
        result_type = draft().intern_builtin_type(BuiltinType::Void);
    }
    if (reachable) {
        if (!is_void_type(draft(), *result_type)) {
            return std::unexpected(fail(
                span,
                DiagnosticCode::FlowMissingReturn,
                "reachable path of value-returning callable has no return"
            ));
        }
        append_statement(SemReturn {std::nullopt}, origin(span));
    }
    if (is_test) {
        draft().require_empty_failures(
            outward_failure_term_id,
            origin(span),
            EmptyFailureRequirementKind::RootBoundary
        );
    }
    diagnose_unused(frames.front());
    regions.front().failures = BodyFailures(outward_failure_term_id);
    return std::move(body_builder).finish(std::move(regions.front()));
}

auto BodyBatchElaborator::run() noexcept -> AnalysisResult<void> {
    for (const auto& source_module : catalog_data.modules()) {
        const auto ast = draft->syntax_tree(source_module.module_id).view();
        for (const auto& source_item : source_module.items) {
            const auto& item = ast.item(source_item.item_id);
            if (const auto* function_id = std::get_if<FunctionID>(&source_item.form)) {
                auto result = complete_function(*function_id);
                if (!result.has_value()) {
                    return std::unexpected(result.error());
                }
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

auto BodyBatchElaborator::ensure_function_signature(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisResult<void> {
    const auto declaration = draft->function_declaration_copy(id);
    if (!draft->pending_function_contract_copy(declaration.callable).has_value()) {
        return {};
    }
    if (std::holds_alternative<Analyzing>(states.at(id.index()))) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeResultInferenceCycle,
            "function result inference forms a cycle; add an explicit '-> T' result type"
        );
        diagnostic.primary(locate(draft->syntax_tree(requester).view().source_id(), span));
        const auto first = std::ranges::find(active_path, id);
        for (auto current = first; current != active_path.end(); ++current) {
            const auto& symbol = *functions.at(current->index());
            diagnostic.related(
                locate(
                    draft->syntax_tree(symbol.module_id).view().source_id(),
                    symbol.declaration_span
                ),
                std::format("result of '{}' is being inferred", symbol.name)
            );
        }
        return std::unexpected(draft->diagnostics().error(diagnostic.build()));
    }
    return complete_function(id);
}

auto BodyBatchElaborator::complete_function(FunctionID id) noexcept -> AnalysisResult<void> {
    auto& state = states.at(id.index());
    if (std::holds_alternative<Complete>(state)) {
        return {};
    }
    if (const auto* failed = std::get_if<Failed>(&state)) {
        return std::unexpected(failed->failure);
    }
    if (!std::holds_alternative<Unvisited>(state)) {
        invariant_violation("function body completion reentered without a signature dependency");
    }
    state = Analyzing {};
    active_path.push_back(id);
    auto result = elaborate_function(id);
    active_path.pop_back();
    if (!result.has_value()) {
        state = Failed {.failure = result.error()};
        return std::unexpected(result.error());
    }
    state = Complete {};
    return {};
}

auto BodyBatchElaborator::elaborate_function(FunctionID id) noexcept -> AnalysisResult<void> {
    const auto& symbol = *functions.at(id.index());
    const auto ast = draft->syntax_tree(symbol.module_id).view();
    const auto& item = ast.item(symbol.item_id);
    const auto& function = std::get<ASTFunctionDecl>(item.value);
    const auto* implementation = std::get_if<ASTFunctionBody>(&function.implementation);
    if (implementation == nullptr) {
        return {};
    }
    const auto declaration = draft->function_declaration_copy(id);
    const auto pending = draft->pending_function_contract_copy(declaration.callable);
    auto parameters = std::vector<ConstructionCallableParameter>();
    auto result_type = std::optional<ConstructionTypeRef>();
    auto failures = std::optional<FailureTermID>();
    auto policy = FailureContractPolicy::Declared;
    if (pending.has_value()) {
        parameters = pending->parameters;
        failures = pending->failures;
        policy = pending->policy;
    } else {
        const auto contract = draft->construction_callable_contract_copy(declaration.callable);
        parameters = contract.parameters;
        result_type = contract.result;
        failures = contract.failures;
        policy = contract.policy;
    }
    if (parameters.size() != function.parameters.size()) {
        invariant_violation("source function parameters differ from resolved callable contract");
    }
    auto reservation = draft->reserve_body(BodyKind::Function);
    draft->complete_callable(
        declaration.callable,
        FunctionBodyImplementation {.body = reservation.id()}
    );
    const auto function_origin =
        draft->append_source_origin(draft->module_source(symbol.module_id), item.span);
    const auto actual_failures =
        create_body_failure_term(*draft, *failures, policy, function_origin);
    auto elaborator = BodyElaborator(
        *this,
        symbol.module_id,
        declaration.module_id,
        ast,
        std::move(reservation),
        result_type,
        actual_failures,
        policy != FailureContractPolicy::UndeclaredExplicit,
        false
    );
    for (auto index = 0uz; index < function.parameters.size(); ++index) {
        auto parameter = elaborator.add_parameter(function.parameters[index], parameters[index]);
        if (!parameter.has_value()) {
            return std::unexpected(parameter.error());
        }
    }
    auto body = elaborator.run(implementation->body);
    if (!body.has_value()) {
        return std::unexpected(body.error());
    }
    if (pending.has_value()) {
        const auto result = elaborator.inferred_result_type();
        draft->complete_function_result(declaration.callable, result);
        auto boundary =
            validate_cpp_boundary_result(*draft, symbol.module_id, ast, function, result);
        if (!boundary.has_value()) {
            return std::unexpected(boundary.error());
        }
    }
    draft->add_body_draft(std::move(*body));
    return {};
}
