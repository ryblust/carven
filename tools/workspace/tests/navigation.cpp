module carven:test.workspace.navigation;

import :diagnostics.code;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {
auto project_module(std::string document, std::string_view path) noexcept
    -> WorkspaceProjectModule {
    auto canonical = CanonicalModulePath::from_value(path);
    require(canonical.has_value());
    return {.document = std::move(document), .module_path = std::move(*canonical)};
}

auto update(
    WorkspaceAnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    require(host.update(std::string(document), version, std::string(text)).has_value());
}

auto offset(std::string_view text, std::string_view needle, bool last = false) noexcept
    -> std::uint32_t {
    const auto position = last ? text.rfind(needle) : text.find(needle);
    require(position != std::string_view::npos);
    return static_cast<std::uint32_t>(position);
}

auto expect_type(const WorkspaceHoverQuery& query, BuiltinType type) noexcept -> void {
    const auto* program = query.analysis.result->program();
    if (!expect(program != nullptr && query.result.has_value())) {
        return;
    }
    const auto* published = std::get_if<TypeID>(&query.result->type);
    expect(published != nullptr && *published == program->types().builtin_type(type));
}

auto expect_target(
    const WorkspaceDefinitionQuery& query,
    std::string_view document,
    std::int64_t version,
    std::uint32_t start,
    std::string_view name
) noexcept -> void {
    if (!expect(query.result.has_value())) {
        return;
    }
    expect_equal(query.result->document, document);
    expect_equal(query.result->version, version);
    expect_equal(query.result->range.start(), start);
    const auto source = query.analysis.result->source(document);
    if (!expect(source.has_value())) {
        return;
    }
    expect_equal(slice(source->text, query.result->range), name);
}

auto expect_references(
    const WorkspaceReferencesQuery& query,
    std::string_view document,
    std::int64_t version,
    std::span<const std::uint32_t> starts,
    std::string_view name
) noexcept -> void {
    if (!expect(query.result.has_value())) {
        return;
    }
    if (!expect_equal(query.result->size(), starts.size())) {
        return;
    }
    for (auto index = 0uz; index < starts.size(); ++index) {
        const auto& location = (*query.result)[index];
        expect_equal(location.document, document);
        expect_equal(location.version, version);
        expect_equal(location.range.start(), starts[index]);
        const auto source = query.analysis.result->source(location.document);
        if (expect(source.has_value())) {
            expect_equal(slice(source->text, location.range), name);
        }
    }
}

const TestSuite tests([] static noexcept {
    "Workspace analysis: ordinary static expressions retain names fields and type anchors"_test =
        [] static noexcept {
            constexpr auto healthy = std::string_view(
                "struct Options { enabled: bool, value: i32, }\n"
                "const answer: i32 = 42;\n"
                "const derived: i32 = answer;\n"
                "fn healthy() -> i32 {\n"
                "    const a = answer;\n"
                "    const b = a + 1;\n"
                "    const options = Options { enabled: true, value: b };\n"
                "    const selected = options.value;\n"
                "    const count = 2usize;\n"
                "    const values: [i32; count] = [selected, selected];\n"
                "    const if options.enabled { let checked = b; } else { let checked = b; }\n"
                "    return values[0];\n"
                "}\n"
            );
            constexpr auto broken = std::string_view(
                "fn broken() -> i32 { const lost = answer; const bad = missing; return bad; }\n"
            );
            each(
                std::array {false, true},
                [](bool failed) static noexcept { return failed ? "failed body" : "published"; },
                [&](bool failed) noexcept {
                    const auto text = std::string(healthy) + (failed ? std::string(broken) : "");
                    auto host = WorkspaceAnalysisHost();
                    update(host, "a.cv", 4, text);
                    const auto modules = std::array {project_module("a.cv", "main")};
                    const auto snapshot = host.snapshot();
                    const auto analysis = snapshot.semantic(modules);
                    if (failed) {
                        expect(analysis.result->program() == nullptr);
                        expect_diagnostic(
                            analysis.result->diagnostics(),
                            DiagnosticCode::NameUnresolved
                        );
                    } else if (!expect(analysis.result->program() != nullptr)) {
                        return;
                    }
                    struct Target final {
                        std::string_view name;
                        BuiltinType type;
                        std::vector<std::uint32_t> occurrences;
                    };
                    const auto targets = std::array {
                        Target {
                            .name = "answer",
                            .type = BuiltinType::I32,
                            .occurrences =
                                {
                                    offset(text, "answer: i32"),
                                    offset(text, "a = answer") + 4u,
                                },
                        },
                        Target {
                            .name = "a",
                            .type = BuiltinType::I32,
                            .occurrences =
                                {
                                    offset(text, "const a =") + 6u,
                                    offset(text, "b = a") + 4u,
                                },
                        },
                        Target {
                            .name = "b",
                            .type = BuiltinType::I32,
                            .occurrences =
                                {
                                    offset(text, "const b") + 6u,
                                    offset(text, "value: b") + 7u,
                                    offset(text, "checked = b") + 10u,
                                    offset(text, "checked = b", true) + 10u,
                                },
                        },
                        Target {
                            .name = "value",
                            .type = BuiltinType::I32,
                            .occurrences =
                                {
                                    offset(text, "value: i32"),
                                    offset(text, "options.value") + 8u,
                                },
                        },
                        Target {
                            .name = "enabled",
                            .type = BuiltinType::Bool,
                            .occurrences =
                                {
                                    offset(text, "enabled: bool"),
                                    offset(text, "options.enabled") + 8u,
                                },
                        },
                        Target {
                            .name = "count",
                            .type = BuiltinType::Usize,
                            .occurrences = {
                                offset(text, "const count") + 6u,
                                offset(text, "i32; count") + 5u,
                            },
                        },
                    };
                    each(targets, &Target::name, [&](const Target& target) noexcept {
                        for (const auto use : target.occurrences) {
                            expect_target(
                                snapshot.definition(modules, "a.cv", use),
                                "a.cv",
                                4,
                                target.occurrences.front(),
                                target.name
                            );
                            expect_references(
                                snapshot.references(modules, "a.cv", use),
                                "a.cv",
                                4,
                                target.occurrences,
                                target.name
                            );
                        }
                        // Catalog declaration navigation does not imply a type observation.
                        const auto use = target.occurrences.back();
                        const auto hover = snapshot.hover(modules, "a.cv", use);
                        if (failed) {
                            if (expect(hover.result.has_value())) {
                                const auto* builtin = std::get_if<BuiltinType>(&hover.result->type);
                                expect(builtin != nullptr && *builtin == target.type);
                            }
                        } else {
                            expect_type(hover, target.type);
                        }
                    });
                    if (!failed) {
                        expect_type(
                            snapshot.hover(modules, "a.cv", offset(text, "+ 1")),
                            BuiltinType::I32
                        );
                        expect_type(
                            snapshot.hover(modules, "a.cv", offset(text, "2usize")),
                            BuiltinType::Usize
                        );
                    }
                    const auto initializer = offset(text, "derived: i32 = answer") + 15u;
                    expect(!snapshot.definition(modules, "a.cv", initializer).result);
                    expect(!snapshot.hover(modules, "a.cv", initializer).result);
                    if (failed) {
                        const auto discarded = offset(text, "lost = answer") + 7u;
                        expect(!snapshot.definition(modules, "a.cv", discarded).result);
                        expect(!snapshot.hover(modules, "a.cv", discarded).result);
                    }
                }
            );
        };

    "Workspace analysis: or-pattern binding selections share one declaration identity"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "enum Value { First(i32), Second(i32), }\n"
                "fn choose(input: Value) -> i32 {\n"
                "    return match input {\n"
                "        .First(selected) | .Second(selected) => selected,\n"
                "    };\n"
                "}\n"
            );
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 1, text);
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto snapshot = host.snapshot();
            if (!expect(snapshot.semantic(modules).result->program() != nullptr)) {
                return;
            }
            const auto first = offset(text, "First(selected)") + 6u;
            const auto second = offset(text, "Second(selected)") + 7u;
            const auto use = offset(text, "=> selected") + 3u;
            for (const auto selected : std::array {first, second, use}) {
                expect_type(snapshot.hover(modules, "a.cv", selected), BuiltinType::I32);
                expect_target(
                    snapshot.definition(modules, "a.cv", selected),
                    "a.cv",
                    1,
                    first,
                    "selected"
                );
                expect_references(
                    snapshot.references(modules, "a.cv", selected),
                    "a.cv",
                    1,
                    std::array {first, second, use},
                    "selected"
                );
            }
        };

    "Workspace analysis: catalog declarations and resolved uses share navigation identities"_test =
        [] static noexcept {
            constexpr auto library = std::string_view(
                "export const answer: i32 = 42;\n"
                "export enum State { Ready, Done, }\n"
                "export enum Other { Ready, Done, }\n"
                "export enum Choice { Value(i32), Empty, }\n"
                "export struct Record { tag: bool, value: i32, }\n"
                "export class Counter { fn create() -> Counter => {}; fn read(self) -> i32 => 7; }\n"
                "export fn identity(value: i32) -> i32 => value;\n"
            );
            constexpr auto caller = std::string_view(
                "import lib using { answer, State, Other, Choice, Record, Counter, identity };\n"
                "fn use(record: Record) -> i32 {\n"
                "    let state = State::Ready;\n"
                "    let other = Other::Ready;\n"
                "    let choice = Choice::Value(answer);\n"
                "    let empty: Choice = .Empty;\n"
                "    let picked = match choice {\n"
                "        Choice::Value(selected) => selected,\n"
                "        .Empty => 0,\n"
                "    };\n"
                "    let counter = Counter::create();\n"
                "    let current = counter.read();\n"
                "    let value = identity(record.value);\n"
                "    return answer + value;\n"
                "}\n"
            );
            auto host = WorkspaceAnalysisHost();
            update(host, "lib.cv", 3, library);
            update(host, "app.cv", 7, caller);
            const auto modules =
                std::array {project_module("lib.cv", "lib"), project_module("app.cv", "app")};
            const auto snapshot = host.snapshot();
            if (!expect(snapshot.semantic(modules).result->program() != nullptr)) {
                return;
            }
            struct Target final {
                std::string_view name;
                std::uint32_t declaration;
                std::vector<std::uint32_t> uses;
            };
            const auto targets = std::array {
                Target {
                    .name = "answer",
                    .declaration = offset(library, "answer"),
                    .uses =
                        {offset(caller, "Value(answer)") + 6u, offset(caller, "return answer") + 7u}
                },
                Target {
                    .name = "State",
                    .declaration = offset(library, "State"),
                    .uses = {offset(caller, "State::Ready")}
                },
                Target {
                    .name = "Ready",
                    .declaration = offset(library, "Ready"),
                    .uses = {offset(caller, "State::Ready") + 7u}
                },
                Target {
                    .name = "Ready",
                    .declaration = offset(library, "Ready", true),
                    .uses = {offset(caller, "Other::Ready") + 7u}
                },
                Target {
                    .name = "Choice",
                    .declaration = offset(library, "Choice"),
                    .uses = {offset(caller, "Choice::Value"), offset(caller, "Choice::Value", true)}
                },
                Target {
                    .name = "Value",
                    .declaration = offset(library, "Value"),
                    .uses =
                        {offset(caller, "Choice::Value") + 8u,
                         offset(caller, "Choice::Value", true) + 8u}
                },
                Target {
                    .name = "Empty",
                    .declaration = offset(library, "Empty"),
                    .uses = {offset(caller, ".Empty") + 1u, offset(caller, ".Empty", true) + 1u}
                },
                Target {
                    .name = "Counter",
                    .declaration = offset(library, "Counter"),
                    .uses = {offset(caller, "Counter::create")}
                },
                Target {
                    .name = "create",
                    .declaration = offset(library, "create"),
                    .uses = {offset(caller, "Counter::create") + 9u}
                },
                Target {
                    .name = "read",
                    .declaration = offset(library, "read"),
                    .uses = {offset(caller, "counter.read") + 8u}
                },
                Target {
                    .name = "identity",
                    .declaration = offset(library, "identity"),
                    .uses = {offset(caller, "identity(record")}
                },
                Target {
                    .name = "value",
                    .declaration = offset(library, "value"),
                    .uses = {offset(caller, "record.value") + 7u}
                },
            };
            each(targets, &Target::name, [&](const Target& target) noexcept {
                expect_target(
                    snapshot.definition(modules, "lib.cv", target.declaration),
                    "lib.cv",
                    3,
                    target.declaration,
                    target.name
                );
                for (const auto use : target.uses) {
                    expect_target(
                        snapshot.definition(modules, "app.cv", use),
                        "lib.cv",
                        3,
                        target.declaration,
                        target.name
                    );
                }
                const auto references = snapshot.references(modules, "lib.cv", target.declaration);
                if (!expect(references.result.has_value())
                    || !expect_equal(references.result->size(), target.uses.size() + 1uz)) {
                    return;
                }
                for (auto index = 0uz; index < target.uses.size(); ++index) {
                    const auto& location = (*references.result)[index];
                    expect_equal(location.document, "app.cv");
                    expect_equal(location.version, 7ll);
                    expect_equal(location.range.start(), target.uses[index]);
                    expect_equal(slice(caller, location.range), target.name);
                }
                const auto& declaration = references.result->back();
                expect_equal(declaration.document, "lib.cv");
                expect_equal(declaration.version, 3ll);
                expect_equal(declaration.range.start(), target.declaration);
                expect_equal(slice(library, declaration.range), target.name);
            });
            expect_target(
                snapshot.definition(modules, "lib.cv", offset(library, "Record")),
                "lib.cv",
                3,
                offset(library, "Record"),
                "Record"
            );
            expect(!snapshot.definition(modules, "app.cv", offset(caller, "record: Record") + 8u)
                        .result);
        };

    "Workspace analysis: declaration navigation survives failed bodies without admitting their uses"_test =
        [] static noexcept {
            constexpr auto declarations = std::string_view(
                "const answer: i32 = 42;\n"
                "enum State { Ready, Done, }\n"
                "struct Pair { value: i32, }\n"
                "fn identity(value: i32) -> i32 => value;\n"
            );
            constexpr auto healthy = std::string_view(
                "fn healthy(pair: Pair) -> i32 {\n"
                "    let before = answer;\n"
                "    if true { let answer: i32 = 7; let inner = answer; }\n"
                "    let state = State::Ready;\n"
                "    return identity(pair.value) + answer;\n"
                "}\n"
            );
            constexpr auto broken = std::string_view(
                "fn broken() -> i32 { let discarded = answer; return missing; }\n"
            );
            each(
                std::array {false, true},
                [](bool first) static noexcept { return first ? "broken first" : "broken last"; },
                [&](bool broken_first) noexcept {
                    const auto text = std::string(declarations)
                        + (broken_first ? std::string(broken) + std::string(healthy)
                                        : std::string(healthy) + std::string(broken));
                    auto host = WorkspaceAnalysisHost();
                    update(host, "a.cv", 2, text);
                    const auto modules = std::array {project_module("a.cv", "main")};
                    const auto snapshot = host.snapshot();
                    const auto analysis = snapshot.semantic(modules);
                    expect(analysis.result->program() == nullptr);
                    expect_diagnostic(
                        analysis.result->diagnostics(),
                        DiagnosticCode::NameUnresolved
                    );
                    const auto declaration = offset(text, "answer");
                    const auto first_use = offset(text, "before = answer") + 9u;
                    const auto last_use = offset(text, "+ answer") + 2u;
                    expect_references(
                        snapshot.references(modules, "a.cv", declaration),
                        "a.cv",
                        2,
                        std::array {declaration, first_use, last_use},
                        "answer"
                    );
                    const auto local = offset(text, "let answer") + 4u;
                    const auto local_use = offset(text, "inner = answer") + 8u;
                    expect_references(
                        snapshot.references(modules, "a.cv", local),
                        "a.cv",
                        2,
                        std::array {local, local_use},
                        "answer"
                    );
                    expect_target(
                        snapshot.definition(modules, "a.cv", last_use),
                        "a.cv",
                        2,
                        declaration,
                        "answer"
                    );
                    expect_target(
                        snapshot.definition(modules, "a.cv", local_use),
                        "a.cv",
                        2,
                        local,
                        "answer"
                    );
                    for (const auto name : std::array {
                             std::string_view("State"),
                             std::string_view("Ready"),
                             std::string_view("identity"),
                             std::string_view("value")
                         }) {
                        expect_target(
                            snapshot.definition(modules, "a.cv", offset(text, name, true)),
                            "a.cv",
                            2,
                            offset(text, name),
                            name
                        );
                    }
                    const auto discarded = offset(text, "discarded = answer") + 12u;
                    expect(!snapshot.definition(modules, "a.cv", discarded).result);
                    expect(!snapshot.references(modules, "a.cv", discarded).result);
                }
            );
        };

    "Workspace analysis: navigation resolves local shadowing by semantic identity"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "fn f(value: i32) -> i32 { let outer = value; if true { let value: i64 = 2; let inner = value; } return value; }"
            );
            auto host = WorkspaceAnalysisHost();
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
            expect_equal(snapshot.counts().semantic, 1uz);
        };

    "Workspace analysis: cross-file definitions use snapshot versions while reusing content"_test =
        [] static noexcept {
            constexpr auto library = std::string_view("export fn answer() -> i32 { return 42; }");
            constexpr auto caller = std::string_view(
                "import lib using answer; fn f() -> i32 { let v = answer(); return v; }"
            );
            auto host = WorkspaceAnalysisHost();
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
            require(references.result.has_value());
            require(references.result->size() == 2uz);
            expect_equal((*references.result)[0].document, "app.cv");
            expect_equal((*references.result)[0].version, 4ll);
            expect_equal((*references.result)[0].range.start(), call);
            expect_equal((*references.result)[1].document, "lib.cv");
            expect_equal((*references.result)[1].version, 2ll);
            expect_equal((*references.result)[1].range.start(), offset(library, "answer"));
            const auto old_references = before.references(modules, "app.cv", call);
            require(old_references.result.has_value());
            require(old_references.result->size() == 2uz);
            expect_equal((*old_references.result)[1].version, 1ll);
            expect(current.analysis.result == original.analysis.result);
            expect_equal(after.counts().semantic, 1uz);
            update(host, "lib.cv", 3, "// moved\nexport fn answer() -> i32 { return 43; }");
            const auto edited = host.snapshot().definition(modules, "app.cv", call);
            expect_target(edited, "lib.cv", 3, offset(library, "answer") + 9u, "answer");
            expect(edited.analysis.result != original.analysis.result);
            expect_target(original, "lib.cv", 1, offset(library, "answer"), "answer");
        };

    "Workspace analysis: function definitions ignore same-name export prefix tokens"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "export(cpp) fn cpp() -> i32 { return 42; } fn caller() -> i32 { return cpp(); }"
            );
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 1, text);
            const auto modules = std::array {project_module("a.cv", "main")};
            expect_target(
                host.snapshot().definition(modules, "a.cv", offset(text, "cpp()", true)),
                "a.cv",
                1,
                offset(text, "cpp()"),
                "cpp"
            );
        };

    "Workspace analysis: field navigation selects the actual member token"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "struct Pair { value: i32, } fn f(p: Pair) -> i32 { return p.value; }"
            );
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 7, text);
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto snapshot = host.snapshot();
            const auto use = offset(text, "value", true);
            const auto hover = snapshot.hover(modules, "a.cv", use);
            expect_type(hover, BuiltinType::I32);
            if (expect(hover.result.has_value())) {
                expect_equal(hover.result->location.range.start(), use);
                expect_equal(hover.result->location.range.end(), use + 5u);
            }
            expect_target(
                snapshot.definition(modules, "a.cv", use),
                "a.cv",
                7,
                offset(text, "value"),
                "value"
            );
            expect(!snapshot.definition(modules, "a.cv", use + 5u).result);
        };

    "Workspace analysis: changed inferred types retain independent owning results"_test =
        [] static noexcept {
            constexpr auto first =
                std::string_view("fn f() -> i32 { let value = 1; return value; }");
            constexpr auto second =
                std::string_view("fn f() -> i64 { let value: i64 = 1; return value; }");
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 1, first);
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto old = host.snapshot().hover(modules, "a.cv", offset(first, "value", true));
            const auto retained_references =
                host.snapshot().references(modules, "a.cv", offset(first, "value", true));
            update(host, "a.cv", 2, second);
            const auto current =
                host.snapshot().hover(modules, "a.cv", offset(second, "value", true));
            expect(host.remove("a.cv"));
            expect_references(
                retained_references,
                "a.cv",
                1,
                std::array {offset(first, "value"), offset(first, "value", true)},
                "value"
            );
            expect_type(old, BuiltinType::I32);
            expect_type(current, BuiltinType::I64);
            expect(old.analysis.result != current.analysis.result);
            expect_equal(old.analysis.result->source("a.cv")->text, first);
            expect_equal(current.analysis.result->source("a.cv")->text, second);
        };

    "Workspace analysis: published source types include nominal array and callable bindings"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "struct Pair { value: i32, } fn f(record: Pair, values: [i32; 2], callback: fn() -> i32) { let first = record; let copied = values; let cb = callback; }"
            );
            auto host = WorkspaceAnalysisHost();
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
            each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                const auto use = offset(text, scenario.name, true);
                const auto query = snapshot.hover(modules, "types.cv", use);
                require(query.result.has_value());
                const auto* program = query.analysis.result->program();
                require(program != nullptr);
                const auto* type = std::get_if<TypeID>(&query.result->type);
                require(type != nullptr);
                const auto& canonical = program->types().type(*type);
                switch (scenario.shape) {
                    case Shape::Structure:
                        expect(std::holds_alternative<StructTypeValue>(canonical.value));
                        break;
                    case Shape::Array:
                        expect(std::holds_alternative<ArrayTypeValue>(canonical.value));
                        break;
                    case Shape::Callable:
                        expect(std::holds_alternative<CallableViewTypeValue>(canonical.value));
                        break;
                }
                expect_equal(query.result->location.range.start(), use);
                expect_target(
                    snapshot.definition(modules, "types.cv", use),
                    "types.cv",
                    1,
                    offset(text, scenario.name),
                    scenario.name
                );
            });
        };

    "Workspace analysis: unavailable semantics and out-of-range offsets have no navigation"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            update(host, "a.cv", 1, "fn f() -> i32 { return unknown; }");
            const auto modules = std::array {project_module("a.cv", "main")};
            const auto invalid = host.snapshot();
            const auto failed = invalid.hover(modules, "a.cv", 23u);
            expect(failed.analysis.result->program() == nullptr);
            expect(!failed.result);
            expect(!invalid.references(modules, "a.cv", 23u).result);
            expect(!invalid.definition(modules, "a.cv", 23u).result);
            constexpr auto text =
                std::string_view("fn f() -> i32 { let value = 1; return value; }");
            update(host, "a.cv", 2, text);
            const auto repaired = host.snapshot();
            expect(!repaired.hover(modules, "absent.cv", 0u).result);
            expect(
                !repaired.hover(modules, "a.cv", static_cast<std::uint32_t>(text.size())).result
            );
            expect(!repaired.definition(modules, "a.cv", std::numeric_limits<std::uint32_t>::max())
                        .result);
            expect_type(
                repaired.hover(modules, "a.cv", offset(text, "value", true)),
                BuiltinType::I32
            );
        };
});
} // namespace
