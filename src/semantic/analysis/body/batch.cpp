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
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.admission;
import :semantic.analysis.constant.evaluation;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

auto BodyElaborator::block(ASTBlockID id) noexcept -> AnalysisTask<void> {
    for (const auto statement_id : ((ast.block(id))).statements) {
        auto built = (co_await statement(statement_id));
        if (!built.has_value()) {
            co_return std::unexpected(built.error());
        }
    }
    co_return {};
}

auto BodyBatchElaborator::defer_constant_block(
    ProgramModuleID module_id,
    const ASTConstantBlock& source,
    BodyLocalNames locals
) noexcept -> void {
    constant_blocks.push_back({
        .module_id = module_id,
        .syntax = source,
        .locals = std::move(locals),
    });
}

auto BodyBatchElaborator::build_constant_block(PendingConstantBlock source) noexcept
    -> AnalysisTask<void> {
    const auto module_id = source.module_id;
    const auto* declaration = catalog_data.find_module(module_id);
    if (declaration == nullptr) {
        invariant_violation("constant block belongs to an unknown module");
    }
    auto reservation = draft->reserve_body(BodyKind::ConstantBlock);
    const auto id = reservation.id();
    auto elaborator = BodyElaborator(
        *this,
        module_id,
        declaration->declaration,
        draft->syntax_tree(module_id).view(),
        std::move(reservation),
        draft->builtin_type(BuiltinType::Void),
        draft->add_empty_failure_term(),
        true,
        false
    );
    elaborator.inherited_locals = std::move(source.locals);
    auto body = (co_await elaborator.run(source.syntax.body));
    if (!body) {
        co_return std::unexpected(body.error());
    }
    auto admitted = validate_constant_body(*draft, *body);
    if (!admitted) {
        co_return std::unexpected(admitted.error());
    }
    draft->add_body_draft(std::move(*body));
    constant_roots.push_back(id);
    co_return {};
}

auto BodyElaborator::run(const ASTCallableBody& source_body) noexcept
    -> AnalysisTask<StructuredBodyDraft> {
    const auto* block_body = std::get_if<ASTBlockID>(&source_body);
    const auto span = block_body != nullptr
        ? ((ast.block(*block_body))).span
        : Span::from_bounds(
              std::get<ASTExpressionBody>(source_body).arrow_span.start(),
              ast.expression(std::get<ASTExpressionBody>(source_body).expression).span.end()
          );
    auto built = AnalysisResult<void>();
    if (block_body != nullptr) {
        built = (co_await block(*block_body));
    } else {
        const auto& expression_body = std::get<ASTExpressionBody>(source_body);
        begin_full_expression(span);
        built = (co_await return_statement(
            expression_body.expression,
            span,
            expression_body.arrow_span,
            true
        ));
        end_full_expression(span);
    }
    if (!built.has_value()) {
        co_return std::unexpected(built.error());
    }
    if (!result_type.has_value()) {
        result_type = draft().builtin_type(BuiltinType::Void);
    }
    if (reachable) {
        if (!is_void_type(draft(), *result_type)) {
            co_return std::unexpected(fail(
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
    collect_unused_locals(frames.front());
    regions.front().failures = BodyFailures(outward_failure_term_id);
    co_return std::move(body_builder).finish(std::move(regions.front()));
}

auto BodyBatchElaborator::run() noexcept -> AnalysisTask<void> {
    for (const auto& source_module : catalog_data.modules()) {
        const auto ast = draft->syntax_tree(source_module.module_id).view();
        for (const auto& source_item : source_module.items) {
            const auto& item = ast.item(source_item.item_id);
            if (const auto* function_id = std::get_if<FunctionID>(&source_item.form)) {
                auto result = (co_await complete_function(*function_id));
                if (!result.has_value()) {
                    co_return std::unexpected(result.error());
                }
                continue;
            }
            if (const auto* block = std::get_if<ASTConstantBlock>(&item.value)) {
                defer_constant_block(source_module.module_id, *block);
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
                    .is_const = test.is_const,
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
                draft->builtin_type(BuiltinType::Void),
                failures,
                false,
                true
            );
            auto body = (co_await elaborator.run(test.body));
            if (!body.has_value()) {
                co_return std::unexpected(body.error());
            }
            if (test.is_const) {
                constant_roots.push_back(body_id);
                auto admitted = validate_constant_body(*draft, *body);
                if (!admitted) {
                    co_return admitted;
                }
            }
            draft->add_body_draft(std::move(*body));
        }
    }
    for (auto index = 0uz; index < constant_blocks.size(); ++index) {
        auto result = (co_await build_constant_block(std::move(constant_blocks[index])));
        if (!result) {
            co_return result;
        }
    }
    for (const auto& [location, diagnostic] : unused_locals) {
        if (!used_locals.contains(location)) {
            draft->diagnostics().warning(diagnostic);
        }
    }
    for (auto index = 0uz; index < constant_roots.size(); ++index) {
        const auto body = constant_roots[index];
        static_cast<void>((co_await evaluate_constant_body(*draft, requests, body)));
    }
    if (const auto failure = draft->diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    co_return {};
}

auto BodyBatchElaborator::ensure_function_signature(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    auto completed =
        (co_await requests.ensure_declaration(catalog_data.function_symbol(id), requester, span));
    if (!completed) {
        co_return completed;
    }

    const auto declaration = draft->function_declaration_copy(id);
    if (!draft->pending_function_contract_copy(declaration.callable).has_value()) {
        co_return {};
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
        co_return std::unexpected(draft->diagnostics().error(diagnostic.build()));
    }
    co_return (co_await complete_function(id));
}

auto BodyBatchElaborator::ensure_function_body(
    FunctionID id,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<BodyID> {
    auto completed = (co_await ensure_function_signature(id, requester, span));
    if (!completed) {
        co_return std::unexpected(completed.error());
    }
    if (std::holds_alternative<Analyzing>(states.at(id.index()))) {
        co_return std::unexpected(draft->diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::ConstEvaluation,
                "constant evaluation depends on an unfinished function body"
            )
                .primary(locate(draft->syntax_tree(requester).view().source_id(), span))
                .build()
        ));
    }
    completed = (co_await complete_function(id));
    if (!completed) {
        co_return std::unexpected(completed.error());
    }
    if (!body_ids.at(id.index())) {
        co_return std::unexpected(draft->diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::ConstAdmission,
                "constant evaluation requires a Carven function body"
            )
                .primary(locate(draft->syntax_tree(requester).view().source_id(), span))
                .build()
        ));
    }
    co_return *body_ids.at(id.index());
}

auto BodyBatchElaborator::complete_function(FunctionID id) noexcept -> AnalysisTask<void> {
    auto& state = states.at(id.index());
    if (std::holds_alternative<Complete>(state)) {
        co_return {};
    }
    if (const auto* failed = std::get_if<Failed>(&state)) {
        co_return std::unexpected(failed->failure);
    }
    if (!std::holds_alternative<Unvisited>(state)) {
        invariant_violation("function body completion reentered without a signature dependency");
    }

    const auto& symbol = *functions.at(id.index());
    auto completed =
        (co_await requests
             .ensure_declaration(symbol.symbol_id, symbol.module_id, symbol.declaration_span));
    if (!completed) {
        state = Failed {.failure = completed.error()};
        co_return completed;
    }
    if (std::holds_alternative<Complete>(state)) {
        co_return {};
    }
    if (const auto* failed = std::get_if<Failed>(&state)) {
        co_return std::unexpected(failed->failure);
    }

    state = Analyzing {};
    active_path.push_back(id);
    auto result = (co_await elaborate_function(id));
    active_path.pop_back();
    if (!result.has_value()) {
        state = Failed {.failure = result.error()};
        co_return std::unexpected(result.error());
    }
    state = Complete {};
    co_return {};
}

auto BodyBatchElaborator::elaborate_function(FunctionID id) noexcept -> AnalysisTask<void> {
    const auto& symbol = *functions.at(id.index());
    const auto ast = draft->syntax_tree(symbol.module_id).view();
    const auto& item = ast.item(symbol.item_id);
    const auto& function = std::get<ASTFunctionDecl>(item.value);
    const auto* implementation = std::get_if<ASTFunctionBody>(&function.implementation);
    if (implementation == nullptr) {
        co_return {};
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
    body_ids.at(id.index()) = reservation.id();
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
    elaborator.lexical_class =
        symbol.class_operation ? std::optional(symbol.class_operation->owner) : std::nullopt;
    for (auto index = 0uz; index < function.parameters.size(); ++index) {
        auto parameter = elaborator.add_parameter(function.parameters[index], parameters[index]);
        if (!parameter.has_value()) {
            co_return std::unexpected(parameter.error());
        }
    }
    auto body = (co_await elaborator.run(implementation->body));
    if (!body.has_value()) {
        co_return std::unexpected(body.error());
    }
    if (pending.has_value()) {
        const auto result = elaborator.inferred_result_type();
        draft->complete_function_result(declaration.callable, result);
    }
    if (declaration.is_const) {
        auto admitted = validate_constant_function(*draft, id, *body);
        if (!admitted) {
            co_return std::unexpected(admitted.error());
        }
    }
    draft->add_body_draft(std::move(*body));
    co_return {};
}
