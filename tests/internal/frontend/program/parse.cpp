module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.program.parse;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :frontend.program.verify;
import :source.manager;
import :source.module_path;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace

TEST_CASE("Syntax program: real module sources publish through the program parser") {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("main.cv", "const answer: i32 = 42;\n");
    REQUIRE(source.has_value());

    const auto inputs = std::array {CompilationModuleInput {
        .source_id = *source,
        .module_path = path("app.main"),
    }};
    auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(parsed.has_value());
    REQUIRE(verify_syntax_program(*parsed).has_value());
    CHECK_EQ(parsed->syntax_trees().size(), 1u);
    CHECK_EQ(parsed->provenance().module_records().size(), 1u);
    CHECK_EQ(parsed->provenance().source_snapshots().size(), 1u);
    CHECK_EQ(
        parsed->provenance().module_record(parsed->provenance().module_id_at(0)).path.value(),
        "app.main"
    );
}

TEST_CASE("Syntax program: closed compilation rejects an empty input batch") {
    const auto sources = SourceManager();
    const auto parsed = parse_program(
        sources,
        CompilationRequest {.modules = std::span<const CompilationModuleInput>()}
    );
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1u);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
    CHECK_EQ(parsed.error().front().finding.severity, DiagnosticSeverity::Error);
    CHECK_FALSE(parsed.error().front().attachment.primary.has_value());
}

TEST_CASE("Syntax program: closed compilation rejects duplicate source snapshots") {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("main.cv", "fn main() {}\n");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path("app.first"),
        },
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path("app.second"),
        },
    };
    const auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1u);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
}

TEST_CASE("Syntax program: closed compilation rejects duplicate module paths") {
    auto sources = SourceManager();
    const auto first = sources.append_virtual("first.cv", "fn first() {}\n");
    const auto second = sources.append_virtual("second.cv", "fn second() {}\n");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *first,
            .module_path = path("app.same"),
        },
        CompilationModuleInput {
            .source_id = *second,
            .module_path = path("app.same"),
        },
    };
    const auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1u);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
}

TEST_CASE("Syntax program: closed compilation rejects a missing source snapshot") {
    const auto sources = SourceManager();
    const auto inputs = std::array {CompilationModuleInput {
        .source_id = SourceID::from_index(0),
        .module_path = path("app.missing"),
    }};
    const auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1u);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
}

TEST_CASE("Syntax program: imports select official, craft root, and module directory sources") {
    struct Case final {
        std::string_view importer;
        std::string_view craft_root_target;
        std::string_view relative_target;
    };

    const auto cases = std::array {
        Case {"nested.main", "std.utf", "nested.std.utf"},
        Case {"crafts.app.nested.main", "crafts.app.std.utf", "crafts.app.nested.std.utf"},
        Case {
            "crafts.carven.nested.main",
            "crafts.carven.std.utf",
            "crafts.carven.nested.std.utf",
        },
    };
    for (const auto& item : cases) {
        CAPTURE(item.importer);
        auto sources = SourceManager();
        auto inputs = std::vector<CompilationModuleInput>();
        const auto append = [&](std::string_view name, std::string_view text) noexcept {
            const auto source = sources.append_virtual(std::string(name), std::string(text));
            REQUIRE(source.has_value());
            inputs.push_back({.source_id = *source, .module_path = path(name)});
        };
        append(
            item.importer,
            "import std::utf using *; import std.utf using *; "
            "import .std.utf using *; import json::parser using *;"
        );
        append("crafts.carven.std.utf", "");
        append("crafts.std.utf", "");
        append("crafts.json.parser", "");
        if (item.craft_root_target != "crafts.carven.std.utf") {
            append(item.craft_root_target, "");
        }
        append(item.relative_target, "");

        const auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
        REQUIRE(parsed.has_value());
        REQUIRE(verify_syntax_program(*parsed).has_value());
        const auto provenance = parsed->provenance();
        const auto importer = provenance.find_program_module(path(item.importer));
        REQUIRE(importer.has_value());
        const auto imports = parsed->resolved_imports(*importer);
        const auto expected = std::array {
            std::string_view("crafts.carven.std.utf"),
            item.craft_root_target,
            item.relative_target,
            std::string_view("crafts.json.parser"),
        };
        REQUIRE_EQ(imports.size(), expected.size());
        for (auto index = 0uz; index < expected.size(); ++index) {
            CHECK_EQ(provenance.module_record(imports[index].target).path.value(), expected[index]);
        }
    }
}

TEST_CASE("Syntax program: a standard import requires the official source in the input batch") {
    auto sources = SourceManager();
    auto inputs = std::vector<CompilationModuleInput>();
    for (const auto name : {"nested.main", "crafts.std.utf", "std.utf", "nested.std.utf"}) {
        const auto source = sources.append_virtual(
            name,
            std::string_view(name) == "nested.main" ? "import std::utf using *;" : ""
        );
        REQUIRE(source.has_value());
        inputs.push_back({.source_id = *source, .module_path = path(name)});
    }
    const auto parsed = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1uz);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::ImportResolution);
    REQUIRE(parsed.error().front().attachment.primary.has_value());
}
