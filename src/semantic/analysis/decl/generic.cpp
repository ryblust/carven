module carven:semantic.analysis.decl.generic.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.semir.generic;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

auto DeclResolver::resolve_generic_declaration(
    const CatalogSymbol& symbol,
    const CatalogGenericForm& form
) noexcept -> AnalysisTask<void> {
    const auto syntax = draft.syntax_tree(symbol.module_id).view();
    if (!symbol.item_id) {
        invariant_violation("generic declaration requires a source item");
    }
    const auto& item = syntax.item(*symbol.item_id);
    const auto* record = std::get_if<ASTRecordDecl>(&item.value);
    const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value);
    const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
    auto source_parameters = std::vector<const ASTGenericParameter*>();
    auto parameters = std::vector<ProgramSpellingID>();
    auto parameter_names = std::map<std::string, Span, std::less<>>();
    auto policies = std::vector<GenericParameterKind>();
    auto bounds = std::vector<std::vector<BuiltinCapability>>();
    auto owner = std::optional<GenericDeclarationID>();
    auto owner_parameter_count = 0uz;
    if (symbol.class_operation) {
        if (const auto* generic_owner =
                std::get_if<GenericDeclarationID>(&symbol.class_operation->owner)) {
            owner = *generic_owner;
            const auto* owner_symbol = catalog.symbol(catalog.generic_symbol(*owner));
            auto resolved = co_await resolve(
                owner_symbol->symbol_id,
                symbol.module_id,
                symbol.declaration_span
            );
            if (!resolved) {
                co_return std::unexpected(resolved.error());
            }
            auto inherited = draft.generic_parameter_contract_copy(*owner);
            parameters = std::move(inherited.parameters);
            policies = std::move(inherited.parameter_kinds);
            bounds = std::move(inherited.parameter_bounds);
            for (const auto parameter : parameters) {
                parameter_names.emplace(
                    draft.spelling_copy(parameter),
                    owner_symbol->declaration_span
                );
            }
            owner_parameter_count = parameters.size();
        }
    }
    const auto* clause = record ? &record->type_parameters
        : enumeration           ? &enumeration->type_parameters
        : function              ? &function->type_parameters
                                : nullptr;
    if (clause && *clause) {
        for (const auto& parameter : (*clause)->parameters) {
            source_parameters.push_back(&parameter);
        }
    }
    if (source_parameters.empty() && parameters.empty()) {
        invariant_violation("generic catalog identity does not name parameterized source");
    }
    for (const auto* source_parameter : source_parameters) {
        const auto& parameter = *source_parameter;
        const auto span = parameter.name_span;
        policies.push_back(
            parameter.kind == ASTGenericParameterKind::Type ? GenericParameterKind::Type
                                                            : GenericParameterKind::FailureSet
        );
        auto name = draft.source_slice_copy(symbol.module_id, span);
        if (name == "_" || source_type_name_is_reserved(name)) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                span,
                DiagnosticCode::TypeGenericDefinition,
                "type parameter requires an unreserved name"
            ));
        }
        const auto [prior, inserted] = parameter_names.emplace(name, span);
        if (!inserted) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::TypeGenericDefinition,
                "a type parameter is declared more than once"
            );
            diagnostic.primary(
                locate(declaration_source_id(draft, symbol.module_id), span),
                "duplicate parameter"
            );
            diagnostic.related(
                locate(declaration_source_id(draft, symbol.module_id), prior->second),
                "first declaration"
            );
            co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        parameters.push_back(draft.intern_spelling(name));
    }
    const auto context = GenericTypeContext {
        .definition = form.definition,
        .parameters = parameters,
        .parameter_kinds = policies
    };
    for (const auto* source_parameter : source_parameters) {
        const auto& parameter = *source_parameter;
        const auto span = parameter.name_span;
        auto requirements = std::vector<BuiltinCapability>();
        if (parameter.kind == ASTGenericParameterKind::FailureSet && !parameter.bounds.empty()) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                span,
                DiagnosticCode::TypeGenericDefinition,
                "capability bounds require an object type parameter"
            ));
        }
        for (const auto bound : parameter.bounds) {
            auto capability = resolve_source_capability(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                bound,
                &context
            );
            if (!capability) {
                co_return std::unexpected(capability.error());
            }
            if (std::ranges::contains(requirements, *capability)) {
                co_return std::unexpected(declaration_failure(
                    draft,
                    symbol.module_id,
                    syntax.type(bound).span,
                    DiagnosticCode::TypeGenericDefinition,
                    "capability bound is repeated"
                ));
            }
            requirements.push_back(*capability);
        }
        bounds.push_back(std::move(requirements));
    }
    auto scope = ConstantScope {
        .resolver = *this,
        .module_id = symbol.module_id,
        .syntax = syntax,
        .generic_context = &context
    };
    const auto extent = [&](ASTExprID expression) noexcept {
        return scope.evaluate_array_extent(expression);
    };
    auto contract = GenericDeclarationContract {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_source_origin(draft, symbol.module_id, item.span),
        .visibility = symbol.visibility,
        .parameter_scope = std::nullopt,
        .parameters = {},
        .audience_dependencies = {},
        .parameter_kinds = std::move(policies),
        .parameter_bounds = std::move(bounds),
        .implementation_dependencies =
            GenericImplementationDependencies {.types = {}, .callables = {}, .definitions = {}}
    };
    auto member_names = std::map<std::string, Span, std::less<>>();
    auto visited_surface_types = std::flat_set<ConstructionTypeRef>();
    const auto collect_surface =
        [&](auto&& self, ConstructionTypeRef type, ProgramOriginID origin) noexcept -> void {
        if (!visited_surface_types.insert(type).second) {
            return;
        }
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            contract.audience_dependencies.push_back({.reference = *concrete, .origin = origin});
            return;
        }
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            visit_construction_type_children(
                draft.construction_type_copy(*term),
                [&](auto child) noexcept { self(self, child, origin); }
            );
            return;
        }
        draft.generic_type_copy(std::get<GenericTypeID>(type))
            .visit(
                Overloaded {
                    [&](TypeID concrete) noexcept {
                        contract.audience_dependencies.push_back(
                            {.reference = concrete, .origin = origin}
                        );
                    },
                    [](const GenericTypeParameter&) static noexcept {},

                    [&](const GenericArrayType& array) noexcept {
                        self(self, array.element, origin);
                    },
                    [&](const GenericTupleType& tuple) noexcept {
                        for (const auto element : tuple.elements) {
                            self(self, element, origin);
                        }
                    },
                    [&](const GenericSliceType& slice) noexcept {
                        self(self, slice.element, origin);
                    },
                    [&](const GenericVectorType& sequence) noexcept {
                        self(self, sequence.element, origin);
                    },
                    [&](const GenericPointerType& pointer) noexcept {
                        self(self, pointer.target, origin);
                    },
                    [&](const GenericOperationType& operation) noexcept {
                        self(self, operation.success, origin);
                        if (const auto* known = std::get_if<FailureSetID>(&operation.failures)) {
                            for (const auto failure : draft.failure_set_copy(*known).members) {
                                contract.audience_dependencies.push_back(
                                    {.reference = failure, .origin = origin}
                                );
                            }
                        }
                    },
                    [&](const GenericCallableViewType& view) noexcept {
                        for (const auto& parameter : view.parameters) {
                            self(self, parameter.type, origin);
                        }
                        self(self, view.result, origin);
                        if (const auto* known = std::get_if<FailureSetID>(&view.failures)) {
                            for (const auto failure : draft.failure_set_copy(*known).members) {
                                contract.audience_dependencies.push_back(
                                    {.reference = failure, .origin = origin}
                                );
                            }
                        }
                    },
                    [&](const GenericCppNamedType& native) noexcept {
                        for (const auto argument : native.arguments) {
                            self(self, argument, origin);
                        }
                    },
                    [&](const GenericCppQueryType& query) noexcept {
                        visit_cpp_query_types(query, [&](ConstructionTypeRef type) noexcept {
                            self(self, type, origin);
                        });
                    },
                    [&]<typename Application>(const Application& application) noexcept
                        requires (std::same_as<Application, GenericNominalApplication>
                                  || std::same_as<Application, GenericClosureType>)
                    {
                        contract.audience_dependencies.push_back(
                            {.reference = application.definition, .origin = origin}
                        );
                        for (const auto& argument : application.arguments) {
                            visit_generic_argument_types(
                                argument,
                                [&](ConstructionTypeRef type) noexcept { self(self, type, origin); }
                            );
                            visit_generic_argument_failures(
                                argument,
                                [&](ConstructionFailureRef effect) noexcept {
                                    if (const auto* known = std::get_if<FailureSetID>(&effect)) {
                                        for (const auto failure :
                                             draft.failure_set_copy(*known).members) {
                                            contract.audience_dependencies.push_back(
                                                {.reference = failure, .origin = origin}
                                            );
                                        }
                                    }
                                }
                            );
                        }
                    },
                    }
            );
    };
    const auto type_reference = [&](GenericTypeID type) noexcept -> ConstructionTypeRef {
        const auto expression = draft.generic_type_copy(type);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return *concrete;
        }
        return type;
    };
    if (function) {
        if (function->async_span && symbol.class_operation && symbol.class_operation->receiver) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                *function->async_span,
                DiagnosticCode::AsyncAdmission,
                "async fn does not admit class receivers"
            ));
        }
        if (function->cpp_export
            || !std::holds_alternative<ASTFunctionBody>(function->implementation)
            || (!symbol.class_operation && symbol.name == "main")) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                function->name_span,
                DiagnosticCode::TypeGenericDefinition,
                "generic functions require a closed Carven body and cannot be an entry or C++ boundary"
            ));
        }
        auto parameter_types = std::vector<ConstructionCallableParameter>();
        auto names = std::set<std::string>();
        const auto check_name = [&](this const auto& self,
                                    const ASTBindingTarget& target) noexcept -> bool {
            if (const auto* named = std::get_if<ASTNamedBindingTarget>(&target)) {
                return names.insert(draft.source_slice_copy(symbol.module_id, named->name_span))
                    .second;
            }
            if (const auto* tuple = std::get_if<UniqueIndirect<ASTTupleBindingTarget>>(&target)) {
                return std::ranges::all_of((*tuple)->elements, [&](const auto& element) noexcept {
                    return self(element);
                });
            }
            return true;
        };
        for (const auto& parameter : function->parameters) {
            if (!check_name(parameter.target)) {
                co_return std::unexpected(declaration_failure(
                    draft,
                    symbol.module_id,
                    parameter.span,
                    DiagnosticCode::NameDuplicateParameter,
                    "a function parameter name is declared more than once"
                ));
            }
            const auto receiver = symbol.class_operation
                && symbol.class_operation->receiver
                && parameter_types.empty();
            if (receiver) {
                if (parameter.const_span || parameter.type) {
                    co_return std::unexpected(declaration_failure(
                        draft,
                        symbol.module_id,
                        parameter.span,
                        DiagnosticCode::TypeParameterAnnotation,
                        "class receiver requires untyped self, &self, or &&self"
                    ));
                }
                auto type = std::optional<ConstructionTypeRef>();
                if (owner) {
                    auto arguments = GenericEnvironment();
                    for (auto i = 0uz; i < owner_parameter_count; ++i) {
                        if (contract.parameter_kinds[i] == GenericParameterKind::Type) {
                            arguments.emplace_back(ConstructionTypeRef(draft.intern_generic_type(
                                GenericTypeParameter {
                                    .definition = form.definition,
                                    .index = static_cast<std::uint32_t>(i)
                                }
                            )));
                        } else {
                            arguments.emplace_back(
                                ConstructionFailureRef(draft.add_generic_failure_parameter(
                                    {form.definition, static_cast<std::uint32_t>(i)}
                                ))
                            );
                        }
                    }
                    type = draft.intern_generic_type(
                        GenericNominalApplication {
                            .definition = *owner,
                            .arguments = std::move(arguments)
                        }
                    );
                } else {
                    type = draft.intern_type(
                        {.value = StructTypeValue {
                             .structure = std::get<StructID>(symbol.class_operation->owner)
                         }}
                    );
                }
                parameter_types.push_back(
                    {.stage = ParameterStage::Runtime,
                     .access = semantic_access_mode(parameter.access),
                     .type = *type}
                );
                continue;
            }
            if (symbol.class_operation
                && draft.source_slice_copy(symbol.module_id, binding_target_span(parameter.target))
                    == "self") {
                co_return std::unexpected(declaration_failure(
                    draft,
                    symbol.module_id,
                    parameter.span,
                    DiagnosticCode::TypeParameterAnnotation,
                    "self must be the first class operation parameter"
                ));
            }
            if (!parameter.type) {
                co_return std::unexpected(declaration_failure(
                    draft,
                    symbol.module_id,
                    parameter.span,
                    DiagnosticCode::TypeParameterAnnotation,
                    "function parameters require an explicit type"
                ));
            }
            auto type = co_await resolve_generic_source_type(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                *parameter.type,
                context,
                extent,
                &requests,
                draft.source_analysis() ? &draft.source_analysis()->declarations() : nullptr
            );
            if (!type) {
                co_return std::unexpected(type.error());
            }
            collect_surface(
                collect_surface,
                *type,
                declaration_source_origin(draft, symbol.module_id, parameter.span)
            );
            parameter_types.push_back(
                {.stage = parameter.const_span ? ParameterStage::Static : ParameterStage::Runtime,
                 .access = semantic_access_mode(parameter.access),
                 .type = type_reference(*type)}
            );
        }
        auto result = ConstructionTypeRef {draft.builtin_type(BuiltinType::Void)};
        if (function->result_type) {
            auto type = co_await resolve_generic_source_type(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                *function->result_type,
                context,
                extent,
                &requests,
                draft.source_analysis() ? &draft.source_analysis()->declarations() : nullptr
            );
            if (!type) {
                co_return std::unexpected(type.error());
            }
            result = type_reference(*type);
            collect_surface(
                collect_surface,
                *type,
                declaration_source_origin(
                    draft,
                    symbol.module_id,
                    syntax.type(*function->result_type).span
                )
            );
        }
        auto failures = ConstructionFailureRef {draft.add_empty_failure_term()};
        auto policy = symbol.visibility == DeclarationVisibility::Module
            ? FailureContractPolicy::Inferred
            : FailureContractPolicy::UndeclaredExplicit;
        if (function->throw_clause) {
            auto resolved = co_await resolve_failures(
                symbol.module_id,
                syntax,
                *function->throw_clause,
                &context
            );
            if (!resolved) {
                co_return std::unexpected(resolved.error());
            }
            failures = *resolved;
            policy = FailureContractPolicy::Declared;
        }
        contract.parameters = std::move(parameters);
        draft.define_generic_declaration(
            form.definition,
            GenericCallableDefinition {
                .contract = std::move(contract),
                .signature =
                    {.execution = function->async_span ? CallableExecutionKind::Async
                                                       : CallableExecutionKind::Synchronous,
                     .parameters = std::move(parameter_types),
                     .result = result,
                     .failures = failures,
                     .policy = policy},
                .is_const = function->const_span.has_value(),
                .infer_result = !function->result_type.has_value(),
                .type_formations = {},
            }
        );
        co_return {};
    }
    if (record) {
        auto fields = std::vector<GenericField>();
        for (const auto& field : record->fields) {
            const auto name = draft.source_slice_copy(symbol.module_id, field.name_span);
            const auto [prior, inserted] = member_names.emplace(name, field.name_span);
            if (!inserted) {
                auto diagnostic = DiagnosticBuilder(
                    DiagnosticCode::NameDuplicateField,
                    "a structure field is declared more than once"
                );
                diagnostic.primary(
                    locate(declaration_source_id(draft, symbol.module_id), field.name_span),
                    "duplicate field"
                );
                diagnostic.related(
                    locate(declaration_source_id(draft, symbol.module_id), prior->second),
                    "first declaration"
                );
                co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
            }
            auto type = (co_await resolve_generic_source_type(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                field.type,
                context,
                extent,
                &requests,
                draft.source_analysis() ? &draft.source_analysis()->declarations() : nullptr
            ));
            if (!type) {
                co_return std::unexpected(type.error());
            }
            fields.push_back(
                {.name = draft.intern_spelling(name),
                 .type = *type,
                 .origin = declaration_source_origin(draft, symbol.module_id, field.span)}
            );
        }
        if (record->kind == ASTRecordKind::Struct) {
            for (const auto& field : fields) {
                collect_surface(collect_surface, field.type, field.origin);
            }
        }
        contract.parameters = std::move(parameters);
        draft.define_generic_declaration(
            form.definition,
            GenericRecordDefinition {
                .contract = std::move(contract),
                .kind =
                    record->kind == ASTRecordKind::Class ? RecordKind::Class : RecordKind::Struct,
                .fields = std::move(fields),
            }
        );
        co_return {};
    }
    if (enumeration->cases.empty()) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            enumeration->name_span,
            DiagnosticCode::TypeEnumEmpty,
            "enum must declare at least one case"
        ));
    }
    const auto payload =
        std::ranges::any_of(enumeration->cases, [](const ASTEnumCase& source_case) static noexcept {
            return !source_case.payload_types.empty();
        });
    if (!payload) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            enumeration->name_span,
            DiagnosticCode::TypeGenericDefinition,
            "type-parameterized numeric enums are not supported"
        ));
    }
    if (enumeration->underlying_type) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            syntax.type(*enumeration->underlying_type).span,
            DiagnosticCode::TypeEnumProfile,
            "payload enum cannot declare an underlying integer type"
        ));
    }
    auto cases = std::vector<GenericEnumCase>();
    for (const auto& source_case : enumeration->cases) {
        const auto name = draft.source_slice_copy(symbol.module_id, source_case.name_span);
        const auto [prior, inserted] = member_names.emplace(name, source_case.name_span);
        if (!inserted) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::NameDuplicateEnumCase,
                "an enum case is declared more than once"
            );
            diagnostic.primary(
                locate(declaration_source_id(draft, symbol.module_id), source_case.name_span),
                "duplicate case"
            );
            diagnostic.related(
                locate(declaration_source_id(draft, symbol.module_id), prior->second),
                "first declaration"
            );
            co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        if (source_case.initializer) {
            co_return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                syntax.expression(*source_case.initializer).span,
                DiagnosticCode::TypeEnumProfile,
                "payload enum case cannot declare a numeric initializer"
            ));
        }
        auto types = std::vector<GenericTypeID>();
        for (const auto source_type : source_case.payload_types) {
            auto type = (co_await resolve_generic_source_type(
                draft,
                catalog,
                import_usage,
                symbol.module_id,
                syntax,
                source_type,
                context,
                extent,
                &requests,
                draft.source_analysis() ? &draft.source_analysis()->declarations() : nullptr
            ));
            if (!type) {
                co_return std::unexpected(type.error());
            }
            types.push_back(*type);
        }
        cases.push_back(
            {.name = draft.intern_spelling(name),
             .origin = declaration_source_origin(draft, symbol.module_id, source_case.span),
             .payload_types = std::move(types)}
        );
    }
    for (const auto& source_case : cases) {
        for (const auto type : source_case.payload_types) {
            collect_surface(collect_surface, type, source_case.origin);
        }
    }
    contract.parameters = std::move(parameters);
    draft.define_generic_declaration(
        form.definition,
        GenericEnumDefinition {
            .contract = std::move(contract),
            .cases = std::move(cases),
        }
    );
    co_return {};
}
