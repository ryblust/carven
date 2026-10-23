module carven:test.workspace.hover;

import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import std;

namespace {

const TestSuite tests([] static noexcept {
    "Workspace: hover shares signatures and compiler type displays through editing errors"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project = std::array {
                WorkspaceProjectModule {"app.cv", *CanonicalModulePath::from_value("pkg.app")},
                WorkspaceProjectModule {"lib.cv", *CanonicalModulePath::from_value("pkg.lib")}
            };
            const auto library = std::string(
                "fn combine(left: i32, right: i64) -> i64 => right; "
                "fn inferred(value: i32) => value as i64; "
                "async fn fetch(value: i32) -> i64 => value as i64; "
                "fn relay<T>(&&value: T) -> T => &&value; const fn constant(value: i32) -> i32 => value; "
                "class Counter { value: i32, fn new(value: i32) -> Counter => { value: value }; fn add(self, amount: i32) -> i32 => self.value + amount; }"
            );
            require(host.update("lib.cv", 1, library).has_value());
            auto version = 0ll;
            for (const auto error :
                 {"",
                  "let broken = unknown(); ",
                  "let broken = ; ",
                  "let broken = \"unfinished\n"}) {
                const auto text =
                    std::string(
                        "import .lib using { combine as compute, inferred, fetch, Counter } fn sample() { "
                    )
                    + error
                    + "let callback = compute; let n = inferred(1); let counter = Counter::new(1); counter.add(1); fetch(1); compute(1, ";
                require(host.update("app.cv", ++version, text).has_value());
                const auto snapshot = host.snapshot();
                const auto check = [&](std::string_view needle,
                                       std::string_view expected) noexcept {
                    const auto start = static_cast<std::uint32_t>(text.rfind(needle));
                    const auto hover = snapshot.hover(project, "app.cv", start);
                    require(hover.result.has_value());
                    expect_equal(hover.result->description, expected).note(text);
                    expect_equal(hover.result->location.range.start(), start);
                };
                check("compute(", "fn compute(left: i32, right: i64) -> i64");
                check("callback", "fn callback(i32, i64) -> i64");
                check("inferred(", "fn inferred(value: i32) -> i64");
                check("add(", "fn add(amount: i32) -> i32");
                check("fetch(", "async fn fetch(value: i32) -> i64");
                const auto hover = snapshot.hover(
                    project,
                    "app.cv",
                    static_cast<std::uint32_t>(text.rfind("compute"))
                );
                const auto help = snapshot.signature_help(
                    project,
                    "app.cv",
                    static_cast<std::uint32_t>(text.size())
                );
                require(help.result.has_value());
                expect_equal(hover.result->description, "fn " + help.result->label);
            }
            const auto before = host.snapshot();
            const auto declaration = before.hover(
                project,
                "lib.cv",
                static_cast<std::uint32_t>(library.find("combine"))
            );
            require(declaration.result.has_value());
            expect_equal(
                declaration.result->description,
                "fn combine(left: i32, right: i64) -> i64"
            );
            for (const auto& [name, expected] : std::array {
                     std::pair {"relay", "fn relay<T>(&&value: T) -> T"},
                     std::pair {"constant", "const fn constant(value: i32) -> i32"}
                 }) {
                const auto hover =
                    before.hover(project, "lib.cv", static_cast<std::uint32_t>(library.find(name)));
                require(hover.result.has_value());
                expect_equal(hover.result->description, expected);
            }
            auto changed = library;
            changed.replace(
                changed.find("right: i64"),
                std::string("right: i64").size(),
                "right: i32"
            );
            changed.replace(changed.find("-> i64"), std::string("-> i64").size(), "-> i32");
            require(host.update("lib.cv", 2, changed).has_value());
            const auto after = host.snapshot().hover(
                project,
                "lib.cv",
                static_cast<std::uint32_t>(changed.find("combine"))
            );
            require(after.result.has_value());
            expect_equal(after.result->description, "fn combine(left: i32, right: i32) -> i32");
            expect_equal(
                declaration.result->description,
                "fn combine(left: i32, right: i64) -> i64"
            );
        };
});

} // namespace
