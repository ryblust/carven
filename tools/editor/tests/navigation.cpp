module carven:test.editor.navigation;

import :editor.analysis;
import :editor.semantic;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import std;

namespace {
namespace ct = carven::testing;

auto project_module(std::string document, std::string_view path) noexcept -> editor::ProjectModule {
    auto canonical = CanonicalModulePath::from_value(path);
    ct::require(canonical.has_value());
    return {.document = std::move(document), .module_path = std::move(*canonical)};
}

auto update(
    editor::AnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    ct::require(host.update(std::string(document), version, std::string(text)).has_value());
}

auto offset(std::string_view text, std::string_view needle, bool last = false) noexcept
    -> std::uint32_t {
    const auto position = last ? text.rfind(needle) : text.find(needle);
    ct::require(position != std::string_view::npos);
    return static_cast<std::uint32_t>(position);
}

auto expect_type(const editor::HoverQuery& query, BuiltinType type) noexcept -> void {
    const auto* program = query.analysis.result->program();
    if (!ct::expect(program != nullptr && query.result.has_value())) {
        return;
    }
    const auto* published = std::get_if<TypeID>(&query.result->type);
    ct::expect(published != nullptr && *published == program->types().builtin_type(type));
}

auto expect_target(
    const editor::DefinitionQuery& query,
    std::string_view document,
    std::int64_t version,
    std::uint32_t start,
    std::string_view name
) noexcept -> void {
    if (!ct::expect(query.result.has_value())) {
        return;
    }
    ct::expect_equal(query.result->document, document);
    ct::expect_equal(query.result->version, version);
    ct::expect_equal(query.result->range.start(), start);
    const auto source = query.analysis.result->source(document);
    if (!ct::expect(source.has_value())) {
        return;
    }
    ct::expect_equal(slice(source->text, query.result->range), name);
}

auto expect_references(
    const editor::ReferencesQuery& query,
    std::string_view document,
    std::int64_t version,
    std::span<const std::uint32_t> starts,
    std::string_view name
) noexcept -> void {
    if (!ct::expect(query.result.has_value())) {
        return;
    }
    if (!ct::expect_equal(query.result->size(), starts.size())) {
        return;
    }
    for (auto index = 0uz; index < starts.size(); ++index) {
        const auto& location = (*query.result)[index];
        ct::expect_equal(location.document, document);
        ct::expect_equal(location.version, version);
        ct::expect_equal(location.range.start(), starts[index]);
        const auto source = query.analysis.result->source(location.document);
        if (ct::expect(source.has_value())) {
            ct::expect_equal(slice(source->text, location.range), name);
        }
    }
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "Editor analysis: navigation resolves local shadowing by semantic identity",
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn f(value: i32) -> i32 { let outer = value; if true { let value: i64 = 2; let inner = value; } return value; }"
            );
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, text);
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto snapshot = host.snapshot();
            const auto parameter = offset(text, "value: i32");
            const auto local = offset(text, "value: i64");
            const auto outer_use = offset(text, "= value;") + 2u;
            const auto inner_use = offset(text, "= value;", true) + 2u;
            const auto return_use = offset(text, "return value") + 7u;
            expect_type(snapshot.hover(modules, "a.cv", outer_use), BuiltinType::I32);
            expect_type(snapshot.hover(modules, "a.cv", inner_use), BuiltinType::I64);
            expect_type(snapshot.hover(modules, "a.cv", return_use), BuiltinType::I32);
            expect_target(
                snapshot.definition(modules, "a.cv", outer_use),
                "a.cv",
                1,
                parameter,
                "value"
            );
            expect_target(
                snapshot.definition(modules, "a.cv", inner_use),
                "a.cv",
                1,
                local,
                "value"
            );
            expect_target(
                snapshot.definition(modules, "a.cv", return_use),
                "a.cv",
                1,
                parameter,
                "value"
            );
            expect_references(
                snapshot.references(modules, "a.cv", outer_use),
                "a.cv",
                1,
                std::array {parameter, outer_use, return_use},
                "value"
            );
            expect_references(
                snapshot.references(modules, "a.cv", inner_use),
                "a.cv",
                1,
                std::array {local, inner_use},
                "value"
            );
            ct::expect_equal(snapshot.counts().semantic, 1uz);
        }
    );

    ct::test(
        "Editor analysis: cross-file definitions use snapshot versions while reusing content",
        [] static noexcept {
            constexpr auto library = std::string_view("export fn answer() -> i32 { return 42; }");
            constexpr auto caller = std::string_view(
                "import lib using answer; fn f() -> i32 { let v = answer(); return v; }"
            );
            auto host = editor::AnalysisHost();
            update(host, "lib.cv", 1, library);
            update(host, "app.cv", 4, caller);
            const auto modules =
                std::array {project_module("lib.cv", "lib"), project_module("app.cv", "app")};
            const auto before = host.snapshot();
            const auto call = offset(caller, "answer()", true);
            const auto original = before.definition(modules, "app.cv", call);
            expect_target(original, "lib.cv", 1, offset(library, "answer"), "answer");
            expect_type(
                before.hover(modules, "app.cv", offset(caller, "return v") + 7u),
                BuiltinType::I32
            );
            update(host, "lib.cv", 2, library);
            const auto after = host.snapshot();
            const auto current = after.definition(modules, "app.cv", call);
            expect_target(current, "lib.cv", 2, offset(library, "answer"), "answer");
            expect_target(
                before.definition(modules, "app.cv", call),
                "lib.cv",
                1,
                offset(library, "answer"),
                "answer"
            );
            const auto references = after.references(modules, "app.cv", call);
            ct::require(references.result.has_value());
            ct::require(references.result->size() == 2uz);
            ct::expect_equal((*references.result)[0].document, "app.cv");
            ct::expect_equal((*references.result)[0].version, 4ll);
            ct::expect_equal((*references.result)[0].range.start(), call);
            ct::expect_equal((*references.result)[1].document, "lib.cv");
            ct::expect_equal((*references.result)[1].version, 2ll);
            ct::expect_equal((*references.result)[1].range.start(), offset(library, "answer"));
            const auto old_references = before.references(modules, "app.cv", call);
            ct::require(old_references.result.has_value());
            ct::require(old_references.result->size() == 2uz);
            ct::expect_equal((*old_references.result)[1].version, 1ll);
            ct::expect(current.analysis.result == original.analysis.result);
            ct::expect_equal(after.counts().semantic, 1uz);
            update(host, "lib.cv", 3, "// moved\nexport fn answer() -> i32 { return 43; }");
            const auto edited = host.snapshot().definition(modules, "app.cv", call);
            expect_target(edited, "lib.cv", 3, offset(library, "answer") + 9u, "answer");
            ct::expect(edited.analysis.result != original.analysis.result);
            expect_target(original, "lib.cv", 1, offset(library, "answer"), "answer");
        }
    );

    ct::test(
        "Editor analysis: function definitions ignore same-name export prefix tokens",
        [] static noexcept {
            constexpr auto text = std::string_view(
                "export(cpp) fn cpp() -> i32 { return 42; } fn caller() -> i32 { return cpp(); }"
            );
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, text);
            const auto modules = std::array {project_module("a.cv", "main")};
            expect_target(
                host.snapshot().definition(modules, "a.cv", offset(text, "cpp()", true)),
                "a.cv",
                1,
                offset(text, "cpp()"),
                "cpp"
            );
        }
    );

    ct::test(
        "Editor analysis: field navigation selects the actual member token",
        [] static noexcept {
            constexpr auto text = std::string_view(
                "struct Pair { value: i32, } fn f(p: Pair) -> i32 { return p.value; }"
            );
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 7, text);
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto snapshot = host.snapshot();
            const auto use = offset(text, "value", true);
            const auto hover = snapshot.hover(modules, "a.cv", use);
            expect_type(hover, BuiltinType::I32);
            if (ct::expect(hover.result.has_value())) {
                ct::expect_equal(hover.result->location.range.start(), use);
                ct::expect_equal(hover.result->location.range.end(), use + 5u);
            }
            expect_target(
                snapshot.definition(modules, "a.cv", use),
                "a.cv",
                7,
                offset(text, "value"),
                "value"
            );
            ct::expect(!snapshot.definition(modules, "a.cv", use + 5u).result);
        }
    );

    ct::test(
        "Editor analysis: changed inferred types retain independent owning results",
        [] static noexcept {
            constexpr auto first =
                std::string_view("fn f() -> i32 { let value = 1; return value; }");
            constexpr auto second =
                std::string_view("fn f() -> i64 { let value: i64 = 1; return value; }");
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, first);
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto old = host.snapshot().hover(modules, "a.cv", offset(first, "value", true));
            const auto retained_references =
                host.snapshot().references(modules, "a.cv", offset(first, "value", true));
            update(host, "a.cv", 2, second);
            const auto current =
                host.snapshot().hover(modules, "a.cv", offset(second, "value", true));
            ct::expect(host.remove("a.cv"));
            expect_references(
                retained_references,
                "a.cv",
                1,
                std::array {offset(first, "value"), offset(first, "value", true)},
                "value"
            );
            expect_type(old, BuiltinType::I32);
            expect_type(current, BuiltinType::I64);
            ct::expect(old.analysis.result != current.analysis.result);
            ct::expect_equal(old.analysis.result->source("a.cv")->text, first);
            ct::expect_equal(current.analysis.result->source("a.cv")->text, second);
        }
    );

    ct::test(
        "Editor analysis: published source types include nominal array and callable bindings",
        [] static noexcept {
            constexpr auto text = std::string_view(
                "struct Pair { value: i32, } fn f(record: Pair, values: [i32; 2], callback: fn() -> i32) { let first = record; let copied = values; let cb = callback; }"
            );
            auto host = editor::AnalysisHost();
            update(host, "types.cv", 1, text);
            const auto modules = std::array {project_module("types.cv", "main")};
            const auto snapshot = host.snapshot();
            enum class Shape { Structure, Array, Callable };
            struct Scenario final {
                std::string_view name;
                Shape shape;
            };
            const auto scenarios = std::array {
                Scenario {.name = "record", .shape = Shape::Structure},
                Scenario {.name = "values", .shape = Shape::Array},
                Scenario {.name = "callback", .shape = Shape::Callable},
            };
            ct::each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                const auto use = offset(text, scenario.name, true);
                const auto query = snapshot.hover(modules, "types.cv", use);
                ct::require(query.result.has_value());
                const auto* program = query.analysis.result->program();
                ct::require(program != nullptr);
                const auto* type = std::get_if<TypeID>(&query.result->type);
                ct::require(type != nullptr);
                const auto& canonical = program->types().type(*type);
                switch (scenario.shape) {
                    case Shape::Structure:
                        ct::expect(std::holds_alternative<StructTypeValue>(canonical.value));
                        break;
                    case Shape::Array:
                        ct::expect(std::holds_alternative<ArrayTypeValue>(canonical.value));
                        break;
                    case Shape::Callable:
                        ct::expect(std::holds_alternative<CallableViewTypeValue>(canonical.value));
                        break;
                }
                ct::expect_equal(query.result->location.range.start(), use);
                expect_target(
                    snapshot.definition(modules, "types.cv", use),
                    "types.cv",
                    1,
                    offset(text, scenario.name),
                    scenario.name
                );
            });
        }
    );

    ct::test(
        "Editor analysis: unavailable semantics and out-of-range offsets have no navigation",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn f() -> i32 { return unknown; }");
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto invalid = host.snapshot();
            const auto failed = invalid.hover(modules, "a.cv", 23u);
            ct::expect(failed.analysis.result->program() == nullptr);
            ct::expect(!failed.result);
            ct::expect(!invalid.references(modules, "a.cv", 23u).result);
            ct::expect(!invalid.definition(modules, "a.cv", 23u).result);
            constexpr auto text =
                std::string_view("fn f() -> i32 { let value = 1; return value; }");
            update(host, "a.cv", 2, text);
            const auto repaired = host.snapshot();
            ct::expect(!repaired.hover(modules, "absent.cv", 0u).result);
            ct::expect(
                !repaired.hover(modules, "a.cv", static_cast<std::uint32_t>(text.size())).result
            );
            ct::expect(!repaired
                            .definition(modules, "a.cv", std::numeric_limits<std::uint32_t>::max())
                            .result);
            expect_type(
                repaired.hover(modules, "a.cv", offset(text, "value", true)),
                BuiltinType::I32
            );
        }
    );
});
} // namespace
