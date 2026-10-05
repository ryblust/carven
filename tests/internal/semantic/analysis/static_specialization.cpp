module carven:test.internal.semantic.analysis.static_specialization;

import :compiler.analysis;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

template<typename Check>
auto with_specialization(std::string_view source, Check check) noexcept -> void {
    auto sources = SourceManager();
    const auto source_id = sources.append_virtual("specialization.cv", std::string(source));
    require(source_id.has_value());
    const auto module_path = CanonicalModulePath::from_value("specialization");
    require(module_path.has_value());
    const auto input = SourceModuleInput {
        .source_id = *source_id,
        .module_path = *module_path,
    };
    // These limits belong to semantic construction; C++ generation adds no
    // resource-accounting evidence for the expanded bodies and instances.
    check(sources, analyze_compilation(sources, SourceBatch {.modules = std::span(&input, 1)}));
}

auto check_specialization_accepts(std::string_view source) noexcept -> void {
    with_specialization(source, [](const auto&, const auto& result) static noexcept {
        expect(result.has_value());
    });
}

auto check_specialization_limit(std::string_view source, std::string_view primary_text) noexcept
    -> void {
    with_specialization(source, [&](const auto& sources, const auto& result) noexcept {
        if (!expect(!result.has_value())) {
            return;
        }
        const auto* diagnostic = find_diagnostic(result.error(), DiagnosticCode::ConstLimit);
        expect_diagnostic(result.error(), DiagnosticCode::ConstLimit);
        if (diagnostic == nullptr) {
            return;
        }
        expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
        if (!expect(diagnostic->attachment.primary.has_value())) {
            return;
        }
        expect_equal(sources.slice(diagnostic->attachment.primary->span), primary_text);
    });
}

auto instance_source(std::size_t count) noexcept -> std::string {
    return std::format(
        "fn lane(const index: i32) -> i32 => index; "
        "fn use() {{ const for index in 0..{} {{ lane(index); }} }}",
        count
    );
}

auto nesting_source(std::size_t last) noexcept -> std::string {
    return std::format(
        "fn count(const index: i32) -> i32 {{ "
        "const if index == {} {{ return 0; }} return count(index + 1); }} "
        "fn use() -> i32 => count(0);",
        last
    );
}

auto node_source(std::size_t count) noexcept -> std::string {
    auto body = std::string();
    // Each copy contains statements and binding expressions. The rejection input
    // exceeds the production node budget without arithmetic or local storage.
    for (auto index = 0uz; index < 32uz; ++index) {
        body += "value; ";
    }
    return std::format("fn use(value: i32) {{ const for index in 0..{} {{ {} }} }}", count, body);
}

const TestSuite suite([] static noexcept {
    "Static specialization budgets: default iteration boundary"_test = [] static noexcept {
        scenario("100000 iterations are accepted", [] static noexcept {
            check_specialization_accepts("fn use() { const for index in 0..100000 {} }");
        });
        scenario("100001 iterations are rejected at the loop", [] static noexcept {
            check_specialization_limit(
                "fn use() { const for index in 0..100001 {} }",
                "const for index in 0..100001 {}"
            );
        });
    };

    "Static specialization budgets: default instance boundary is independent per root"_test =
        [] static noexcept {
            scenario("two roots each accept 4096 distinct instances", [] static noexcept {
                with_specialization(
                    "fn lane(const index: i32) -> i32 => index; "
                    "fn first() { const for index in 0..4096 { lane(index); } } "
                    "fn second() { const for index in 4096..8192 { lane(index); } }",
                    [](const auto&, const auto& result) static noexcept {
                        if (!expect(result.has_value())) {
                            return;
                        }
                        expect_equal(result->value.static_instances().size(), 8192uz);
                    }
                );
            });
            scenario("one root rejects its 4097th instance at the call", [] static noexcept {
                check_specialization_limit(instance_source(4097uz), "lane(index)");
            });
        };

    "Static specialization budgets: default nesting boundary"_test = [] static noexcept {
        scenario("128 roots including the body root are accepted", [] static noexcept {
            check_specialization_accepts(nesting_source(126uz));
        });
        scenario("the next nested root is rejected at the recursive call", [] static noexcept {
            check_specialization_limit(nesting_source(127uz), "count(index + 1)");
        });
    };

    "Static specialization budgets: default node limit counts copied operations"_test =
        [] static noexcept {
            scenario(
                "copied statements and expressions are accepted below the limit",
                [] static noexcept { check_specialization_accepts(node_source(1000uz)); }
            );
            scenario("expanded operations exceed the default node budget", [] static noexcept {
                check_specialization_limit(node_source(8192uz), "value");
            });
        };
});

} // namespace
