module carven:test.workspace.edits;

import :diagnostics.code;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {

const TestSuite tests([] static noexcept {
    "Workspace: formatting reuses syntax and preserves source content"_test = [] static noexcept {
        auto host = WorkspaceAnalysisHost(16uz);
        auto text = std::string("// 😀 comment\r\nfn sample( )->i32{return  1;}\r\n");
        require(host.update("app.cv", 1, text).has_value());
        const auto snapshot = host.snapshot();
        const auto parsed = snapshot.syntax("app.cv");
        const auto formatted = snapshot.formatting("app.cv");
        require(formatted.has_value());
        expect_equal(formatted->document.result, parsed->result);
        require(formatted->result.has_value() && formatted->result->has_value());
        const auto& edit = **formatted->result;
        expect(edit.range.start() == std::string_view("// 😀 comment").size());
        text.replace(edit.range.start(), edit.range.size(), edit.replacement);
        expect_equal(text, "// 😀 comment\nfn sample() -> i32 {\n    return 1;\n}\n");
        require(host.update("app.cv", 2, text).has_value());
        const auto unchanged = host.snapshot().formatting("app.cv");
        require(unchanged->result.has_value());
        expect(!unchanged->result->has_value());
        require(host.update("app.cv", 3, "fn sample( {").has_value());
        expect(!host.snapshot().formatting("app.cv")->result.has_value());
    };

    "Workspace: compiler fixes repair delimiters and imported spellings"_test = [] static noexcept {
        auto host = WorkspaceAnalysisHost(16uz);
        const auto project = std::array {
            WorkspaceProjectModule {"lib.cv", *CanonicalModulePath::from_value("pkg.lib")},
            WorkspaceProjectModule {"app.cv", *CanonicalModulePath::from_value("pkg.app")}
        };
        require(host.update("lib.cv", 1, "fn combine() -> i32 => 1; struct Widget { value: i32 }")
                    .has_value());
        auto version = 0ll;
        for (const auto broken :
             {"fn sample() -> i32 { return 1\n}",
              "fn sample() { let _ = (1; }",
              "fn sample() { let _ = [1, 2; }",
              "import .lib using combine; fn sample() -> i32 => comibne();",
              "import .lib using Widget; fn sample(value: Widgte) -> i32 => value.value;"}) {
            auto text = std::string(broken);
            require(host.update("app.cv", ++version, text).has_value());
            const auto snapshot = host.snapshot();
            const auto fixes = snapshot.code_actions(
                project,
                "app.cv",
                Span::from_bounds(0, static_cast<std::uint32_t>(text.size()))
            );
            expect_equal(fixes.analysis.result, snapshot.semantic(project).result);
            require_equal(fixes.result.size(), 1uz).note(text);
            for (const auto& edit : fixes.result.front().edits | std::views::reverse) {
                expect_equal(edit.document, "app.cv");
                text.replace(edit.range.start(), edit.range.size(), edit.replacement);
            }
            require(host.update("app.cv", ++version, text).has_value());
            const auto repaired = host.snapshot().semantic(project);
            expect(
                std::ranges::none_of(
                    repaired.result->diagnostics(),
                    [](const auto& diagnostic) static noexcept {
                        return diagnostic.finding.severity == DiagnosticSeverity::Error;
                    }
                )
            ).note(text);
        }
    };
});

}
