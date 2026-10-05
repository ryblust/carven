module carven:test.workspace.observations;

import :diagnostics.code;
import :semantic.evaluation.output;
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

auto expect_type(
    const WorkspaceHoverQuery& query,
    BuiltinType expected,
    std::string_view document,
    std::string_view name
) noexcept -> void {
    if (!expect(query.result.has_value())) {
        return;
    }
    if (const auto* builtin = std::get_if<BuiltinType>(&query.result->type)) {
        expect_equal(*builtin, expected);
        expect(query.analysis.result->program() == nullptr);
    } else {
        const auto* program = query.analysis.result->program();
        const auto* published = std::get_if<TypeID>(&query.result->type);
        expect(
            program != nullptr
            && published != nullptr
            && *published == program->types().builtin_type(expected)
        );
    }
    expect_equal(query.result->location.document, document);
    const auto source = query.analysis.result->source(document);
    if (!expect(source.has_value())) {
        return;
    }
    expect_equal(slice(source->text, query.result->location.range), name);
}

auto expect_definition(
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

const TestSuite tests([] static noexcept {
    "Workspace analysis: healthy source occurrences survive failed bodies in either declaration order"_test =
        [] static noexcept {
            const auto healthy = std::string_view(
                "fn healthy(value: i32) -> i32 { let outer: i32 = value; if true { let value: i64 = 2; let inner: i64 = value; } return value; }"
            );
            const auto broken = std::string_view(
                "fn broken() -> i32 { let doomed: i32 = missing; return doomed; }"
            );
            each(
                std::array {false, true},
                [](bool first) static noexcept { return first ? "broken first" : "healthy first"; },
                [&](bool broken_first) noexcept {
                    auto host = WorkspaceAnalysisHost();
                    const auto text = broken_first ? std::format("{}\n{}", broken, healthy)
                                                   : std::format("{}\n{}", healthy, broken);
                    update(host, "untitled:source", 1, text);
                    const auto project = std::array {project_module("untitled:source", "main")};
                    const auto snapshot = host.snapshot();
                    const auto outer_use = offset(text, "= value;") + 2u;
                    const auto inner_use = offset(text, "= value;", true) + 2u;
                    const auto return_use = offset(text, "return value") + 7u;
                    expect_type(
                        snapshot.hover(project, "untitled:source", offset(text, "value: i32")),
                        BuiltinType::I32,
                        "untitled:source",
                        "value"
                    );
                    expect_type(
                        snapshot.hover(project, "untitled:source", offset(text, "outer: i32")),
                        BuiltinType::I32,
                        "untitled:source",
                        "outer"
                    );
                    expect_type(
                        snapshot.hover(project, "untitled:source", outer_use),
                        BuiltinType::I32,
                        "untitled:source",
                        "value"
                    );
                    expect_type(
                        snapshot.hover(project, "untitled:source", inner_use),
                        BuiltinType::I64,
                        "untitled:source",
                        "value"
                    );
                    const auto query = snapshot.hover(project, "untitled:source", return_use);
                    expect_type(query, BuiltinType::I32, "untitled:source", "value");
                    expect(query.analysis.result->program() == nullptr);
                    const auto references =
                        snapshot.references(project, "untitled:source", return_use);
                    require(references.result.has_value());
                    expect_equal(references.result->size(), 3uz);
                    for (const auto& reference : *references.result) {
                        expect_equal(reference.document, "untitled:source");
                        expect_equal(reference.version, 1ll);
                        expect_equal(slice(text, reference.range), "value");
                    }
                    expect_diagnostic(
                        query.analysis.result->diagnostics(),
                        DiagnosticCode::NameUnresolved
                    );
                    expect_definition(
                        snapshot.definition(project, "untitled:source", outer_use),
                        "untitled:source",
                        1,
                        offset(text, "value: i32"),
                        "value"
                    );
                    expect_definition(
                        snapshot.definition(project, "untitled:source", inner_use),
                        "untitled:source",
                        1,
                        offset(text, "value: i64"),
                        "value"
                    );
                    expect(!snapshot.hover(project, "untitled:source", offset(text, "doomed: i32"))
                                .result);
                    expect(
                        !snapshot
                             .hover(project, "untitled:source", offset(text, "return doomed") + 7u)
                             .result
                    );
                    expect(
                        !snapshot.hover(project, "untitled:source", offset(text, "missing")).result
                    );
                    expect_equal(snapshot.counts().semantic, 1uz);
                }
            );
        };

    "Workspace analysis: invalid declaration headers suppress all source occurrences"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "unresolved signature type",
                    .source = "fn broken(value: Unknown) {}"
                },
                Scenario {
                    .name = "duplicate declaration",
                    .source = "fn duplicate() {} fn duplicate() {}"
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto host = WorkspaceAnalysisHost();
                const auto text = std::format(
                    "fn healthy(value: i32) -> i32 {{ let local: i32 = value; return local; }}\n{}",
                    scenario.source
                );
                update(host, "headers.cv", 1, text);
                const auto project = std::array {project_module("headers.cv", "main")};
                const auto snapshot = host.snapshot();
                const auto query =
                    snapshot.hover(project, "headers.cv", offset(text, "return local") + 7u);
                expect(!query.result);
                expect(query.analysis.result->program() == nullptr);
                expect(!query.analysis.result->diagnostics().empty());
                expect(!snapshot.hover(project, "headers.cv", offset(text, "value: i32")).result);
                expect(!snapshot
                            .definition(project, "headers.cv", offset(text, "return local") + 7u)
                            .result);
            });
        };

    "Workspace analysis: source occurrences preserve source expression boundaries and unknown types"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
                std::string_view selection;
                bool check_update_binding;
                bool check_definition;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "static template body",
                    .source =
                        "fn staged(const value: i32) -> i32 { let local: i32 = value; return local; }",
                    .selection = "local;",
                    .check_update_binding = false,
                    .check_definition = false
                },
                Scenario {
                    .name = "closure body",
                    .source =
                        "fn outer() { let callback = [](input: i32) -> i32 { let inner: i32 = input; return inner; }; }",
                    .selection = "inner;",
                    .check_update_binding = false,
                    .check_definition = false
                },
                Scenario {
                    .name = "nominal local",
                    .source =
                        "struct Pair { value: i32, } fn f() -> Pair { let local = Pair { value: 1 }; return local; }",
                    .selection = "local;",
                    .check_update_binding = false,
                    .check_definition = true
                },
                Scenario {
                    .name = "callable local",
                    .source =
                        "fn identity() -> i32 { return 1; } fn f() { let callback: fn() -> i32 = identity; callback(); }",
                    .selection = "callback();",
                    .check_update_binding = false,
                    .check_definition = true
                },
                Scenario {
                    .name = "update operator",
                    .source = "fn f() { var value: i32 = 1; ++value; }",
                    .selection = "++value;",
                    .check_update_binding = true,
                    .check_definition = false
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto host = WorkspaceAnalysisHost();
                const auto text =
                    std::format("{}\nfn broken() -> i32 {{ return missing; }}", scenario.source);
                update(host, "unsupported.cv", 1, text);
                const auto project = std::array {project_module("unsupported.cv", "main")};
                const auto snapshot = host.snapshot();
                const auto query =
                    snapshot.hover(project, "unsupported.cv", offset(text, scenario.selection));
                expect(query.analysis.result->program() == nullptr);
                expect_diagnostic(
                    query.analysis.result->diagnostics(),
                    DiagnosticCode::NameUnresolved
                );
                expect(!query.result);
                if (scenario.check_definition) {
                    const auto definition = snapshot.definition(
                        project,
                        "unsupported.cv",
                        offset(text, scenario.selection)
                    );
                    expect(definition.result.has_value());
                }
                if (scenario.check_update_binding) {
                    expect_type(
                        snapshot.hover(project, "unsupported.cv", offset(text, "value: i32")),
                        BuiltinType::I32,
                        "unsupported.cv",
                        "value"
                    );
                    expect_type(
                        snapshot.hover(project, "unsupported.cv", offset(text, "++value") + 2u),
                        BuiltinType::I32,
                        "unsupported.cv",
                        "value"
                    );
                }
            });
        };

    "Workspace analysis: source type anchors do not extend into unobserved source positions"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
                std::string_view known;
                BuiltinType type;
                std::string_view unknown;
            };
            constexpr auto closure = std::string_view(
                "fn f() { let callback: fn(i32) -> i32 = [](input: i32) -> i32 { let inner: i32 = input; return inner; }; let value: i32 = 1; }"
            );
            const auto scenarios = std::array {
                Scenario {
                    .name = "closure parameter",
                    .source = closure,
                    .known = "value: i32",
                    .type = BuiltinType::I32,
                    .unknown = "input: i32"
                },
                Scenario {
                    .name = "closure local use",
                    .source = closure,
                    .known = "value: i32",
                    .type = BuiltinType::I32,
                    .unknown = "inner;"
                },
                Scenario {
                    .name = "binding type annotation",
                    .source = closure,
                    .known = "value: i32",
                    .type = BuiltinType::I32,
                    .unknown = "i32 = 1"
                },
                Scenario {
                    .name = "callable type annotation",
                    .source = closure,
                    .known = "value: i32",
                    .type = BuiltinType::I32,
                    .unknown = "fn(i32)"
                },
                Scenario {
                    .name = "cast type operand",
                    .source = "fn f() -> i64 { return 1 as i64; }",
                    .known = "as i64",
                    .type = BuiltinType::I64,
                    .unknown = "i64;"
                },
                Scenario {
                    .name = "expression comment",
                    .source = "fn f() -> i32 { return 1 // unobserved\n + 2; }",
                    .known = "+ 2",
                    .type = BuiltinType::I32,
                    .unknown = "unobserved"
                },
                Scenario {
                    .name = "update statement operator",
                    .source = "fn f() { var value: i32 = 1; ++value; }",
                    .known = "value;",
                    .type = BuiltinType::I32,
                    .unknown = "++"
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto host = WorkspaceAnalysisHost();
                update(host, "anchors.cv", 1, scenario.source);
                const auto project = std::array {project_module("anchors.cv", "main")};
                const auto snapshot = host.snapshot();
                const auto known =
                    snapshot.hover(project, "anchors.cv", offset(scenario.source, scenario.known));
                require(known.analysis.result->program() != nullptr);
                require(known.result.has_value());
                const auto* type = std::get_if<TypeID>(&known.result->type);
                require(type != nullptr);
                expect(
                    *type == known.analysis.result->program()->types().builtin_type(scenario.type)
                );
                expect(!snapshot
                            .hover(project, "anchors.cv", offset(scenario.source, scenario.unknown))
                            .result);
            });
        };

    "Workspace analysis: failed publication retains construction evidence without published type IDs"_test =
        [] static noexcept {
            constexpr auto text = std::string_view(
                "struct Entry { value: i32, } fn bad() -> i32 { var entry = Entry {1}; let taken = &&entry; return entry.value; } fn healthy(value: i32) -> i32 { return value; }"
            );
            auto host = WorkspaceAnalysisHost();
            update(host, "ownership.cv", 1, text);
            const auto project = std::array {project_module("ownership.cv", "main")};
            const auto snapshot = host.snapshot();
            const auto use = offset(text, "return value") + 7u;
            const auto healthy = snapshot.hover(project, "ownership.cv", use);
            expect_type(healthy, BuiltinType::I32, "ownership.cv", "value");
            expect(healthy.analysis.result->program() == nullptr);
            expect_diagnostic(
                healthy.analysis.result->diagnostics(),
                DiagnosticCode::AccessUnavailable
            );
            expect_definition(
                snapshot.definition(project, "ownership.cv", use),
                "ownership.cv",
                1,
                offset(text, "value: i32", true),
                "value"
            );
            const auto nominal = offset(text, "entry.value");
            expect(!snapshot.hover(project, "ownership.cv", nominal).result);
            expect_definition(
                snapshot.definition(project, "ownership.cv", nominal),
                "ownership.cv",
                1,
                offset(text, "entry ="),
                "entry"
            );
            const auto references = snapshot.references(project, "ownership.cv", nominal);
            require(references.result.has_value());
            expect_equal(references.result->size(), 3uz);
        };

    "Workspace analysis: source queries retain snapshot types versions and binding locations"_test =
        [] static noexcept {
            const auto original_text = std::string_view(
                "fn f() -> i32 { let value: i32 = 1; return value; } fn broken() -> i32 { return missing; }"
            );
            const auto edited_text = std::string_view(
                "// shifted\nfn f() -> i64 { let value: i64 = 1; return value; } fn broken() -> i32 { return missing; }"
            );
            const auto retained = [&]() noexcept {
                auto host = WorkspaceAnalysisHost();
                const auto project =
                    std::array {project_module("file:///workspace/facts.cv", "main")};
                update(host, project[0].document, 1, original_text);
                const auto before = host.snapshot();
                const auto original = before.hover(
                    project,
                    project[0].document,
                    offset(original_text, "return value") + 7u
                );
                expect_type(original, BuiltinType::I32, project[0].document, "value");
                update(host, project[0].document, 2, original_text);
                const auto version_only = host.snapshot();
                const auto unchanged = version_only.hover(
                    project,
                    project[0].document,
                    offset(original_text, "return value") + 7u
                );
                expect(unchanged.analysis.result == original.analysis.result);
                expect_definition(
                    version_only.definition(
                        project,
                        project[0].document,
                        offset(original_text, "return value") + 7u
                    ),
                    project[0].document,
                    2,
                    offset(original_text, "value: i32"),
                    "value"
                );
                update(host, project[0].document, 3, edited_text);
                const auto after = host.snapshot();
                const auto edited = after.hover(
                    project,
                    project[0].document,
                    offset(edited_text, "return value") + 7u
                );
                expect_type(edited, BuiltinType::I64, project[0].document, "value");
                expect(edited.analysis.result != original.analysis.result);
                expect_definition(
                    after.definition(
                        project,
                        project[0].document,
                        offset(edited_text, "return value") + 7u
                    ),
                    project[0].document,
                    3,
                    offset(edited_text, "value: i64"),
                    "value"
                );
                expect_definition(
                    before.definition(
                        project,
                        project[0].document,
                        offset(original_text, "return value") + 7u
                    ),
                    project[0].document,
                    1,
                    offset(original_text, "value: i32"),
                    "value"
                );
                require(host.remove(project[0].document));
                const auto removed = host.snapshot().hover(
                    project,
                    project[0].document,
                    offset(edited_text, "return value") + 7u
                );
                expect(!removed.result);
                expect(removed.analysis.result->program() == nullptr);
                expect_type(
                    before.hover(
                        project,
                        project[0].document,
                        offset(original_text, "return value") + 7u
                    ),
                    BuiltinType::I32,
                    project[0].document,
                    "value"
                );
                return original;
            }();
            expect_type(retained, BuiltinType::I32, "file:///workspace/facts.cv", "value");
            const auto source = retained.analysis.result->source("file:///workspace/facts.cv");
            if (!expect(source.has_value())) {
                return;
            }
            expect_equal(source->text, original_text);
            expect_equal(retained.analysis.documents.size(), 1uz);
            expect_equal(retained.analysis.documents.front().version, 1ll);
        };

    "Workspace analysis: source queries share captured output without reexecution"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto text = std::string_view(
                "const { println(\"captured once\"); } fn f(value: i32) -> i32 { let local: i32 = value; return local; }"
            );
            const auto project = std::array {project_module("output.cv", "main")};
            update(host, "output.cv", 1, text);
            const auto before = host.snapshot();
            const auto query =
                before.hover(project, "output.cv", offset(text, "return local") + 7u);
            expect_type(query, BuiltinType::I32, "output.cv", "local");
            expect(query.analysis.result->program() != nullptr);
            const auto repeated = before.hover(project, "output.cv", offset(text, "value: i32"));
            expect(repeated.analysis.result == query.analysis.result);
            expect(before.semantic(project).result == query.analysis.result);
            update(host, "output.cv", 2, text);
            const auto after = host.snapshot();
            const auto unchanged =
                after.hover(project, "output.cv", offset(text, "return local") + 7u);
            expect(unchanged.analysis.result == query.analysis.result);
            auto output = std::string();
            for (const auto& chunk : unchanged.analysis.result->output()) {
                expect_equal(chunk.stream, ExecutionOutputStream::Standard);
                output += chunk.bytes;
            }
            expect_equal(output, "captured once\n");
            expect_equal(after.counts().semantic, 1uz);
        };
});

} // namespace
