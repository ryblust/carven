module carven:semantic.analysis.decl.function.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.decl;
import :semantic.analysis.expr.scope;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

auto DeclResolver::resolve_function(
    const CatalogSymbol& symbol,
    const CatalogFunctionForm& form,
    ASTView syntax,
    const ASTFunctionDecl& function,
    Span item_span
) noexcept -> AnalysisResult<void> {
    const auto cpp_import = std::holds_alternative<ASTCppImportForm>(function.implementation);
    const auto entry = symbol.name == "main" && !cpp_import;
    if (entry && function.parameters.size() > 1uz) {
        return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            function.name_span,
            DiagnosticCode::EntryParameters,
            "entry function accepts zero parameters or one untyped command-line argument"
        ));
    }

    auto parameter_names = std::flat_map<std::string, Span, std::less<>>();
    for (const auto& parameter : function.parameters) {
        const auto* named = std::get_if<ASTNamedBindingTarget>(&parameter.target);
        if (named == nullptr) {
            continue;
        }
        auto name = draft.source_slice_copy(symbol.module_id, named->name_span);
        const auto [prior, inserted] = parameter_names.emplace(name, named->name_span);
        if (inserted) {
            continue;
        }
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::NameDuplicateParameter,
            "a function parameter name is declared more than once"
        );
        diagnostic.primary(
            locate(declaration_source_id(draft, symbol.module_id), named->name_span),
            "duplicate parameter"
        );
        diagnostic.related(
            locate(declaration_source_id(draft, symbol.module_id), prior->second),
            "first declaration"
        );
        return std::unexpected(draft.diagnostics().error(diagnostic.build()));
    }

    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        const auto access = semantic_access_mode(parameter.access);
        if (entry && access != AccessMode::Read) {
            return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                parameter.access.marker.value_or(parameter.span),
                DiagnosticCode::EntryParameters,
                "entry command-line argument must use Read access"
            ));
        }
        if (entry && parameter.type.has_value()) {
            return std::unexpected(declaration_failure(
                draft,
                symbol.module_id,
                parameter.span,
                DiagnosticCode::EntryParameters,
                "entry command-line argument must not have an explicit type"
            ));
        }
        if (!parameter.type.has_value()) {
            if (!entry) {
                return std::unexpected(declaration_failure(
                    draft,
                    symbol.module_id,
                    parameter.span,
                    DiagnosticCode::TypeParameterAnnotation,
                    "function parameters require an explicit type"
                ));
            }
            parameters.push_back({
                .access = AccessMode::Read,
                .type = draft.intern_builtin_type(BuiltinType::EntryArgs),
            });
            continue;
        }
        auto type =
            resolve_value_type(symbol.module_id, syntax, *parameter.type, "function parameter");
        if (!type.has_value()) {
            return std::unexpected(type.error());
        }
        parameters.push_back({.access = access, .type = *type});
    }

    auto result = ConstructionTypeRef {draft.intern_builtin_type(BuiltinType::Void)};
    if (function.result_type.has_value()) {
        auto resolved = resolve_type(symbol.module_id, syntax, *function.result_type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        result = *resolved;
    }

    auto failures = std::optional<FailureTermID>();
    auto policy = FailureContractPolicy::Declared;
    if (function.throw_clause.has_value()) {
        auto resolved = resolve_failures(symbol.module_id, syntax, *function.throw_clause);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        failures = draft.add_concrete_failure_term(std::move(*resolved));
    } else if (cpp_import) {
        failures = draft.add_empty_failure_term();
    } else {
        failures = draft.add_empty_failure_term();
        policy = !entry && symbol.visibility == DeclarationVisibility::Module
            ? FailureContractPolicy::Inferred
            : FailureContractPolicy::UndeclaredExplicit;
    }

    auto boundary = validate_cpp_boundary_declaration(
        draft,
        symbol.module_id,
        syntax,
        function,
        parameters,
        result
    );
    if (!boundary.has_value()) {
        return std::unexpected(boundary.error());
    }
    if (form.function.index() >= functions.size()
        || form.callable.index() >= callable_contracts.size()) {
        invariant_violation("function declaration identity is outside its reserved table");
    }
    functions[form.function.index()] = FunctionDeclaration {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_source_origin(draft, symbol.module_id, item_span),
        .visibility = symbol.visibility,
        .callable = form.callable,
        .entry_point = entry ? std::optional(
                                   function.parameters.empty() ? EntryPointKind::NoArguments
                                                               : EntryPointKind::WithArguments
                               )
                             : std::nullopt,
        .cpp_export_origin = function.cpp_export.has_value()
            ? std::optional(
                  declaration_source_origin(draft, symbol.module_id, function.cpp_export->span)
              )
            : std::nullopt,
    };
    callable_contracts[form.callable.index()] = ConstructionCallableContract {
        .parameters = std::move(parameters),
        .result = result,
        .failures = *failures,
        .policy = policy,
    };
    if (cpp_import) {
        cpp_import_origins[form.callable.index()] = declaration_source_origin(
            draft,
            symbol.module_id,
            std::get<ASTCppImportForm>(function.implementation).span
        );
    }
    return {};
}
