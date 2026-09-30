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
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.stage.session;
import :semantic.analysis.types;
import :semantic.analysis.types.display;
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

auto BodyBatchElaborator::block_source(
    ProgramModuleID module_id,
    Span keyword,
    const std::optional<ASTBlockLabel>& label
) noexcept -> BlockSource {
    return {
        .label = label ? std::optional(draft->intern_spelling(label->text)) : std::nullopt,
        .origin = draft->append_source_origin(
            draft->module_source(module_id),
            label ? label->span : keyword
        ),
    };
}

auto BodyBatchElaborator::build_const_block(
    ProgramModuleID module_id,
    const ASTConstBlock& source
) noexcept -> AnalysisTask<void> {
    const auto* declaration = catalog_data.find_module(module_id);
    if (declaration == nullptr) {
        invariant_violation("const block belongs to an unknown module");
    }
    auto reservation = draft->reserve_body(BodyKind::ConstBlock);
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
    elaborator.static_body = true;
    // A block is not a callable: nothing returns from it.
    elaborator.transfer_boundaries.push_back({.loop_depth = 0uz, .construct = "a const block"});
    auto body = (co_await elaborator.run(source.body));
    if (!body) {
        co_return std::unexpected(body.error());
    }
    draft->add_body_draft(std::move(*body));
    static_body_roots.push_back({
        .body = id,
        .source = block_source(module_id, source.keyword_span, source.label),
    });
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
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::FlowMissingReturn,
                std::format(
                    "reachable path of callable returning '{}' has no return",
                    type_display_name(draft(), *result_type)
                )
            );
            diagnostic.primary(locate(ast.source_id(), span));
            // The value of a condition does not end a loop; only its absence does.
            if (block_body != nullptr && !ast.block(*block_body).statements.empty()) {
                const auto& last = ast.statement(ast.block(*block_body).statements.back());
                const auto* loop = std::get_if<ASTWhileStmt>(&last.value);
                const auto* literal = loop != nullptr && loop->condition
                    ? std::get_if<ASTLiteral>(&ast.expression(*loop->condition).value)
                    : nullptr;
                const auto* boolean = literal != nullptr
                    ? std::get_if<BooleanLiteralValue>(&literal->value)
                    : nullptr;
                if (boolean != nullptr && boolean->value) {
                    diagnostic.help("write a loop that has no condition as 'while { ... }'");
                }
            }
            co_return std::unexpected(draft().diagnostics().error(diagnostic.build()));
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
                // A failed body is diagnosed once and remembered; dependents on its
                // inferred contract receive that failure without another diagnostic.
                static_cast<void>((co_await complete_function(*function_id)));
                continue;
            }
            if (const auto* block = std::get_if<ASTConstBlock>(&item.value)) {
                static_cast<void>(co_await build_const_block(source_module.module_id, *block));
                continue;
            }
            const auto* test_form = std::get_if<CatalogTestForm>(&source_item.form);
            if (test_form == nullptr) {
                continue;
            }
            const auto& test = std::get<ASTTestDecl>(item.value);
            auto reservation = draft->reserve_body(BodyKind::Test);
            const auto body_id = reservation.id();
            const auto source =
                block_source(source_module.module_id, test.keyword_span, test.label);
            draft->define_test(
                test_form->test,
                TestDeclaration {
                    .is_const = test.is_const,
                    .module_id = source_module.declaration,
                    .source = source,
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
            elaborator.static_body = test.is_const;
            auto body = (co_await elaborator.run(test.body));
            if (!body.has_value()) {
                continue;
            }
            if (test.is_const) {
                static_body_roots.push_back({.body = body_id, .source = source});
            }
            draft->add_body_draft(std::move(*body));
        }
    }
    // Later stages read completed bodies; every body error is reported before this gate.
    if (const auto failure = draft->diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    for (const auto& [location, diagnostic] : unused_locals) {
        if (!used_locals.contains(location)) {
            draft->diagnostics().warning(diagnostic);
        }
    }
    auto contracts = validate_const_contracts(*draft, body_ids);
    if (!contracts) {
        co_return std::unexpected(contracts.error());
    }
    auto& stage = requests.stage();
    auto static_bodies = std::set<BodyID>();
    for (auto index = 0uz; index < static_body_roots.size(); ++index) {
        const auto root = static_body_roots[index];
        static_bodies.insert(root.body);
        static_cast<void>((co_await stage.run_body(root.body, root.source)));
    }
    if (const auto failure = draft->diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    // A function with static parameters executes only through its instances,
    // and the static stage has already executed the bodies that belong to it.
    for (const auto function : draft->function_declaration_ids()) {
        if (draft->staged_function(function)) {
            const auto declaration = draft->function_declaration_copy(function);
            if (const auto body = draft->body_for_callable(declaration.callable)) {
                static_bodies.insert(*body);
            }
        }
    }
    for (const auto body : draft->completed_body_ids()) {
        if (static_bodies.contains(body) || draft->body_draft(body).specialized) {
            continue;
        }
        auto realized = co_await stage.realize_body(body);
        if (!realized) {
            co_return std::unexpected(realized.error());
        }
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
                "compile-time execution depends on an unfinished function body"
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
                "compile-time execution requires a Carven function body"
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
    draft->add_body_draft(std::move(*body));
    co_return {};
}
