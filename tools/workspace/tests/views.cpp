module carven:test.workspace.views;

import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.folding;
import std;

namespace {

const TestSuite tests([] static noexcept {
    "Workspace: document highlights share navigation and preserve alias and shadow identities"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project = std::array {
                WorkspaceProjectModule {"app.cv", *CanonicalModulePath::from_value("pkg.app")},
                WorkspaceProjectModule {"lib.cv", *CanonicalModulePath::from_value("pkg.lib")}
            };
            require(host.update("lib.cv", 1, "fn provide() -> i32 => 1;").has_value());
            auto version = 0ll;
            for (const auto error :
                 {"",
                  "let broken = unknown(); ",
                  "let broken = ; ",
                  "let broken = \"unfinished\n"}) {
                const auto text =
                    std::string(
                        "import .lib using { provide as compute, provide as again } fn sample() { "
                    )
                    + error
                    + "let chosen = compute(); if true { let chosen = again(); println(chosen); } println(chosen); compute(";
                require(host.update("app.cv", ++version, text).has_value());
                const auto snapshot = host.snapshot();
                const auto at = static_cast<std::uint32_t>(text.rfind("compute"));
                const auto query = snapshot.document_highlights(project, "app.cv", at);

                require_equal(query.result.size(), 3uz);
                for (const auto range : query.result) {
                    expect_equal(slice(text, range), std::string_view("compute"));
                }
                const auto local = snapshot.document_highlights(
                    project,
                    "app.cv",
                    static_cast<std::uint32_t>(text.rfind("chosen"))
                );
                require_equal(local.result.size(), 2uz);
                expect_equal(
                    local.result.front().start(),
                    static_cast<std::uint32_t>(text.find("chosen"))
                );
                expect(
                    snapshot
                        .document_highlights(
                            project,
                            "app.cv",
                            static_cast<std::uint32_t>(text.find("sample"))
                        )
                        .result.size()
                    == 1uz
                );
            }
        };

    "Workspace: folds retain syntax ranges and share unchanged content"_test = [] static noexcept {
        auto host = WorkspaceAnalysisHost(16uz);
        const auto text = std::string(
            "// heading\r\n// detail\r\nimport .lib using first;\r\nimport .lib using second;\r\n"
            "struct Point {\r\n    width: i32\r\n}\r\n"
            "fn sample() {\r\n    let broken = \"unfinished\r\n    let values = [\r\n        1,\r\n        2\r\n    ];\r\n    if true {\r\n        println(values);\r\n    }\r\n"
        );
        require(host.update("app.cv", 1, text).has_value());
        const auto snapshot = host.snapshot();
        const auto query = snapshot.folding_ranges("app.cv");
        require(query.has_value());
        const auto& folds = *query->result;
        const auto count = [&](WorkspaceFoldKind kind) noexcept {
            return std::ranges::count(folds, kind, &WorkspaceFold::kind);
        };
        expect_equal(count(WorkspaceFoldKind::Comment), 1);
        expect_equal(count(WorkspaceFoldKind::Imports), 1);
        expect_equal(count(WorkspaceFoldKind::Code), 4);
        const auto array_start = static_cast<std::uint32_t>(text.find('[') + 1);
        const auto array_end = static_cast<std::uint32_t>(text.find(']'));
        expect(std::ranges::any_of(folds, [&](const auto& fold) noexcept {
            return fold.range == Span::from_bounds(array_start, array_end);
        }));
        require(host.update("app.cv", 2, text).has_value());
        const auto next = host.snapshot().folding_ranges("app.cv");
        require(next.has_value());
        expect_equal(next->document.version, 2ll);

        require(host.update("app.cv", 3, "fn other() {\n    println(1);\n}\n").has_value());

        expect_equal(query->document.version, 1ll);
    };
});

} // namespace
