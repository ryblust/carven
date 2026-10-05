module carven:test.internal.semantic.semir.fixture;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto semir_test_module_path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    require(result.has_value());
    return std::move(*result);
}

} // namespace

auto begin_semir_test_compilation_batch(
    SourceManager& sources,
    DiagnosticSink& diagnostics,
    std::span<const std::string_view> module_names
) noexcept -> ProgramDraft {
    auto inputs = std::vector<SourceModuleInput>();
    inputs.reserve(module_names.size());
    for (auto index = 0uz; index < module_names.size(); ++index) {
        const auto source =
            sources.append_virtual(std::format("semir-publication-{}.cv", index), "");
        require(source.has_value());
        inputs.push_back(
            SourceModuleInput {
                .source_id = *source,
                .module_path = semir_test_module_path(module_names[index]),
            }
        );
    }
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    require(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

auto begin_semir_test_compilation(
    SourceManager& sources,
    DiagnosticSink& diagnostics,
    std::string_view module_name
) noexcept -> ProgramDraft {
    const auto module_names = std::array {module_name};
    return begin_semir_test_compilation_batch(sources, diagnostics, module_names);
}

struct SemIRTestModuleOrigin final {
    ProgramModuleID provenance_module;
    ProgramOriginID origin;
};

auto make_semir_test_module_origin(ProgramDraft& builder, std::size_t index = 0uz) noexcept
    -> SemIRTestModuleOrigin {
    const auto module_id = builder.provenance_module_at(index);
    return {
        .provenance_module = module_id,
        .origin = builder.append_source_origin(builder.module_source(module_id), Span::at(0u)),
    };
}

auto make_semir_test_callable_contract(ProgramDraft& builder, TypeID result) noexcept
    -> ConstructionCallableContract {
    return {
        .parameters = {},
        .result = result,
        .failures = builder.add_empty_failure_term(),
        .policy = FailureContractPolicy::Declared,
    };
}

auto make_semir_test_body(
    BodyReservation reservation,
    ProgramOriginID origin,
    ProgramDraft& program
) noexcept -> StructuredBodyDraft {
    auto body = BodyBuilder(std::move(reservation), program);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
    return std::move(body).finish(
        SemanticRegion {
            .lifetime = lifetime,
            .origin = origin,
            .statements = {},
            .result = std::nullopt,
            .result_reachable = false,
            .failures = BodyFailures(program.add_empty_failure_term()),
            .exits_test = false,
        }
    );
}
