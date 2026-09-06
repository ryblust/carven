module carven:frontend.program.parse.impl;

import :compiler.request;
import :diagnostics.builder;
import :diagnostics.sink;
import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.lex;
import :frontend.parse;
import :frontend.program.parse;
import :frontend.program.verify;
import :frontend.program;
import :source.provenance;
import :support.invariant;
import :support.visit;
import std;

class SyntaxProgramBuilder final {
public:
    SyntaxProgramBuilder(
        CompilationProvenance provenance_value,
        ResolvedModuleImportGraph import_graph
    ) noexcept
        : provenance(std::move(provenance_value)),
          resolved_import_graph(std::move(import_graph)) {}

    SyntaxProgramBuilder(const SyntaxProgramBuilder&) = delete;
    SyntaxProgramBuilder(SyntaxProgramBuilder&&) = default;
    ~SyntaxProgramBuilder() = default;

    auto operator=(const SyntaxProgramBuilder&) -> SyntaxProgramBuilder& = delete;
    auto operator=(SyntaxProgramBuilder&&) -> SyntaxProgramBuilder& = default;

    auto define_module_syntax(ProgramModuleID module_id, SyntaxTree syntax_tree) noexcept -> void {
        const auto expected_module_id = provenance.view().module_id_at(syntax_by_module.size());
        if (expected_module_id != module_id) {
            invariant_violation("syntax modules must be defined in provenance module order");
        }
        syntax_by_module.push_back(std::move(syntax_tree));
    }

    auto finish() && noexcept -> SyntaxProgram {
        auto program = SyntaxProgram(SyntaxProgramParts(
            std::move(provenance),
            std::move(syntax_by_module),
            std::move(resolved_import_graph)
        ));
        const auto verification = verify_syntax_program(program);
        if (!verification.has_value()) {
            invariant_violation(verification.error().message);
        }
        return program;
    }

private:
    CompilationProvenance provenance;
    std::vector<SyntaxTree> syntax_by_module;
    ResolvedModuleImportGraph resolved_import_graph;
};

namespace {

auto compilation_input_error(std::string message) noexcept -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::CompilationInput, std::move(message)).build();
}

auto import_error(SourceID source, std::string message, Span span) noexcept -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::ImportResolution, std::move(message))
        .primary(locate(source, span))
        .build();
}

auto append_domain_prefix(
    std::vector<std::string_view>& components,
    const ModuleDomainPrefix& prefix
) noexcept -> void {
    if (const auto craft_name = prefix.craft_name()) {
        components.push_back("crafts");
        components.push_back(*craft_name);
    }
}

enum class ModuleReferenceResolutionError {
    InvalidCanonicalPath,
    EscapesModuleDomain,
};

auto resolve_import_path(
    std::string_view source,
    const CanonicalModulePath& importer_path,
    const ASTModuleReference& reference
) noexcept -> std::expected<CanonicalModulePath, ModuleReferenceResolutionError> {
    auto components = std::vector<std::string_view>();
    std::visit(
        Overloaded {
            [&](const ASTDomainRootModuleReference& value) noexcept {
                append_domain_prefix(components, importer_path.module_domain_prefix());
                for (const auto component : value.components) {
                    components.push_back(slice(source, component));
                }
            },
            [&](const ASTParentRelativeModuleReference& value) noexcept {
                append_domain_prefix(components, importer_path.module_domain_prefix());
                const auto relative = importer_path.domain_relative_components();
                for (auto index = 0uz; index + 1uz < relative.size(); ++index) {
                    components.push_back(relative[index]);
                }
                for (const auto component : value.components) {
                    components.push_back(slice(source, component));
                }
            },
            [&](const ASTCraftQualifiedModuleReference& value) noexcept {
                components.push_back("crafts");
                components.push_back(slice(source, value.name_span));
                for (const auto component : value.components) {
                    components.push_back(slice(source, component));
                }
            },
        },
        reference.value
    );
    auto resolved = CanonicalModulePath::from_components(components);
    if (!resolved.has_value()) {
        return std::unexpected(ModuleReferenceResolutionError::InvalidCanonicalPath);
    }
    const auto domain_local =
        !std::holds_alternative<ASTCraftQualifiedModuleReference>(reference.value);
    if (domain_local && !same_module_domain(importer_path, *resolved)) {
        return std::unexpected(ModuleReferenceResolutionError::EscapesModuleDomain);
    }
    return std::move(*resolved);
}

auto close_import_graph(
    CompilationProvenanceView provenance,
    std::span<const SyntaxTree> syntax_trees
) noexcept -> std::expected<ResolvedModuleImportGraph, Diagnostics> {
    auto result = ResolvedModuleImportGraph();
    auto diagnostics = Diagnostics();
    for (auto index = 0uz; index < syntax_trees.size(); ++index) {
        const auto module_id = provenance.module_id_at(index);
        const auto& module_record = provenance.module_record(module_id);
        const auto& source = provenance.source_snapshot(module_record.source_id);
        const auto ast = syntax_trees[index].view();
        auto imports = std::vector<ResolvedModuleImport>();
        imports.reserve(ast.ast_module().module_imports.size());
        for (const auto declaration : ast.ast_module().module_imports) {
            const auto& module_import = ast.module_import(declaration);
            const auto resolved =
                resolve_import_path(source.text(), module_record.path, module_import.module_reference);
            if (!resolved.has_value()) {
                diagnostics.push_back(import_error(
                    source.manager_source_id(),
                    resolved.error() == ModuleReferenceResolutionError::EscapesModuleDomain
                        ? "domain-local module reference escapes the importer's module domain"
                        : "module reference does not form a canonical path",
                    module_import.module_reference.span
                ));
                continue;
            }
            const auto target = provenance.find_program_module(*resolved);
            if (!target.has_value()) {
                diagnostics.push_back(import_error(
                    source.manager_source_id(),
                    std::format(
                        "imported module '{}' is not present in this compilation batch",
                        resolved->value()
                    ),
                    module_import.module_reference.span
                ));
                continue;
            }
            if (*target == module_id) {
                diagnostics.push_back(import_error(
                    source.manager_source_id(),
                    "a module cannot import itself",
                    module_import.module_reference.span
                ));
                continue;
            }
            imports.push_back({.declaration = declaration, .target = *target});
        }
        result.push_back(std::move(imports));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return result;
}

auto validate_inputs(
    const SourceManager& sources,
    std::span<const CompilationModuleInput> inputs
) noexcept -> Diagnostics {
    auto diagnostics = Diagnostics();
    if (inputs.empty()) {
        diagnostics.push_back(
            compilation_input_error("a compilation requires at least one source module")
        );
        return diagnostics;
    }

    for (auto index = 0uz; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        if (!sources.contains(input.source_id)) {
            diagnostics.push_back(compilation_input_error(
                std::format(
                    "source snapshot {} is not present in the supplied source manager",
                    input.source_id.index()
                )
            ));
        }
        for (auto earlier = 0uz; earlier < index; ++earlier) {
            if (inputs[earlier].source_id == input.source_id) {
                diagnostics.push_back(compilation_input_error(
                    std::format(
                        "source snapshot {} is supplied more than once",
                        input.source_id.index()
                    )
                ));
                break;
            }
        }
        for (auto earlier = 0uz; earlier < index; ++earlier) {
            if (inputs[earlier].module_path == input.module_path) {
                diagnostics.push_back(compilation_input_error(
                    std::format("duplicate module path '{}'", input.module_path.value())
                ));
                break;
            }
        }
    }
    return diagnostics;
}

} // namespace

auto parse_program(const SourceManager& sources, CompilationRequest request) noexcept
    -> std::expected<SyntaxProgram, Diagnostics> {
    const auto inputs = request.modules;
    auto input_diagnostics = validate_inputs(sources, inputs);
    if (!input_diagnostics.empty()) {
        return std::unexpected(std::move(input_diagnostics));
    }

    auto ordered_inputs = std::vector<const CompilationModuleInput*>();
    ordered_inputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        ordered_inputs.push_back(std::addressof(input));
    }
    std::ranges::sort(
        ordered_inputs,
        {},
        [](const CompilationModuleInput* input) static noexcept -> const CanonicalModulePath& {
            return input->module_path;
        }
    );

    auto provenance_construction = CompilationProvenanceBuilder();
    auto program_module_ids = std::vector<ProgramModuleID>();
    program_module_ids.reserve(ordered_inputs.size());
    for (const auto* input : ordered_inputs) {
        const auto program_source_id =
            provenance_construction.intern_source_snapshot(sources.view(input->source_id));
        program_module_ids.push_back(provenance_construction.append_module({
            .source_id = program_source_id,
            .path = input->module_path,
        }));
    }

    auto diagnostics = DiagnosticSink();
    auto syntax_trees = std::vector<SyntaxTree>();
    syntax_trees.reserve(ordered_inputs.size());
    for (const auto* input : ordered_inputs) {
        auto lexical = lex(sources.view(input->source_id));
        const auto lexical_has_errors = has_errors(lexical);
        for (auto& diagnostic : lexical.diagnostics) {
            diagnostics.emit(std::move(diagnostic));
        }
        if (lexical_has_errors) {
            continue;
        }

        auto syntax_tree = ::parse(sources, lexical.value);
        if (!syntax_tree.has_value()) {
            for (auto& diagnostic : syntax_tree.error()) {
                diagnostics.emit(std::move(diagnostic));
            }
            continue;
        }
        syntax_trees.push_back(std::move(*syntax_tree));
    }
    if (!diagnostics.empty()) {
        return std::unexpected(diagnostics.take());
    }

    auto provenance = std::move(provenance_construction).finish();
    auto import_graph = close_import_graph(provenance.view(), syntax_trees);
    if (!import_graph.has_value()) {
        return std::unexpected(std::move(import_graph.error()));
    }
    auto construction = SyntaxProgramBuilder(std::move(provenance), std::move(*import_graph));
    for (auto index = 0uz; index < syntax_trees.size(); ++index) {
        construction.define_module_syntax(
            program_module_ids[index],
            std::move(syntax_trees[index])
        );
    }
    return std::move(construction).finish();
}
