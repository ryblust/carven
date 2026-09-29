module carven:test.internal.semantic.evaluation.fixture;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.semir.constant;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

} // namespace

auto constant_test_module_path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    ct::require(result.has_value());
    return std::move(*result);
}

auto begin_constant_test_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("constant-evaluate.cv", "");
    ct::require(source.has_value());
    const auto inputs = std::array {
        SourceModuleInput {
            .source_id = *source,
            .module_path = constant_test_module_path("constant.evaluate"),
        },
    };
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    ct::require(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

struct ConstantEvaluationFixture final {
    SourceManager sources;
    DiagnosticSink diagnostics;
    ProgramDraft compilation;

    ConstantEvaluationFixture() noexcept
        : sources(),
          diagnostics(),
          compilation(begin_constant_test_compilation(sources, diagnostics)) {}
};

auto constant_test_integer_fact(TypeID type, std::int64_t value) noexcept -> ConstantFact {
    return ConstantFact {.type = type, .value = IntegerConstant::from_signed(value)};
}
