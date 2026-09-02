module carven:frontend.program.parse.impl;

import :compilation.request;
import :diagnostics.builder;
import :diagnostics.sink;
import :frontend.lex;
import :frontend.parse;
import :frontend.program;
import :frontend.program.parse;
import :frontend.program.verify;
import :source.provenance;
import :support.id_table;
import :support.invariant;
import std;

class SyntaxProgramBuilder final {
public:
    explicit SyntaxProgramBuilder(CompilationProvenance provenance_value) noexcept
        : provenance(std::move(provenance_value)) {}

    SyntaxProgramBuilder(const SyntaxProgramBuilder&) = delete;
    SyntaxProgramBuilder(SyntaxProgramBuilder&&) = default;
    ~SyntaxProgramBuilder() = default;

    auto operator=(const SyntaxProgramBuilder&) -> SyntaxProgramBuilder& = delete;
    auto operator=(SyntaxProgramBuilder&&) -> SyntaxProgramBuilder& = default;

    auto define_module_syntax(ProgramModuleID module_id, SyntaxTree syntax_tree) noexcept -> void {
        const auto defined_module_id = syntax_by_module.add(std::move(syntax_tree));
        if (defined_module_id != module_id) {
            invariant_violation("syntax modules must be defined in provenance module order");
        }
    }

    auto finish() && noexcept -> SyntaxProgram {
        auto program =
            SyntaxProgram(SyntaxProgramParts(std::move(provenance), std::move(syntax_by_module)));
        const auto verification = verify_syntax_program(program);
        if (!verification.has_value()) {
            invariant_violation(verification.error().message);
        }
        return program;
    }

private:
    CompilationProvenance provenance;
    IDTable<SyntaxTree, ProgramModuleID> syntax_by_module;
};

namespace {

auto compilation_input_error(std::string message) noexcept -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::CompilationInput, std::move(message)).build();
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

    auto construction = SyntaxProgramBuilder(std::move(provenance_construction).finish());
    for (auto index = 0uz; index < syntax_trees.size(); ++index) {
        construction.define_module_syntax(
            program_module_ids[index],
            std::move(syntax_trees[index])
        );
    }
    return std::move(construction).finish();
}
