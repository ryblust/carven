module carven:frontend.program.parse.impl;

import :diagnostics.builder;
import :diagnostics.sink;
import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.document;
import :frontend.program;
import :frontend.program.parse;
import :frontend.program.verify;
import :source.batch;
import :source.document;
import :source.provenance;
import :source.standard;
import :support.invariant;
import :support.timing;
import :support.visit;
import std;

class SyntaxProgramBuilder final {
public:
    SyntaxProgramBuilder(
        CompilationProvenance provenance_value,
        ResolvedModuleImportGraph import_graph,
        std::size_t imported_modules
    ) noexcept;
    SyntaxProgramBuilder(const SyntaxProgramBuilder&) = delete;
    SyntaxProgramBuilder(SyntaxProgramBuilder&&) = default;
    ~SyntaxProgramBuilder() = default;
    auto operator=(const SyntaxProgramBuilder&) -> SyntaxProgramBuilder& = delete;
    auto operator=(SyntaxProgramBuilder&&) -> SyntaxProgramBuilder& = delete;
    auto define_module_syntax(ProgramModuleID module_id, SyntaxTree syntax_tree) noexcept -> void;
    auto finish() && noexcept -> SyntaxProgram;

private:
    CompilationProvenance provenance;
    std::vector<std::optional<SyntaxTree>> syntax_by_module;
    ResolvedModuleImportGraph resolved_import_graph;
};

SyntaxProgramBuilder::SyntaxProgramBuilder(
    CompilationProvenance provenance_value,
    ResolvedModuleImportGraph import_graph,
    std::size_t imported_modules
) noexcept
    : provenance(std::move(provenance_value)),
      syntax_by_module(imported_modules),
      resolved_import_graph(std::move(import_graph)) {}

auto SyntaxProgramBuilder::define_module_syntax(
    ProgramModuleID module_id,
    SyntaxTree syntax_tree
) noexcept -> void {
    const auto expected_module_id = provenance.view().module_id_at(syntax_by_module.size());
    if (expected_module_id != module_id) {
        invariant_violation("syntax modules must be defined in provenance module order");
    }
    syntax_by_module.push_back(std::move(syntax_tree));
}

auto SyntaxProgramBuilder::finish() && noexcept -> SyntaxProgram {
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

} // namespace

auto resolve_import_path(
    std::string_view source,
    const CanonicalModulePath& importer_path,
    const ASTModuleReference& reference
) noexcept -> std::expected<CanonicalModulePath, ModuleReferenceResolutionError> {
    const auto domain_prefix = importer_path.module_domain_prefix();
    auto components = std::vector<std::string_view>();
    reference.value.visit(
        Overloaded {
            [&](const ASTDomainRootModuleReference& value) noexcept {
                append_domain_prefix(components, domain_prefix);
                for (const auto component : value.components) {
                    components.push_back(slice(source, component));
                }
            },
            [&](const ASTParentRelativeModuleReference& value) noexcept {
                append_domain_prefix(components, domain_prefix);
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
                const auto craft = slice(source, value.name_span);
                if (craft == "std") {
                    components.push_back("carven");
                }
                components.push_back(craft);
                for (const auto component : value.components) {
                    components.push_back(slice(source, component));
                }
            },
        }
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

namespace {

auto close_import_graph(
    CompilationProvenanceView provenance,
    std::span<const std::optional<SyntaxTree>> syntax_trees
) noexcept -> std::expected<ResolvedModuleImportGraph, Diagnostics> {
    auto result = ResolvedModuleImportGraph();
    auto diagnostics = Diagnostics();
    for (auto index = 0uz; index < syntax_trees.size(); ++index) {
        const auto module_id = provenance.module_id_at(index);
        if (!syntax_trees[index]) {
            result.emplace_back();
            continue;
        }
        const auto& module_record = provenance.module_record(module_id);
        const auto& source = provenance.source_snapshot(module_record.source_id);
        const auto ast = syntax_trees[index]->view();
        auto imports = std::vector<ResolvedModuleImport>();
        imports.reserve(ast.ast_module().module_imports.size());
        for (const auto declaration : ast.ast_module().module_imports) {
            const auto& module_import = ast.module_import(declaration);
            const auto resolved = resolve_import_path(
                source.text(),
                module_record.path,
                module_import.module_reference
            );
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
            const auto provided = find_provided_module(resolved->value());
            if (!target && !provided) {
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
            if (target == module_id) {
                diagnostics.push_back(import_error(
                    source.manager_source_id(),
                    "a module cannot import itself",
                    module_import.module_reference.span
                ));
                continue;
            }
            imports.push_back({
                .declaration = declaration,
                .target = {.source = target, .provided = provided},
            });
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
    std::span<const SourceModuleInput> inputs
) noexcept -> Diagnostics {
    auto diagnostics = Diagnostics();
    if (inputs.empty()) {
        diagnostics.push_back(
            compilation_input_error("a compilation requires at least one source module")
        );
        return diagnostics;
    }

    auto source_ids = std::set<SourceID>();
    auto module_paths = std::set<std::string_view>();
    for (const auto& input : inputs) {
        if (!sources.contains(input.source_id)) {
            diagnostics.push_back(compilation_input_error(
                std::format(
                    "source snapshot {} is not present in the supplied source manager",
                    input.source_id.index()
                )
            ));
        }
        if (!source_ids.insert(input.source_id).second) {
            diagnostics.push_back(compilation_input_error(
                std::format(
                    "source snapshot {} is supplied more than once",
                    input.source_id.index()
                )
            ));
        }
        if (!module_paths.insert(input.module_path.value()).second) {
            diagnostics.push_back(compilation_input_error(
                std::format("duplicate module path '{}'", input.module_path.value())
            ));
        }
    }
    return diagnostics;
}

} // namespace

auto parse_program(
    const SourceManager& sources,
    SourceBatch batch,
    TimingOutput timings,
    std::span<const ParsedSourceInput> parsed,
    std::optional<CompilationProvenance> imported,
    bool source_query
) noexcept -> std::expected<SyntaxProgram, Diagnostics> {
    const auto inputs = batch.modules;
    auto input_diagnostics = validate_inputs(sources, inputs);
    if (!input_diagnostics.empty()) {
        return std::unexpected(std::move(input_diagnostics));
    }

    auto selected_sources = std::set<SourceID>();
    for (const auto& input : inputs) {
        selected_sources.insert(input.source_id);
    }
    auto retained = std::map<SourceID, std::shared_ptr<const ParsedDocument>>();
    for (const auto& input : parsed) {
        if (!input.document
            || !sources.contains(input.source_id)
            || !selected_sources.contains(input.source_id)) {
            input_diagnostics.push_back(
                compilation_input_error("retained syntax is not bound to a selected source")
            );
            continue;
        }
        const auto source = sources.view(input.source_id);
        const auto original = input.document->source();
        if (source.text != original.text || source.origin != original.origin) {
            input_diagnostics.push_back(compilation_input_error(
                "retained syntax does not match its selected source snapshot"
            ));
        }
        if (!retained.emplace(input.source_id, input.document).second) {
            input_diagnostics.push_back(
                compilation_input_error("retained syntax is supplied more than once for a source")
            );
        }
    }
    if (!input_diagnostics.empty()) {
        return std::unexpected(std::move(input_diagnostics));
    }

    auto ordered_inputs = std::vector<const SourceModuleInput*>();
    ordered_inputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        ordered_inputs.push_back(std::addressof(input));
    }
    std::ranges::sort(
        ordered_inputs,
        {},
        [](const SourceModuleInput* input) static noexcept -> const CanonicalModulePath& {
            return input->module_path;
        }
    );

    const auto imported_modules = imported ? imported->view().module_records().size() : 0uz;
    auto provenance_construction = imported ? CompilationProvenanceBuilder(std::move(*imported))
                                            : CompilationProvenanceBuilder();
    auto program_module_ids = std::vector<ProgramModuleID>();
    program_module_ids.reserve(ordered_inputs.size());
    for (const auto* input : ordered_inputs) {
        if (provenance_construction.find_program_module(input->module_path)) {
            input_diagnostics.push_back(compilation_input_error(
                std::format(
                    "source module '{}' is already supplied by checked definitions",
                    input->module_path.value()
                )
            ));
            continue;
        }
        const auto program_source_id =
            provenance_construction.intern_source_snapshot(sources, input->source_id);
        program_module_ids.push_back(provenance_construction.append_module({
            .source_id = program_source_id,
            .path = input->module_path,
        }));
    }

    if (!input_diagnostics.empty()) {
        return std::unexpected(std::move(input_diagnostics));
    }
    auto diagnostics = DiagnosticSink();
    auto syntax_trees = std::vector<std::optional<SyntaxTree>>(imported_modules);
    syntax_trees.reserve(ordered_inputs.size());
    for (const auto* input : ordered_inputs) {
        const auto found = retained.find(input->source_id);
        const auto document = found == retained.end()
            ? parse_document(SourceDocument::retain(sources, input->source_id), timings)
            : found->second;
        for (auto diagnostic : source_query && document->recovered_syntax()
                 ? std::span<const Diagnostic>()
                 : document->diagnostics()) {
            const auto rebind = [&](SourceSpan& span) noexcept {
                span.source_id = input->source_id;
            };
            if (diagnostic.attachment.primary) {
                rebind(diagnostic.attachment.primary->span);
            }
            for (auto& label : diagnostic.attachment.related) {
                rebind(label.span);
            }
            for (auto& note : diagnostic.attachment.notes) {
                if (note.span) {
                    rebind(*note.span);
                }
            }
            for (auto& fix : diagnostic.attachment.fixes) {
                for (auto& edit : fix.edits) {
                    rebind(edit.span);
                }
            }
            diagnostics.emit(std::move(diagnostic));
        }
        auto syntax_tree = source_query ? document->bind_recovered(input->source_id)
                                        : document->bind(input->source_id);
        if (syntax_tree) {
            syntax_trees.push_back(std::move(*syntax_tree));
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(diagnostics.take());
    }

    auto provenance = std::move(provenance_construction).finish();
    auto import_graph = close_import_graph(provenance.view(), syntax_trees);
    if (!import_graph.has_value()) {
        return std::unexpected(std::move(import_graph.error()));
    }
    auto construction =
        SyntaxProgramBuilder(std::move(provenance), std::move(*import_graph), imported_modules);
    for (auto index = 0uz; index < program_module_ids.size(); ++index) {
        construction.define_module_syntax(
            program_module_ids[index],
            std::move(*syntax_trees[index + imported_modules])
        );
    }
    return std::move(construction).finish();
}
