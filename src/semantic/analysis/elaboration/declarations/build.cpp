module carven:semantic.analysis.elaboration.declarations.build.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.declarations;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.hir.type;
import std;

namespace {

auto elaborate_function_contract(
    ModuleAnalysis& module_analysis,
    DeclarationDefinitionSink& definitions,
    SymbolID symbol,
    const ASTFunctionDecl& function
) noexcept -> bool {
    const auto* catalog_symbol = module_analysis.catalog().symbol(symbol);
    const auto* catalog_function = catalog_symbol == nullptr
        ? nullptr
        : std::get_if<CatalogFunctionForm>(&catalog_symbol->form);
    if (catalog_function == nullptr) {
        invariant_violation("function syntax does not match its catalog identity");
    }
    auto scopes = ScopeStack();
    auto& builder = module_analysis.builder();
    const auto function_name = module_analysis.spelling(function.name_span);
    auto entry_point = std::optional<HIREntryPointKind>();
    if (function_name == "main") {
        const auto current_origin = module_analysis.origin(function.name_span);
        if (const auto first_entry = module_analysis.entry_points().origin()) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::EntryDuplicate,
                "a program may define only one entry function"
            );
            diagnostic.primary(
                locate(module_analysis.source_id(), function.name_span),
                "duplicate entry"
            );
            diagnostic.related(module_analysis.diagnostic_span(*first_entry), "first entry");
            module_analysis.emit(diagnostic.build());
        } else {
            module_analysis.entry_points().record(current_origin);
        }
        entry_point = function.parameters.empty() ? HIREntryPointKind::NoArguments
                                                  : HIREntryPointKind::WithArguments;
    }
    const auto entry_with_arguments = function_name == "main"
        && function.parameters.size() == 1
        && !function.parameters.front().type.has_value();
    if (function_name == "main" && function.parameters.size() > 1) {
        module_analysis.emit(
            function.name_span,
            "entry function accepts zero parameters or one untyped command-line argument",
            DiagnosticCode::EntryParameters
        );
    }
    auto parameter_types = std::vector<HIRFunctionParameterType>();
    parameter_types.reserve(function.parameters.size());
    for (auto index = 0uz; index < function.parameters.size(); ++index) {
        const auto& parameter = function.parameters[index];
        const auto access = access_mode(parameter.access);
        if (function_name == "main" && access != HIRAccessMode::Read) {
            module_analysis.emit(
                *parameter.access.marker,
                "entry command-line argument must use Read access",
                DiagnosticCode::EntryParameters
            );
        }
        if (parameter.type.has_value()) {
            if (function_name == "main") {
                module_analysis.emit(
                    parameter.span,
                    "entry command-line argument must not have an explicit type",
                    DiagnosticCode::EntryParameters
                );
            }
            const auto parameter_type =
                build_type(module_analysis, scopes, root_body_control(), *parameter.type);
            parameter_types.push_back({
                .access = access,
                .type = require_value_type(
                    module_analysis,
                    parameter_type,
                    module_analysis.syntax().type(*parameter.type).span,
                    ValueTypeRole::FunctionParameter
                ),
            });
        } else if (entry_with_arguments && index == 0) {
            parameter_types.push_back({
                .access = HIRAccessMode::Read,
                .type = builtin(module_analysis, parameter.span, HIRBuiltinType::EntryArgs),
            });
        } else {
            module_analysis.emit(
                parameter.span,
                "function parameters require an explicit type",
                DiagnosticCode::TypeParameterAnnotation
            );
            parameter_types.push_back({
                .access = access,
                .type = error_type(module_analysis, parameter.span),
            });
        }
    }
    const auto result = function.result_type.has_value()
        ? build_type(module_analysis, scopes, root_body_control(), *function.result_type)
        : builtin(module_analysis, function.name_span, HIRBuiltinType::Void);
    const auto failures = function.throw_clause.has_value()
        ? normalized_failures(module_analysis, scopes, root_body_control(), *function.throw_clause)
        : std::vector<HIRTypeID>();
    const auto failure_contract = function.throw_clause.has_value()
        ? HIRFailureContractKind::Declared
        : (catalog_symbol->visibility == DeclarationVisibility::Module
               ? HIRFailureContractKind::Inferred
               : HIRFailureContractKind::UndeclaredPublished);
    const auto callable =
        builder.append_callable(parameter_types, result, failures, failure_contract);
    definitions.define(
        catalog_function->function,
        FunctionContractState {
            .origin =
                module_analysis.origin(module_analysis.syntax().item(catalog_symbol->item_id).span),
            .visibility = catalog_symbol->visibility,
            .name = builder.intern_string(catalog_symbol->name),
            .callable = callable,
            .result = result,
            .result_origin = module_analysis.origin(
                function.result_type.has_value()
                    ? module_analysis.syntax().type(*function.result_type).span
                    : function.name_span
            ),
            .symbol = symbol,
            .entry_point = entry_point,
        }
    );
    set_symbol_type(module_analysis, symbol, builder.intern_function_type(callable));
    return true;
}

auto elaborate_struct_contract(
    ModuleAnalysis& module_analysis,
    DeclarationDefinitionSink& definitions,
    SymbolID symbol,
    const ASTStructDecl& structure
) noexcept -> bool {
    const auto* catalog_symbol = module_analysis.catalog().symbol(symbol);
    const auto* catalog_structure =
        catalog_symbol == nullptr ? nullptr : std::get_if<CatalogStructForm>(&catalog_symbol->form);
    if (catalog_structure == nullptr) {
        invariant_violation("structure syntax does not match its catalog identity");
    }
    auto scopes = ScopeStack();
    auto& builder = module_analysis.builder();
    auto fields = std::vector<HIRStructField>();
    auto names = std::flat_map<std::string, Span, std::less<>>();
    fields.reserve(structure.fields.size());
    for (const auto& field : structure.fields) {
        const auto name = module_analysis.spelling(field.name_span);
        const auto found = names.find(name);
        if (found != names.end()) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::NameDuplicateField,
                "a structure field is declared more than once"
            );
            diagnostic.primary(
                locate(module_analysis.source_id(), field.name_span),
                "duplicate field"
            );
            diagnostic.related(
                locate(module_analysis.source_id(), found->second),
                "first declaration"
            );
            module_analysis.emit(diagnostic.build());
        } else {
            names.emplace(std::string(name), field.name_span);
        }
        const auto field_type =
            build_type(module_analysis, scopes, root_body_control(), field.type);
        fields.push_back({
            .name = builder.intern_string(name),
            .type = require_value_type(
                module_analysis,
                field_type,
                module_analysis.syntax().type(field.type).span,
                ValueTypeRole::StructureField
            ),
            .origin = module_analysis.origin(field.span),
        });
    }
    definitions.define(
        catalog_structure->structure,
        StructContractState {
            .origin =
                module_analysis.origin(module_analysis.syntax().item(catalog_symbol->item_id).span),
            .visibility = catalog_symbol->visibility,
            .name = builder.intern_string(catalog_symbol->name),
            .fields = std::move(fields),
            .symbol = symbol,
        }
    );
    return true;
}

auto elaborate_enum_contract(
    ModuleAnalysis& module_analysis,
    DeclarationDefinitionSink& definitions,
    SymbolID symbol,
    const ASTEnumDecl& enumeration
) noexcept -> bool {
    auto scopes = ScopeStack();
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto nominal = symbol_type(module_analysis, symbol);
    auto names = std::flat_map<std::string, Span, std::less<>>();
    const auto* catalog_symbol = module_analysis.catalog().symbol(symbol);
    const auto* catalog_enumeration =
        catalog_symbol == nullptr ? nullptr : std::get_if<CatalogEnumForm>(&catalog_symbol->form);
    if (catalog_enumeration == nullptr
        || catalog_enumeration->cases.size() != enumeration.cases.size()) {
        invariant_violation("enum catalog identities do not match the source declaration");
    }
    const auto profile =
        std::ranges::any_of(
            enumeration.cases,
            [](const auto& enum_case) static noexcept { return !enum_case.payload_types.empty(); }
        )
        ? HIREnumProfile::Payload
        : HIREnumProfile::Numeric;
    if (enumeration.cases.empty()) {
        module_analysis.emit(
            enumeration.name_span,
            "enum must declare at least one case",
            DiagnosticCode::TypeEnumEmpty
        );
    }
    if (profile == HIREnumProfile::Payload && enumeration.underlying_type.has_value()) {
        module_analysis.emit(
            ast.type(*enumeration.underlying_type).span,
            "payload enum cannot declare an underlying integer type",
            DiagnosticCode::TypeEnumProfile
        );
    }
    for (auto case_index = 0uz; case_index < enumeration.cases.size(); ++case_index) {
        const auto& enum_case = enumeration.cases[case_index];
        const auto name = module_analysis.spelling(enum_case.name_span);
        const auto found = names.find(name);
        if (found != names.end()) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::NameDuplicateEnumCase,
                "an enum case is declared more than once"
            );
            diagnostic.primary(
                locate(module_analysis.source_id(), enum_case.name_span),
                "duplicate case"
            );
            diagnostic.related(
                locate(module_analysis.source_id(), found->second),
                "first declaration"
            );
            module_analysis.emit(diagnostic.build());
        } else {
            names.emplace(std::string(name), enum_case.name_span);
        }
        const auto case_id = catalog_enumeration->cases[case_index];
        const auto case_symbol = module_analysis.catalog().enum_case_symbol(case_id);
        const auto* case_catalog = module_analysis.catalog().symbol(case_symbol);
        const auto* case_form = case_catalog == nullptr
            ? nullptr
            : std::get_if<CatalogEnumCaseForm>(&case_catalog->form);
        if (case_form == nullptr
            || case_form->owner != catalog_enumeration->enumeration
            || case_form->index != case_index
            || case_form->enum_case != case_id) {
            invariant_violation("enum case catalog identity is inconsistent");
        }
        auto payload_types = std::vector<HIRTypeID>();
        payload_types.reserve(enum_case.payload_types.size());
        for (const auto payload : enum_case.payload_types) {
            const auto payload_type = require_value_type(
                module_analysis,
                build_type(module_analysis, scopes, root_body_control(), payload),
                ast.type(payload).span,
                ValueTypeRole::EnumPayload
            );
            payload_types.push_back(payload_type);
        }
        if (payload_types.empty()) {
            set_symbol_type(module_analysis, case_symbol, nominal);
        } else {
            auto parameters = std::vector<HIRFunctionParameterType>();
            parameters.reserve(payload_types.size());
            for (const auto payload_type : payload_types) {
                parameters.push_back({.access = HIRAccessMode::Read, .type = payload_type});
            }
            set_symbol_type(
                module_analysis,
                case_symbol,
                builder.intern_function_ref_type(std::move(parameters), nominal)
            );
        }
        definitions.define(
            case_id,
            {
                .owner = catalog_enumeration->enumeration,
                .name = builder.intern_string(name),
                .payload_types = std::move(payload_types),
                .symbol = case_symbol,
                .origin = module_analysis.origin(enum_case.span),
            }
        );
    }
    auto underlying_type = std::optional<HIRTypeID>();
    if (profile == HIREnumProfile::Numeric) {
        underlying_type = enumeration.underlying_type.has_value()
            ? build_type(module_analysis, scopes, root_body_control(), *enumeration.underlying_type)
            : builtin(module_analysis, enumeration.name_span, HIRBuiltinType::I32);
    }
    if (underlying_type.has_value()
        && !is_integer(module_analysis, *underlying_type)
        && !std::holds_alternative<HIRErrorTypeValue>(builder.type(*underlying_type).value)) {
        module_analysis.emit(
            enumeration.underlying_type.has_value() ? ast.type(*enumeration.underlying_type).span
                                                    : enumeration.name_span,
            "enum underlying type must be an integer type",
            DiagnosticCode::TypeEnumUnderlying
        );
        underlying_type = builtin(module_analysis, enumeration.name_span, HIRBuiltinType::I32);
    }
    definitions.define(
        catalog_enumeration->enumeration,
        EnumContractState {
            .origin =
                module_analysis.origin(module_analysis.syntax().item(catalog_symbol->item_id).span),
            .visibility = catalog_symbol->visibility,
            .name = builder.intern_string(catalog_symbol->name),
            .underlying_type = underlying_type,
            .cases = catalog_enumeration->cases,
            .profile = profile,
            .symbol = symbol,
        }
    );
    return true;
}

} // namespace

auto elaborate_declaration_contract(
    ModuleAnalysis& module_analysis,
    DeclarationDefinitionSink& definitions,
    SymbolID symbol,
    ASTItemID item_id
) noexcept -> bool {
    const auto& item = module_analysis.syntax().item(item_id);
    if (const auto* function = std::get_if<ASTFunctionDecl>(&item.value)) {
        return elaborate_function_contract(module_analysis, definitions, symbol, *function);
    }
    if (const auto* structure = std::get_if<ASTStructDecl>(&item.value)) {
        return elaborate_struct_contract(module_analysis, definitions, symbol, *structure);
    }
    if (const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value)) {
        return elaborate_enum_contract(module_analysis, definitions, symbol, *enumeration);
    }
    return true;
}
