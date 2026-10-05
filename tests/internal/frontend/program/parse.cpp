module carven:test.internal.frontend.program.parse;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :frontend.program.verify;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    require(result.has_value());
    return std::move(*result);
}

const TestSuite suite([] static noexcept {
    "Syntax program: real module sources publish through the program parser"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = sources.append_virtual("main.cv", "const answer: i32 = 42;\n");
            if (!expect(source.has_value())) {
                return;
            }

            const auto inputs = std::array {SourceModuleInput {
                .source_id = *source,
                .module_path = path("app.main"),
            }};
            auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
            if (!expect(parsed.has_value())) {
                return;
            }
            if (!expect(verify_syntax_program(*parsed).has_value())) {
                return;
            }
            expect_equal(parsed->syntax_trees().size(), 1uz);
            expect_equal(parsed->provenance().module_records().size(), 1uz);
            expect_equal(parsed->provenance().source_snapshots().size(), 1uz);
            expect_equal(
                parsed->provenance()
                    .module_record(parsed->provenance().module_id_at(0))
                    .path.value(),
                std::string_view("app.main")
            );
        };

    "Syntax program: closed compilation rejects an empty input batch"_test = [] static noexcept {
        const auto sources = SourceManager();
        const auto parsed =
            parse_program(sources, SourceBatch {.modules = std::span<const SourceModuleInput>()});
        if (!expect(!(parsed.has_value()))) {
            return;
        }
        if (!expect_equal(parsed.error().size(), 1uz)) {
            return;
        }
        expect_equal(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
        expect_equal(parsed.error().front().finding.severity, DiagnosticSeverity::Error);
        expect(!(parsed.error().front().attachment.primary.has_value()));
    };

    "Syntax program: closed compilation rejects duplicate source snapshots"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source = sources.append_virtual("main.cv", "fn main() {}\n");
            if (!expect(source.has_value())) {
                return;
            }
            const auto inputs = std::array {
                SourceModuleInput {
                    .source_id = *source,
                    .module_path = path("app.first"),
                },
                SourceModuleInput {
                    .source_id = *source,
                    .module_path = path("app.second"),
                },
                SourceModuleInput {
                    .source_id = *source,
                    .module_path = path("app.third"),
                },
            };
            const auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
            if (!expect(!(parsed.has_value()))) {
                return;
            }
            if (!expect_equal(parsed.error().size(), 2uz)) {
                return;
            }
            expect_equal(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
        };

    "Syntax program: closed compilation rejects duplicate module paths"_test = [] static noexcept {
        auto sources = SourceManager();
        const auto first = sources.append_virtual("first.cv", "fn first() {}\n");
        const auto second = sources.append_virtual("second.cv", "fn second() {}\n");
        if (!expect(first.has_value())) {
            return;
        }
        if (!expect(second.has_value())) {
            return;
        }
        const auto inputs = std::array {
            SourceModuleInput {
                .source_id = *first,
                .module_path = path("app.same"),
            },
            SourceModuleInput {
                .source_id = *second,
                .module_path = path("app.same"),
            },
        };
        const auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
        if (!expect(!(parsed.has_value()))) {
            return;
        }
        if (!expect_equal(parsed.error().size(), 1uz)) {
            return;
        }
        expect_equal(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
    };

    "Syntax program: closed compilation rejects a missing source snapshot"_test =
        [] static noexcept {
            const auto sources = SourceManager();
            const auto inputs = std::array {SourceModuleInput {
                .source_id = SourceID::from_index(0),
                .module_path = path("app.missing"),
            }};
            const auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
            if (!expect(!(parsed.has_value()))) {
                return;
            }
            if (!expect_equal(parsed.error().size(), 1uz)) {
                return;
            }
            expect_equal(parsed.error().front().finding.code, DiagnosticCode::CompilationInput);
        };

    "Syntax program: imports select official, craft root, and module directory sources"_test =
        [] static noexcept {
            struct Case final {
                std::string_view importer;
                std::string_view craft_root_target;
                std::string_view relative_target;
            };

            const auto cases = std::array {
                Case {"nested.main", "std.utf", "nested.std.utf"},
                Case {"crafts.app.nested.main", "crafts.app.std.utf", "crafts.app.nested.std.utf"},
                Case {
                    "crafts.carven.nested.main",
                    "crafts.carven.std.utf",
                    "crafts.carven.nested.std.utf",
                },
            };
            each(
                cases,
                [](const Case& item) static noexcept -> std::string_view { return item.importer; },
                [](const Case& item) static noexcept {
                    auto sources = SourceManager();
                    auto inputs = std::vector<SourceModuleInput>();
                    const auto append = [&](std::string_view name, std::string_view text) noexcept {
                        const auto source =
                            sources.append_virtual(std::string(name), std::string(text));
                        require(source.has_value()).note("item.importer = ", item.importer);
                        inputs.push_back({.source_id = *source, .module_path = path(name)});
                    };
                    append(
                        item.importer,
                        "import std::utf using *; import std.utf using *; "
                        "import .std.utf using *; import json::parser using *;"
                    );
                    append("crafts.carven.std.utf", "");
                    append("crafts.std.utf", "");
                    append("crafts.json.parser", "");
                    if (item.craft_root_target != "crafts.carven.std.utf") {
                        append(item.craft_root_target, "");
                    }
                    append(item.relative_target, "");

                    const auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
                    if (!(expect(parsed.has_value()).note("item.importer = ", item.importer))) {
                        return;
                    }
                    if (!(expect(verify_syntax_program(*parsed).has_value())
                              .note("item.importer = ", item.importer))) {
                        return;
                    }
                    const auto provenance = parsed->provenance();
                    const auto importer = provenance.find_program_module(path(item.importer));
                    if (!(expect(importer.has_value()).note("item.importer = ", item.importer))) {
                        return;
                    }
                    const auto imports = parsed->resolved_imports(*importer);
                    const auto expected = std::array {
                        std::string_view("crafts.carven.std.utf"),
                        item.craft_root_target,
                        item.relative_target,
                        std::string_view("crafts.json.parser"),
                    };
                    if (!(expect_equal(imports.size(), expected.size())
                              .note("item.importer = ", item.importer))) {
                        return;
                    }
                    for (auto index = 0uz; index < expected.size(); ++index) {
                        expect(((provenance.module_record(imports[index].target).path.value())
                                == (expected[index])))
                            .note(
                                "provenance.module_record(imports[index].target).path.value() == expected[index]",
                                "item.importer = ",
                                item.importer
                            );
                    }
                }
            );
        };

    "Syntax program: a standard import requires the official source in the input batch"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto inputs = std::vector<SourceModuleInput>();
            for (const auto name : {"nested.main", "crafts.std.utf", "std.utf", "nested.std.utf"}) {
                const auto source = sources.append_virtual(
                    name,
                    std::string_view(name) == "nested.main" ? "import std::utf using *;" : ""
                );
                if (!expect(source.has_value())) {
                    return;
                }
                inputs.push_back({.source_id = *source, .module_path = path(name)});
            }
            const auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
            if (!expect(!(parsed.has_value()))) {
                return;
            }
            if (!expect_equal(parsed.error().size(), 1uz)) {
                return;
            }
            expect_equal(parsed.error().front().finding.code, DiagnosticCode::ImportResolution);
            expect(parsed.error().front().attachment.primary.has_value());
        };
});

} // namespace
