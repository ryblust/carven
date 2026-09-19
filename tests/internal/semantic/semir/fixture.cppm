module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.fixture;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace semir_test {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto begin_compilation_batch(
    SourceManager& sources,
    DiagnosticSink& diagnostics,
    std::span<const std::string_view> module_names
) noexcept -> ProgramDraft {
    auto inputs = std::vector<SourceModuleInput>();
    inputs.reserve(module_names.size());
    for (auto index = 0uz; index < module_names.size(); ++index) {
        const auto source =
            sources.append_virtual(std::format("semir-publication-{}.cv", index), "");
        REQUIRE(source.has_value());
        inputs.push_back(
            SourceModuleInput {
                .source_id = *source,
                .module_path = path(module_names[index]),
            }
        );
    }
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    REQUIRE(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

auto begin_compilation(
    SourceManager& sources,
    DiagnosticSink& diagnostics,
    std::string_view module_name
) noexcept -> ProgramDraft {
    const auto module_names = std::array {module_name};
    return begin_compilation_batch(sources, diagnostics, module_names);
}

struct ModuleFacts final {
    ProgramModuleID provenance_module;
    ProgramOriginID origin;
};

auto module_facts(ProgramDraft& builder, std::size_t index = 0uz) noexcept -> ModuleFacts {
    const auto module_id = builder.provenance_module_at(index);
    return {
        .provenance_module = module_id,
        .origin = builder.append_source_origin(builder.module_source(module_id), Span::at(0u)),
    };
}

auto callable_contract(ProgramDraft& builder, TypeID result) noexcept
    -> ConstructionCallableContract {
    return {
        .parameters = {},
        .result = result,
        .failures = builder.add_empty_failure_term(),
        .policy = FailureContractPolicy::Declared,
    };
}

auto minimal_body(
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
            .failures = BodyFailures(program.add_empty_failure_term()),
            .exits_test = false,
        }
    );
}

auto body_with_closure(
    BodyReservation reservation,
    ProgramOriginID origin,
    TypeID closure_type,
    CallableID closure_callable,
    ProgramDraft& program
) noexcept -> StructuredBodyDraft {
    auto body = BodyBuilder(std::move(reservation), program);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
    auto statements = std::vector<SemanticStatement>();
    statements.push_back(
        SemanticStatement {
            .origin = origin,
            .lifetime = lifetime,
            .value = SemExpressionStatement {
                .expression = body.make_expression(
                    closure_type,
                    lifetime,
                    origin,
                    SemClosure {.callable = closure_callable, .captures = {}}
                ),
            },
        }
    );
    return std::move(body).finish(
        SemanticRegion {
            .lifetime = lifetime,
            .origin = origin,
            .statements = std::move(statements),
            .result = std::nullopt,
            .failures = BodyFailures(program.add_empty_failure_term()),
            .exits_test = false,
        }
    );
}

auto publish(std::vector<StructuredBodyDraft> bodies, ProgramDraft& builder) noexcept -> void {
    for (auto& body : bodies) {
        builder.add_body_draft(std::move(body));
    }
}

auto publish(StructuredBodyDraft body, ProgramDraft& builder) noexcept -> void {
    auto bodies = std::vector<StructuredBodyDraft>();
    bodies.push_back(std::move(body));
    publish(std::move(bodies), builder);
}

} // namespace semir_test
