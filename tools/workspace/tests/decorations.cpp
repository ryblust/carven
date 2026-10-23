module carven:test.workspace.decorations;

import :source.module_path;
import :source.text;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.decorations;
import std;

namespace {

const TestSuite tests([] static noexcept {
    "Workspace: decorations reuse recovered semantics and unopened callable declarations"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(16uz);
            const auto project = std::array {
                WorkspaceProjectModule {"app.cv", *CanonicalModulePath::from_value("pkg.app")},
                WorkspaceProjectModule {"lib.cv", *CanonicalModulePath::from_value("pkg.lib")}
            };
            const auto library = std::string(
                "struct Point { width: i32 } enum Mode { Ready(i32) } fn combine(left: i32, right: i64) -> i64 => right; class Counter { value: i32, fn new(value: i32) -> Counter => { value: value }; fn add(self, amount: i32) -> i32 => self.value + amount; }"
            );
            require(host.update("lib.cv", 1, library).has_value());
            const auto body = std::string(
                "let count = compute(1, 2); var changed = count; let named: i64 = count; let left = 1; compute(left, 2); let p = Point { width: 1 }; let width = p.width; let counter = Counter::new(1); let value = counter.add(2); let mode = Mode::Ready(1); compute(1, "
            );
            auto version = 0ll;
            for (const auto error :
                 {"",
                  "let broken = missing(); ",
                  "let broken = ; ",
                  "let broken = \"unfinished\n"}) {
                const auto text =
                    std::string(
                        "import .lib using { combine as compute, Point, Counter, Mode } fn sample(input: i32) { "
                    )
                    + error + body;
                require(host.update("app.cv", ++version, text).has_value());
                const auto snapshot = host.snapshot();
                const auto query = snapshot.decorations(
                    project,
                    "app.cv",
                    Span::from_bounds(0, static_cast<std::uint32_t>(text.size()))
                );
                const auto hint = [&](std::string_view name, std::string_view expected) noexcept {
                    const auto position = static_cast<std::uint32_t>(
                        text.find(std::string(name) + " =") + name.size()
                    );
                    return std::ranges::any_of(query.result.hints, [&](const auto& value) noexcept {
                        return value.position == position && value.label == expected;
                    });
                };
                expect(hint("count", ": i64")).note(text);
                expect(hint("width", ": i32")).note(text);
                expect(hint("mode", ": pkg.lib::Mode")).note(text);
                expect(hint("value", ": i32")).note(text);
                expect(std::ranges::none_of(query.result.hints, [&](const auto& value) noexcept {
                    return value.position == text.find("named:") + 5;
                }));
                expect(
                    std::ranges::any_of(query.result.hints, [](const auto& value) static noexcept {
                        return value.label == "amount:"
                            && value.kind == WorkspaceHintKind::Parameter;
                    })
                );
                const auto named_argument =
                    static_cast<std::uint32_t>(text.find("compute(left") + 8);
                expect(std::ranges::none_of(query.result.hints, [&](const auto& value) noexcept {
                    return value.position == named_argument && value.label == "left:";
                }));
                const auto highlight = [&](std::string_view needle,
                                           WorkspaceHighlightKind kind,
                                           bool readonly) noexcept {
                    const auto start = static_cast<std::uint32_t>(text.find(needle));
                    return std::ranges::any_of(
                        query.result.highlights,
                        [&](const auto& value) noexcept {
                            return value.range.start() == start
                                && value.kind == kind
                                && ((value.modifiers
                                     & static_cast<unsigned>(WorkspaceHighlightModifier::Readonly))
                                    != 0)
                                == readonly;
                        }
                    );
                };
                expect(highlight("input:", WorkspaceHighlightKind::Parameter, true));
                expect(highlight("count =", WorkspaceHighlightKind::Variable, true));
                expect(highlight("changed =", WorkspaceHighlightKind::Variable, false));
                expect(highlight("add(2)", WorkspaceHighlightKind::Method, false));
                expect(highlight("Ready", WorkspaceHighlightKind::EnumMember, true));
                expect(highlight("width;", WorkspaceHighlightKind::Property, false));
                const auto enumeration = snapshot.definition(
                    project,
                    "app.cv",
                    static_cast<std::uint32_t>(text.find("Ready"))
                );
                require(enumeration.result.has_value());
                expect_equal(enumeration.result->document, "lib.cv");
                expect_equal(slice(library, enumeration.result->range), "Ready");
                const auto hover = snapshot.hover(
                    project,
                    "app.cv",
                    static_cast<std::uint32_t>(text.find("count ="))
                );
                expect_equal(hover.analysis.result, query.analysis.result);
                const auto start = static_cast<std::uint32_t>(text.find("count =") + 5);
                const auto limited =
                    snapshot.decorations(project, "app.cv", Span::from_bounds(start, start + 1));
                require_equal(limited.result.hints.size(), 1uz);
                expect_equal(limited.result.hints.front().label, ": i64");
            }
        };
});

} // namespace
