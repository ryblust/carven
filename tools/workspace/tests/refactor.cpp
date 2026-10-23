module carven:test.workspace.refactor;

import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import std;

namespace {

auto module(std::string document, std::string_view name) -> WorkspaceProjectModule {
    return {std::move(document), *CanonicalModulePath::from_value(name)};
}

auto update(WorkspaceAnalysisHost& host, std::int64_t version, std::string text) -> void {
    require(host.update("app.cv", version, std::move(text)).has_value());
}

const TestSuite tests([] static noexcept {
    "Workspace: rename follows declaration and alias identities through unfinished calls"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project = std::array {
                module("app.cv", "pkg.app"),
                module("lib.cv", "pkg.lib"),
                module("consumer.cv", "pkg.consumer")
            };
            const auto library = std::string("fn combine(left: i32, right: i64) -> i64 => right;");
            require(host.update("lib.cv", 1, library).has_value());
            require(host.update(
                            "consumer.cv",
                            1,
                            "import .lib using combine; fn use() -> i64 => combine(1, 2);"
            )
                        .has_value());
            const auto text = std::string(
                "import .lib using combine as compute; fn sample() { let broken = ; let value = compute(1, 2); compute("
            );
            update(host, 1, text);
            const auto snapshot = host.snapshot();
            const auto alias = snapshot.rename(
                project,
                "app.cv",
                static_cast<std::uint32_t>(text.rfind("compute")),
                "calculate"
            );
            require(alias && alias->result);
            expect_equal(alias->result->name, "compute");
            expect_equal(alias->result->references.size(), 3uz);
            for (const auto& location : alias->result->references) {
                expect_equal(location.document, "app.cv");
            }
            const auto declaration = snapshot.rename(
                project,
                "lib.cv",
                static_cast<std::uint32_t>(library.find("combine")),
                "merge"
            );
            require(declaration && declaration->result);
            expect_equal(declaration->result->references.size(), 4uz);
            expect(
                std::ranges::any_of(declaration->result->references, [](const auto& ref) noexcept {
                    return ref.document == "consumer.cv";
                })
            );
            const auto invalid = snapshot.rename(
                project,
                "app.cv",
                static_cast<std::uint32_t>(text.rfind("compute")),
                "fn"
            );
            expect(!invalid && invalid.error() == WorkspaceRenameError::InvalidName);
            const auto capture = std::string(
                "fn sample() { let first = 1; let second = 2; println(first + second); }"
            );
            update(host, 2, capture);
            const auto conflict = host.snapshot().rename(
                project,
                "app.cv",
                static_cast<std::uint32_t>(capture.find("first")),
                "second"
            );
            expect(!conflict && conflict.error() == WorkspaceRenameError::Conflict);
        };

    "Workspace: signature help selects nested arguments and imported callables in incomplete code"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project =
                std::array {module("app.cv", "pkg.app"), module("lib.cv", "pkg.lib")};
            require(
                host.update(
                        "lib.cv",
                        1,
                        "fn combine(left: i32, right: i64) -> i64 => right; class Counter { value: i32, fn new(value: i32) -> Counter => { value: value }; fn add(self, amount: i32) -> i32 => self.value + amount; }"
                )
                    .has_value()
            );
            struct Case {
                std::string_view prefix;
                std::string_view label;
                std::uint32_t parameter;
            };
            const auto cases = std::array {
                Case {"compute(", "compute(left: i32, right: i64) -> i64", 0},
                Case {"compute(1, ", "compute(left: i32, right: i64) -> i64", 1},
                Case {"compute(1, compute(2, ", "compute(left: i32, right: i64) -> i64", 1},
                Case {"compute(1, compute(2, 3), ", "compute(left: i32, right: i64) -> i64", 1},
                Case {"compute(\"a,b\", ", "compute(left: i32, right: i64) -> i64", 1},
                Case {"let callback = compute; callback(", "callback(i32, i64) -> i64", 0},
                Case {"let counter = Counter::new(1); counter.add(", "add(amount: i32) -> i32", 0}
            };
            auto version = 0;
            for (const auto& test : cases) {
                const auto text =
                    std::string(
                        "import .lib using { combine as compute, Counter } fn sample() { let broken = missing(); "
                    )
                    + std::string(test.prefix);
                update(host, ++version, text);
                const auto help = host.snapshot().signature_help(
                    project,
                    "app.cv",
                    static_cast<std::uint32_t>(text.size())
                );
                if (!expect(help.result.has_value()).note(text)) {
                    continue;
                }
                expect_equal(help.result->label, test.label).note(text);
                expect_equal(help.result->active_parameter, std::optional(test.parameter));
            }
        };
});

} // namespace
