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
import :semantic.semir.traversal;
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
        false,
        std::nullopt,
        true
    );
    elaborator.static_body = true;
    // A block is not a callable: nothing returns from it.
    elaborator.transfer_boundaries.push_back({.loop_depth = 0uz, .construct = "a const block"});
    auto body = (co_await elaborator.run(source.body));
    if (!body) {
        co_return std::unexpected(body.error());
    }
    if (elaborator.observe_sources) {
        draft->source_analysis()->add_body(std::move(elaborator.source_occurrences));
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
        end_full_expression();
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
                    type_display_name(draft(), *result_type, catalog())
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
        append_statement(SemReturn {.value = std::nullopt}, origin(span));
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
    co_return finish_body(std::move(regions.front()));
}

auto BodyBatchElaborator::run() noexcept -> AnalysisTask<void> {
    for (const auto definition : draft->generic_declaration_ids()) {
        const auto source = draft->generic_declaration_copy(definition);
        const auto* callable = std::get_if<GenericCallableDefinition>(&source);
        if (!callable || !callable->contract.name) {
            continue;
        }
        const auto& symbol = *catalog_data.symbol(catalog_data.generic_symbol(definition));
        static_cast<void>(
            co_await ensure_generic_function(definition, symbol.module_id, symbol.declaration_span)
        );
    }
    if (const auto failure = draft->diagnostics().failure()) {
        co_return std::unexpected(*failure);
    }
    auto formed = draft->validate_generic_type_formations();
    if (!formed) {
        co_return std::unexpected(formed.error());
    }
    for (const auto& source_module : catalog_data.modules()) {
        if (draft->imported(source_module.module_id)) {
            continue;
        }
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
            const auto execution = test.is_const ? TestExecution::Static
                : test.async_span                ? TestExecution::Asynchronous
                                                 : TestExecution::Synchronous;
            draft->define_test(
                test_form->test,
                TestDeclaration {
                    .execution = execution,
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
                true,
                std::nullopt,
                true
            );
            elaborator.static_body = execution == TestExecution::Static;
            elaborator.async_body = execution == TestExecution::Asynchronous;
            auto body = (co_await elaborator.run(test.body));
            if (!body.has_value()) {
                continue;
            }
            if (execution == TestExecution::Static) {
                static_body_roots.push_back({.body = body_id, .source = source});
            }
            if (elaborator.observe_sources) {
                draft->source_analysis()->add_body(std::move(elaborator.source_occurrences));
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
    // Close inferred generic environments together before static realization
    // consumes their callable bodies.
    auto closed = draft->close_generic_calls();
    if (!closed) {
        co_return std::unexpected(closed.error());
    }
    auto contracts = validate_const_contracts(*draft, body_ids);
    if (!contracts) {
        co_return std::unexpected(contracts.error());
    }
    auto& stage = requests.stage();
    auto static_bodies = static_expression_bodies;
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
        if (draft->imported(body)
            || static_bodies.contains(body)
            || draft->body_draft(body).specialized) {
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
    if (id.index() >= functions.size() || functions[id.index()] == nullptr) {
        co_return {};
    }

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
        diagnostic.primary(locate(draft->module_source_id(requester), span));
        const auto first = std::ranges::find(active_path, id);
        for (auto current = first; current != active_path.end(); ++current) {
            const auto& symbol = *functions.at(current->index());
            diagnostic.related(
                locate(draft->module_source_id(symbol.module_id), symbol.declaration_span),
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
    auto body = std::optional<BodyID>();
    if (id.index() >= functions.size() || functions[id.index()] == nullptr) {
        const auto declaration = draft->function_declaration_copy(id);
        auto prepared = co_await requests.ensure_callable(declaration.callable, requester, span);
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
        body = draft->body_for_callable(declaration.callable);
        if (!body) {
            invariant_violation("generic function instance has no completed body");
        }
    } else {
        auto completed = co_await ensure_function_signature(id, requester, span);
        if (!completed) {
            co_return std::unexpected(completed.error());
        }
        if (std::holds_alternative<Analyzing>(states.at(id.index()))) {
            co_return std::unexpected(draft->diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::ConstEvaluation,
                    "compile-time execution depends on an unfinished function body"
                )
                    .primary(locate(draft->module_source_id(requester), span))
                    .build()
            ));
        }
        completed = co_await complete_function(id);
        if (!completed) {
            co_return std::unexpected(completed.error());
        }
        body = body_ids.at(id.index());
        if (!body) {
            co_return std::unexpected(draft->diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::ConstAdmission,
                    "compile-time execution requires a Carven function body"
                )
                    .primary(locate(draft->module_source_id(requester), span))
                    .build()
            ));
        }
    }
    // Substitution may introduce concrete value types beyond the callable signature.
    // Prepare their declarations before the body can query value properties.
    auto types = std::map<ConstructionTypeRef, ProgramOriginID>();
    const auto& definition = draft->body_draft(*body);
    for (const auto binding : definition.bindings.entries()) {
        types.try_emplace(binding.value.type, binding.value.origin);
    }
    for (const auto pattern : definition.patterns.entries()) {
        types.try_emplace(pattern.value.type, pattern.value.origin);
    }
    const auto collect = [&](const SemanticExpression& expression) noexcept {
        types.try_emplace(expression.type.construction(), expression.origin);
    };
    visit_semantic_nodes(definition.region, collect);
    if (definition.residual) {
        visit_semantic_nodes(*definition.residual, collect);
    }
    for (const auto& [type, origin] : types) {
        const auto location = draft->source_origin(origin);
        auto prepared = co_await requests.ensure_type(
            type,
            draft->source_module(location.source_id),
            location.span
        );
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
    }
    co_return *body;
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
    if (!symbol.item_id) {
        invariant_violation("source function has no AST item");
    }
    const auto& item = ast.item(*symbol.item_id);
    const auto& function = std::get<ASTFunctionDecl>(item.value);
    const auto* implementation = std::get_if<ASTFunctionBody>(&function.implementation);
    if (implementation == nullptr) {
        co_return {};
    }
    const auto declaration = draft->function_declaration_copy(id);
    const auto pending = draft->pending_function_contract_copy(declaration.callable);
    auto parameters = std::vector<ConstructionCallableParameter>();
    auto result_type = std::optional<ConstructionTypeRef>();
    auto failures = std::optional<ConstructionFailureRef>();
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
        false,
        std::nullopt,
        !draft->staged_function(id)
    );
    elaborator.async_body = function.async_span.has_value();
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
    if (elaborator.observe_sources) {
        draft->source_analysis()->add_body(std::move(elaborator.source_occurrences));
    }
    draft->add_body_draft(std::move(*body));
    co_return {};
}

auto BodyBatchElaborator::ensure_generic_function(
    GenericDeclarationID definition,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    if (draft->has_generic_callable_body(definition)) {
        co_return {};
    }
    auto [found, inserted] = generic_states.try_emplace(definition, Unvisited {});
    auto& state = found->second;
    if (std::holds_alternative<Complete>(state)) {
        co_return {};
    }
    if (const auto* failure = std::get_if<Failed>(&state)) {
        co_return std::unexpected(failure->failure);
    }
    auto declared = co_await requests.ensure_declaration(
        catalog_data.generic_symbol(definition),
        requester,
        span
    );
    if (!declared) {
        co_return declared;
    }
    auto function =
        std::get<GenericCallableDefinition>(draft->generic_declaration_copy(definition));
    if (std::holds_alternative<Analyzing>(state)) {
        if (!function.infer_result) {
            co_return {};
        }
        co_return std::unexpected(draft->diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::TypeResultInferenceCycle,
                "generic function result inference forms a cycle; add an explicit '-> T' result type"
            )
                .primary(locate(draft->module_source_id(requester), span))
                .build()
        ));
    }
    state = Analyzing {};
    const auto& symbol = *catalog_data.symbol(catalog_data.generic_symbol(definition));
    const auto syntax = draft->syntax_tree(symbol.module_id).view();
    const auto& source = std::get<ASTFunctionDecl>(syntax.item(*symbol.item_id).value);
    const auto actual = create_body_failure_term(
        *draft,
        function.signature.failures,
        function.signature.policy,
        function.contract.origin
    );
    const auto parameters = draft->generic_parameter_contract_copy(definition);
    auto elaborator = BodyElaborator(
        *this,
        symbol.module_id,
        function.contract.module_id,
        syntax,
        draft->reserve_generic_body(definition),
        function.infer_result ? std::nullopt : std::optional(function.signature.result),
        actual,
        function.signature.policy != FailureContractPolicy::UndeclaredExplicit,
        false,
        GenericTypeContext {
            .definition = definition,
            .parameters = parameters.parameters,
            .parameter_kinds = parameters.parameter_kinds
        },
        true
    );
    elaborator.async_body = source.async_span.has_value();
    elaborator.lexical_class =
        symbol.class_operation ? std::optional(symbol.class_operation->owner) : std::nullopt;
    for (auto i = 0uz; i < source.parameters.size(); ++i) {
        auto parameter =
            elaborator.add_parameter(source.parameters[i], function.signature.parameters[i]);
        if (!parameter) {
            state = Failed {.failure = parameter.error()};
            co_return std::unexpected(parameter.error());
        }
    }
    auto body = co_await elaborator.run(std::get<ASTFunctionBody>(source.implementation).body);
    if (!body) {
        state = Failed {.failure = body.error()};
        co_return std::unexpected(body.error());
    }
    if (function.infer_result) {
        draft->complete_generic_callable_result(definition, elaborator.inferred_result_type());
    }
    if (elaborator.observe_sources) {
        draft->source_analysis()->add_body(std::move(elaborator.source_occurrences));
    }
    draft->set_generic_callable_body(definition, std::move(*body));
    state = Complete {};
    co_return {};
}
