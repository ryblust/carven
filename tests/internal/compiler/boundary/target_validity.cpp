module carven:test.internal.compiler.boundary.target_validity;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.report;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler integration: C++ target validity remains downstream-owned",
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual(
                "native.cv",
                "#[cpp] ---\n"
                "auto invalid = object.virtual;\n"
                "---\n"
                "fn value() {}\n"
            );
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("native"),
            };

            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = LinkageDomain::explicit_value("test:target-validity").value(),
                }
            );

            if (!ct::expect(result.has_value()).note([&] noexcept {
                    return render_diagnostics(result.error(), sources);
                })) {
                return;
            }
            if (!ct::expect_equal(result->value.entries().size(), 2u)) {
                return;
            }
            ct::expect(
                result->value.entries()[1].content.contains("auto invalid = object.virtual;")
            );
        }
    );
});

} // namespace
