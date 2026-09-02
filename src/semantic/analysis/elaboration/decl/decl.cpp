module carven:semantic.analysis.elaboration.decl.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :frontend.ast.interop;
import :frontend.literal;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.decl;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.types;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.interop;
import :semantic.hir.symbol;
import :semantic.visibility;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto build_function_body(
    ModuleAnalysis& source,
    FunctionID function_id,
    const ASTFunctionDecl& function,
    HIRModule& target
) noexcept -> void {
    const auto callable = source.declarations().function(function_id).callable;
    if (std::holds_alternative<ASTFunctionBody>(function.implementation)) {
        auto body = BodyElaborator(source).elaborate_function(function, callable);
        source.builder()
            .define_callable_body(callable, std::move(body.parameters), body.scope, body.root);
    }
    target.items.push_back(function_id);
}

auto build_test(
    ModuleAnalysis& source,
    const ASTTestDecl& test,
    Span item_span,
    HIRModule& target
) noexcept -> void {
    if (test.name == "main") {
        source.emit(test.name_span, "a test cannot be named 'main'", DiagnosticCode::TestMainName);
    }
    if (const auto prior = source.tests().register_name(test.name, test.name_span)) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TestDuplicateName,
            "a test name is defined more than once"
        );
        diagnostic.primary(locate(source.source_id(), test.name_span), "duplicate test name");
        diagnostic.related(locate(source.source_id(), *prior), "first definition");
        source.emit(diagnostic.build());
    }
    const auto body = BodyElaborator(source).elaborate_test(
        test.body,
        builtin(source, test.keyword_span, HIRBuiltinType::Void)
    );
    target.items.push_back(source.builder().append_test(
        source.origin(item_span),
        source.builder().intern_string(test.name),
        source.builder().block(body).scope,
        body
    ));
}

} // namespace

auto build_module(ModuleAnalysis& source) noexcept -> void {
    const auto* catalog_module = source.catalog().find_module(source.module_id());
    if (catalog_module == nullptr) {
        invariant_violation("module body construction has no catalog schedule");
    }
    const auto ast = source.syntax();
    auto& target = source.builder().hir_module(source.module_id());
    target.cpp_header_dependencies.clear();
    target.cpp_header_dependencies.reserve(ast.ast_module().cpp_header_imports.size());
    for (const auto& header : ast.ast_module().cpp_header_imports) {
        target.cpp_header_dependencies.push_back({
            .delimiter = header.delimiter == ASTCppHeaderDelimiter::AngleBrackets
                ? HIRCppHeaderDelimiter::AngleBrackets
                : HIRCppHeaderDelimiter::Quotes,
            .name = source.builder().intern_string(source.spelling(header.name_span)),
        });
    }
    target.cpp_source_payload_origins.clear();
    target.cpp_source_payload_origins.reserve(ast.ast_module().cpp_source_fragments.size());
    for (const auto& fragment : ast.ast_module().cpp_source_fragments) {
        target.cpp_source_payload_origins.push_back(source.origin(fragment.payload_span));
    }
    target.items.clear();
    target.items.reserve(catalog_module->items.size());
    for (const auto& scheduled : catalog_module->items) {
        const auto& item = ast.item(scheduled.item_id);
        std::visit(
            Overloaded {
                [&](FunctionID function_id) noexcept {
                    const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
                    if (function == nullptr) {
                        invariant_violation("function schedule does not match its syntax item");
                    }
                    build_function_body(source, function_id, *function, target);
                },
                [&](StructID struct_id) noexcept { target.items.push_back(struct_id); },
                [&](EnumID enum_id) noexcept { target.items.push_back(enum_id); },
                [&](const CatalogTestForm&) noexcept {
                    const auto* test = std::get_if<ASTTestDecl>(&item.value);
                    if (test == nullptr) {
                        invariant_violation("test schedule does not match its syntax item");
                    }
                    build_test(source, *test, item.span, target);
                },
            },
            scheduled.form
        );
    }
}
