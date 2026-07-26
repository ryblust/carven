module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.program.parse;

import :compilation.request;
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

    const auto inputs = std::array {CompilationInput {
        .source_id = *source,
        .module_path = path("app.main"),
    }};
    auto parsed = parse(sources, inputs);
    REQUIRE(parsed.has_value());
    REQUIRE(verify_syntax_program(*parsed).has_value());
    CHECK_EQ(parsed->syntax_trees().size(), 1u);
    CHECK_EQ(parsed->provenance().module_records().size(), 1u);
    CHECK_EQ(parsed->provenance().source_snapshots().size(), 1u);
    CHECK_EQ(
        parsed->provenance().module_record(ProgramModuleID::from_index(0)).path.value(),
        "app.main"
    );
}

TEST_CASE("Syntax program: closed compilation rejects an empty input batch") {
    const auto sources = SourceManager();
    const auto parsed = parse(sources, std::span<const CompilationInput>());
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
        CompilationInput {
            .source_id = *source,
            .module_path = path("app.first"),
        },
        CompilationInput {
            .source_id = *source,
            .module_path = path("app.second"),
        },
    };
    const auto parsed = parse(sources, inputs);
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
        CompilationInput {
            .source_id = *first,
            .module_path = path("app.same"),
        },
        CompilationInput {
            .source_id = *second,
            .module_path = path("app.same"),
        },
    };
    const auto parsed = parse(sources, inputs);
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1u);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
}

TEST_CASE("Syntax program: closed compilation rejects a missing source snapshot") {
    const auto sources = SourceManager();
    const auto inputs = std::array {CompilationInput {
        .source_id = SourceID::from_index(0),
        .module_path = path("app.missing"),
    }};
    const auto parsed = parse(sources, inputs);
    REQUIRE_FALSE(parsed.has_value());
    REQUIRE_EQ(parsed.error().size(), 1u);
    CHECK_EQ(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
}
