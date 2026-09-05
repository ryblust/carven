module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constants;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace {

struct ModuleFixture final {
    std::string_view origin;
    std::string_view path;
    std::string_view source;
};

struct VisibilityExpectation final {
    std::string_view name;
    std::string_view provider_path;
    std::string_view provider_source;
    std::string_view consumer_path;
    std::string_view consumer_source;
    bool succeeds;
};

auto compile_fixture(SourceManager& sources, std::span<const ModuleFixture> modules) noexcept
    -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics> {
    auto inputs = std::vector<CompilationModuleInput>();
    inputs.reserve(modules.size());
    for (const auto& source_module : modules) {
        const auto source_id = sources.append_virtual(
            std::string(source_module.origin),
            std::string(source_module.source)
        );
        const auto module_path = CanonicalModulePath::from_value(source_module.path);
        REQUIRE(source_id.has_value());
        REQUIRE(module_path.has_value());
        inputs.push_back({
            .source_id = *source_id,
            .module_path = *module_path,
        });
    }
    return compile(
        sources,
        CompilationRequest {.modules = inputs},
        TargetPlanningRequest {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = LinkageDomain::explicit_value("test:constants").value(),
        }
    );
}

auto find_diagnostic(std::span<const Diagnostic> diagnostics, std::string_view code) noexcept
    -> const Diagnostic* {
    const auto found =
        std::ranges::find_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        });
    return found == diagnostics.end() ? nullptr : std::addressof(*found);
}

auto diagnostic_count(std::span<const Diagnostic> diagnostics, std::string_view code) noexcept
    -> std::size_t {
    return static_cast<std::size_t>(
        std::ranges::count_if(diagnostics, [&](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        })
    );
}

} // namespace

TEST_CASE("Top-level constants: visibility follows declaration audiences") {
    static constexpr auto cases = std::to_array<VisibilityExpectation>({
        {
            .name = "private constant from a sibling module",
            .provider_path = "api",
            .provider_source = "private const shared = 42;\n",
            .consumer_path = "user",
            .consumer_source = "import api using shared;\n"
                               "fn read() -> i32 { return shared; }\n",
            .succeeds = false,
        },
        {
            .name = "bare constant within one module domain",
            .provider_path = "crafts.alpha.api",
            .provider_source = "const shared = 42;\n",
            .consumer_path = "crafts.alpha.user",
            .consumer_source = "import api using shared;\n"
                               "export fn read() -> i32 { return shared; }\n",
            .succeeds = true,
        },
        {
            .name = "bare constant across module domains",
            .provider_path = "crafts.alpha.api",
            .provider_source = "const shared = 42;\n",
            .consumer_path = "crafts.beta.user",
            .consumer_source = "import alpha::api using shared;\n"
                               "export fn read() -> i32 { return shared; }\n",
            .succeeds = false,
        },
        {
            .name = "exported constant across module domains",
            .provider_path = "crafts.alpha.api",
            .provider_source = "export const shared: i32 = 42;\n",
            .consumer_path = "crafts.beta.user",
            .consumer_source = "import alpha::api using shared;\n"
                               "export fn read() -> i32 { return shared; }\n",
            .succeeds = true,
        },
    });

    for (const auto& expectation : cases) {
        CAPTURE(expectation.name);
        auto sources = SourceManager();
        const auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "provider.cv",
                .path = expectation.provider_path,
                .source = expectation.provider_source,
            },
            {
                .origin = "consumer.cv",
                .path = expectation.consumer_path,
                .source = expectation.consumer_source,
            },
        });

        const auto result = compile_fixture(sources, modules);
        if (expectation.succeeds) {
            CHECK(result.has_value());
            continue;
        }
        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-IMPORT-RESOLUTION");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "shared");
    }
}

TEST_CASE("Top-level constants: cycles retain one stable diagnostic") {
    SUBCASE("direct cycle") {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "direct.cv",
                .path = "direct",
                .source = "const direct =\n"
                          "    direct;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        REQUIRE_FALSE(result.has_value());
        REQUIRE_EQ(diagnostic_count(result.error(), "CV-CONST-CYCLE"), 1uz);
        const auto* diagnostic = find_diagnostic(result.error(), "CV-CONST-CYCLE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "direct");
    }

    SUBCASE("cross-module cycle") {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "a.cv",
                .path = "a",
                .source = "import b using second;\n"
                          "const first = second;\n",
            },
            {
                .origin = "b.cv",
                .path = "b",
                .source = "import a using first;\n"
                          "const second = first;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        REQUIRE_FALSE(result.has_value());
        REQUIRE_EQ(diagnostic_count(result.error(), "CV-CONST-CYCLE"), 1uz);
        const auto* diagnostic = find_diagnostic(result.error(), "CV-CONST-CYCLE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        REQUIRE_EQ(diagnostic->attachment.related.size(), 2uz);
    }

    SUBCASE("numeric enum case cycle") {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "enum-cycle.cv",
                .path = "enum_cycle",
                .source = "enum Code: i32 {\n"
                          "    First = Code::Second as i32,\n"
                          "    Second = Code::First as i32,\n"
                          "}\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        REQUIRE_FALSE(result.has_value());
        CHECK_EQ(diagnostic_count(result.error(), "CV-CONST-CYCLE"), 1uz);
    }
}

TEST_CASE("Top-level constants: enum owners resolve before case lookup") {
    auto sources = SourceManager();
    static constexpr auto modules = std::to_array<ModuleFixture>({
        {
            .origin = "enum-owner.cv",
            .path = "enum_owner",
            .source = "const invalid = Empty::Missing;\n"
                      "enum Empty {}\n",
        },
    });

    const auto result = compile_fixture(sources, modules);

    REQUIRE_FALSE(result.has_value());
    CHECK_EQ(diagnostic_count(result.error(), "CV-TYPE-ENUM-EMPTY"), 1uz);
    CHECK_EQ(diagnostic_count(result.error(), "CV-TYPE-MEMBER-UNRESOLVED"), 0uz);
}

TEST_CASE("Top-level constants: lexical facts stay outside declaration elaboration") {
    auto sources = SourceManager();
    static constexpr auto modules = std::to_array<ModuleFixture>({
        {
            .origin = "lambda-initializer.cv",
            .path = "lambda_initializer",
            .source = "const invalid = [](value: i32) { return value; };\n",
        },
    });

    const auto result = compile_fixture(sources, modules);

    REQUIRE_FALSE(result.has_value());
    REQUIRE_EQ(result.error().size(), 1uz);
    CHECK_EQ(diagnostic_count(result.error(), "CV-CONST-INITIALIZER"), 1uz);
}

TEST_CASE("Top-level constants: a nullary enum case remains a value") {
    static constexpr auto cases = std::to_array<std::string_view>({
        "enum Choice { Empty }\n"
        "const invalid = Choice::Empty();\n",
        "enum Choice { Empty, Value(i32) }\n"
        "const invalid = Choice::Empty();\n",
    });

    for (const auto source : cases) {
        CAPTURE(source);
        auto sources = SourceManager();
        const auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "nullary-call.cv",
                .path = "nullary_call",
                .source = source,
            },
        });
        const auto result = compile_fixture(sources, modules);

        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-TYPE-ENUM-CASE-ARITY");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "Choice::Empty()");
    }
}

TEST_CASE("Top-level constants: published values close their declared surface") {
    SUBCASE("exported constant requires a declaration type") {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "missing-type.cv",
                .path = "missing_type",
                .source = "export const answer = 42;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-CONST-EXPORTED-TYPE");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
        CHECK_EQ(sources.slice(diagnostic->attachment.primary->span), "answer");
    }

    SUBCASE("exported constant cannot expose a private nominal identity") {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "nominal-leak.cv",
                .path = "nominal_leak",
                .source = "private enum Hidden { Value }\n"
                          "export const exposed: Hidden = Hidden::Value;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        REQUIRE_FALSE(result.has_value());
        const auto* diagnostic = find_diagnostic(result.error(), "CV-TYPE-VISIBILITY-LEAK");
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->attachment.primary.has_value());
    }

    SUBCASE("normalization removes an implementation-only enum identity") {
        auto sources = SourceManager();
        static constexpr auto modules = std::to_array<ModuleFixture>({
            {
                .origin = "normalized-surface.cv",
                .path = "normalized_surface",
                .source = "private enum Hidden: i32 { Value = 7 }\n"
                          "export const exposed: i32 = Hidden::Value as i32;\n",
            },
        });

        const auto result = compile_fixture(sources, modules);

        CHECK(result.has_value());
    }
}
