module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.utf;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnosed;
import :source.manager;
import :source.module_path;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

class UTFCompilation final {
public:
    explicit UTFCompilation(std::string_view application) noexcept {
        for (const auto name : {"validation", "text"}) {
            const auto filename = std::format("crafts/carven/std/utf/{}.cv", name);
            auto input = std::ifstream(filename);
            REQUIRE(input.is_open());
            auto text = std::string(std::istreambuf_iterator<char>(input), {});
            const auto source = sources.append_virtual(filename, std::move(text));
            REQUIRE(source.has_value());
            inputs.push_back(
                {.source_id = *source,
                 .module_path = *CanonicalModulePath::from_value(
                     std::format("crafts.carven.std.utf.{}", name)
                 )}
            );
        }
        const auto application_source =
            sources.append_virtual("application.cv", std::string(application));
        REQUIRE(application_source.has_value());
        inputs.push_back(
            {.source_id = *application_source,
             .module_path = *CanonicalModulePath::from_value("application")}
        );
    }

    auto run() noexcept {
        return compile(
            sources,
            CompilationRequest {.modules = inputs},
            TargetPlanningRequest {
                .test_mode = TestGenerationMode::None,
                .linkage_domain = *LinkageDomain::explicit_value("test:utf"),
            }
        );
    }

    SourceManager sources;
    std::vector<CompilationModuleInput> inputs;
};

} // namespace

TEST_CASE("UTF craft: source arguments retain Carven type and storage checks") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "named input slice retains its backing",
         .source =
             "fn bad() throw UTF8Error { var a: [u8; 1] = [65]; let bytes = a.as_slice(); let text = from_utf8(bytes)?; a[0] = 66; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "a[0] = 66"},
        {.name = "input slice cannot outlive a temporary",
         .source =
             "fn bad() throw UTF8Error { let bytes = [65 as u8].as_slice(); let text = from_utf8(bytes)?; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "let bytes = [65 as u8].as_slice()"},
    });
    for (const auto& item : cases) {
        CAPTURE(item.name);
        auto fixture = UTFCompilation(
            std::string(
                "import std::utf.text using from_utf8; import std::utf.validation using UTF8Error; "
            )
            + std::string(item.source)
        );
        const auto result = fixture.run();
        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_compiler_diagnostic(result.error(), item.code);
        for (const auto& reported : result.error()) {
            INFO(reported.finding.message);
        }
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(fixture.sources.slice(diagnostic->attachment.primary->span), item.primary_text);
    }
}
