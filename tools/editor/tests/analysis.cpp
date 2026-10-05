module carven:test.editor.analysis;

import :diagnostics.code;
import :editor.analysis;
import :editor.document;
import :editor.symbols;
import :source.manager;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

auto update(
    EditorAnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    require(host.update(std::string(document), version, std::string(text)).has_value());
}

const TestSuite tests([] static noexcept {
    "Editor analysis: queries lazily cache syntax and derived symbols"_test = [] static noexcept {
        auto host = EditorAnalysisHost();
        update(host, "a.cv", 1, "import missing using *; fn f() {}");
        const auto snapshot = host.snapshot();
        expect_equal(snapshot.counts().syntax, 0uz);
        expect(!snapshot.syntax("absent.cv"));
        expect(!snapshot.document_symbols("absent.cv"));
        const auto parsed = snapshot.syntax("a.cv");
        if (!expect(parsed.has_value())) {
            return;
        }
        expect_equal(parsed->version, 1ll);
        expect(parsed->result->syntax().has_value());
        expect(parsed->result->diagnostics().empty());
        expect_equal(snapshot.counts().syntax, 1uz);
        expect_equal(snapshot.counts().document_symbols, 0uz);
        const auto repeated = snapshot.syntax("a.cv");
        if (!expect(repeated.has_value())) {
            return;
        }
        expect(repeated->result == parsed->result);
        const auto symbols = snapshot.document_symbols("a.cv");
        if (!expect(symbols && symbols->result)) {
            return;
        }
        if (!expect_equal(symbols->result->size(), 1uz)) {
            return;
        }
        expect_equal(symbols->result->front().name, "f");
        const auto workspace = snapshot.workspace_symbols();
        expect(snapshot.workspace_symbols() == workspace);
        expect_equal(snapshot.counts().syntax, 1uz);
        expect_equal(snapshot.counts().document_symbols, 1uz);
        expect_equal(snapshot.counts().workspace_symbols, 1uz);
    };

    "Editor analysis: editing one document preserves unrelated computations"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "a.cv", 1, "fn f() {}");
            update(host, "b.cv", 1, "fn g() {}");
            const auto before = host.snapshot();
            const auto old_a = before.syntax("a.cv");
            const auto old_b = before.syntax("b.cv");
            if (!expect(old_a && old_b)) {
                return;
            }
            update(host, "a.cv", 2, "fn renamed() {}");
            const auto after = host.snapshot();
            expect_equal(after.counts().syntax, 2uz);
            const auto new_b = after.syntax("b.cv");
            const auto new_a = after.syntax("a.cv");
            if (!expect(new_a && new_b)) {
                return;
            }
            expect(new_b->result == old_b->result);
            expect(new_a->result != old_a->result);
            expect_equal(new_a->version, 2ll);
            expect_equal(new_a->result->source().text, "fn renamed() {}");
            expect_equal(old_a->result->source().text, "fn f() {}");
            expect_equal(after.counts().syntax, 3uz);
        };

    "Editor analysis: identical bytes advance the version without recomputation"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "a.cv", 10, "fn f() {}");
            const auto before = host.snapshot();
            const auto original = before.syntax("a.cv");
            const auto workspace = before.workspace_symbols();
            const auto changed = host.update("a.cv", 11, "fn f() {}");
            if (!expect(changed.has_value())) {
                return;
            }
            expect_equal(*changed, EditorDocumentChange::VersionOnly);
            const auto after = host.snapshot();
            const auto current = after.syntax("a.cv");
            if (!expect(original && current)) {
                return;
            }
            expect(current->result == original->result);
            expect_equal(original->version, 10ll);
            expect_equal(current->version, 11ll);
            expect(after.workspace_symbols() == workspace);
            expect_equal(after.counts().syntax, 1uz);
            expect_equal(after.counts().document_symbols, 1uz);
            expect_equal(after.counts().workspace_symbols, 1uz);
            const auto versions = std::to_array<std::int64_t>({11, 9});
            each(
                versions,
                [](std::int64_t version) static noexcept {
                    return version == 11 ? "Equal version" : "Older version";
                },
                [&](std::int64_t version) noexcept {
                    const auto rejected = host.update("a.cv", version, "fn stale() {}");
                    if (!expect(!rejected.has_value())) {
                        return;
                    }
                    const auto* error = std::get_if<EditorStaleDocumentVersion>(&rejected.error());
                    if (!expect(error != nullptr)) {
                        return;
                    }
                    expect_equal(error->current, 11ll);
                    expect_equal(error->received, version);
                    const auto retained = host.snapshot().syntax("a.cv");
                    if (!expect(retained.has_value())) {
                        return;
                    }
                    expect_equal(retained->version, 11ll);
                    expect_equal(retained->result->source().text, "fn f() {}");
                }
            );
        };

    "Editor analysis: equal symbol results stop workspace invalidation"_test = [] static noexcept {
        auto host = EditorAnalysisHost();
        update(host, "a.cv", 1, "fn f() -> i32 { return 1; }");
        const auto before = host.snapshot();
        const auto original = before.document_symbols("a.cv");
        const auto workspace = before.workspace_symbols();
        if (!expect(original && original->result)) {
            return;
        }
        update(host, "a.cv", 2, "fn f() -> i32 { return 2; }");
        const auto after = host.snapshot();
        const auto current = after.document_symbols("a.cv");
        if (!expect(current && current->result)) {
            return;
        }
        expect(current->document.result != original->document.result);
        expect_equal(current->document.result->source().text, "fn f() -> i32 { return 2; }");
        expect(current->result == original->result);
        expect(after.workspace_symbols() == workspace);
        expect_equal(after.counts().syntax, 2uz);
        expect_equal(after.counts().document_symbols, 2uz);
        expect_equal(after.counts().workspace_symbols, 1uz);
    };

    "Editor analysis: symbol changes and shifted ranges invalidate workspace results"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "a.cv", 1, "fn f() {}");
            auto previous = host.snapshot().workspace_symbols();
            const auto edits = std::to_array<std::pair<std::string_view, std::string_view>>({
                {"Rename", "fn g() {}"},
                {"Shift", "// comment\nfn g() {}"},
                {"Longer body", "// comment\nfn g() { let x = 1; }"},
            });
            auto version = 1ll;
            each(
                edits,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    update(host, "a.cv", ++version, entry.second);
                    const auto snapshot = host.snapshot();
                    const auto current = snapshot.workspace_symbols();
                    if (!expect_equal(current->size(), 1uz)) {
                        return;
                    }
                    expect(current != previous);
                    expect_equal(current->front().name, "g");
                    expect_equal(slice(entry.second, current->front().selection), "g");
                    expect_equal(current->front().range.end(), entry.second.size());
                    previous = current;
                }
            );
            expect_equal(host.snapshot().counts().workspace_symbols, 4uz);
        };

    "Editor analysis: invalid files cache diagnostics without blocking independent files"_test =
        [] static noexcept {
            const auto cases =
                std::to_array<std::tuple<std::string_view, std::string_view, DiagnosticCode>>({
                    {"Lexical error", "@", DiagnosticCode::Lexical},
                    {"Incomplete function", "fn broken(", DiagnosticCode::Syntax},
                });
            each(
                cases,
                [](const auto& entry) static noexcept { return std::get<0>(entry); },
                [&](const auto& entry) noexcept {
                    auto host = EditorAnalysisHost();
                    update(host, "valid.cv", 1, "fn good() {}");
                    update(host, "broken.cv", 1, std::get<1>(entry));
                    const auto before = host.snapshot();
                    const auto broken = before.document_symbols("broken.cv");
                    if (!expect(broken.has_value())) {
                        return;
                    }
                    expect(!broken->result);
                    expect(!broken->document.result->syntax());
                    const auto* diagnostic =
                        find_diagnostic(broken->document.result->diagnostics(), std::get<2>(entry));
                    if (!expect(diagnostic != nullptr)) {
                        return;
                    }
                    expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
                    if (!expect(diagnostic->attachment.primary.has_value())) {
                        return;
                    }
                    expect_equal(
                        broken->document.result->sources()
                            .view(diagnostic->attachment.primary->span.source_id)
                            .origin,
                        "broken.cv"
                    );
                    const auto workspace = before.workspace_symbols();
                    if (!expect_equal(workspace->size(), 1uz)) {
                        return;
                    }
                    expect_equal(workspace->front().name, "good");
                    const auto repeated = before.syntax("broken.cv");
                    if (!expect(repeated.has_value())) {
                        return;
                    }
                    expect(repeated->result == broken->document.result);
                    expect_equal(before.counts().syntax, 2uz);
                    update(host, "broken.cv", 2, "fn repaired() {}");
                    const auto after = host.snapshot();
                    expect_equal(after.workspace_symbols()->size(), 2uz);
                    const auto repaired = after.syntax("broken.cv");
                    if (!expect(repaired.has_value())) {
                        return;
                    }
                    expect(repaired->result->diagnostics().empty());
                    expect_equal(broken->document.result->source().text, std::get<1>(entry));
                    expect_equal(after.counts().syntax, 3uz);
                }
            );
        };

    "Editor analysis: removing and reopening documents preserves old snapshots"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "a.cv", 20, "fn old() {}");
            const auto before = host.snapshot();
            expect(host.remove("a.cv"));
            expect(!host.remove("a.cv"));
            const auto removed = host.snapshot();
            expect(!removed.syntax("a.cv"));
            expect(removed.workspace_symbols()->empty());
            update(host, "a.cv", 1, "fn fresh() {}");
            const auto old = before.document_symbols("a.cv");
            const auto fresh = host.snapshot().document_symbols("a.cv");
            if (!expect(old && old->result && fresh && fresh->result)) {
                return;
            }
            expect_equal(old->document.version, 20ll);
            expect_equal(old->result->front().name, "old");
            expect_equal(fresh->document.version, 1ll);
            expect_equal(fresh->result->front().name, "fresh");
            expect(!removed.syntax("a.cv"));
        };

    "Editor analysis: results own their sources after hosts and snapshots die"_test =
        [] static noexcept {
            const auto snapshot = []() static noexcept {
                auto host = EditorAnalysisHost();
                update(host, "owned.cv", 1, "// source\nfn retained() {}");
                return host.snapshot();
            }();
            const auto retained = snapshot.document_symbols("owned.cv");
            if (!expect(retained && retained->result)) {
                return;
            }
            const auto& symbol = retained->result->front();
            expect_equal(
                slice(retained->document.result->source().text, symbol.selection),
                "retained"
            );
            const auto result = []() static noexcept {
                auto host = EditorAnalysisHost();
                update(host, "diagnostic.cv", 1, "\n@");
                return host.snapshot().syntax("diagnostic.cv");
            }();
            if (!expect(result.has_value())) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(result->result->diagnostics(), DiagnosticCode::Lexical);
            if (!expect(diagnostic && diagnostic->attachment.primary)) {
                return;
            }
            const auto location =
                result->result->sources().location(diagnostic->attachment.primary->span);
            expect_equal(location.line, 2u);
            expect_equal(location.column, 1u);
        };

    "Editor analysis: caches release obsolete source trees without retained snapshots"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "a.cv", 1, "fn f() {}");
            auto old_syntax = std::weak_ptr<const EditorDocumentSyntax>();
            {
                const auto snapshot = host.snapshot();
                const auto old = snapshot.syntax("a.cv");
                if (!expect(old.has_value())) {
                    return;
                }
                old_syntax = old->result;
                static_cast<void>(snapshot.workspace_symbols());
            }
            update(host, "a.cv", 2, "fn g() {}");
            expect(old_syntax.expired());
            update(host, "a.cv", 3, "fn h() {}");
            const auto current = host.snapshot().workspace_symbols();
            if (!expect_equal(current->size(), 1uz)) {
                return;
            }
            expect_equal(current->front().name, "h");
            expect_equal(host.snapshot().counts().syntax, 2uz);
        };

    "Editor symbols: source declarations retain hierarchy and byte selections"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            const auto text = std::string_view(
                "// 中文\r\n"
                "struct Pair { left: i32, right: i32, }\r\n"
                "enum Choice { Yes(i32), No, }\r\n"
                "class Counter { value: i32, fn read(self) -> i32 => self.value; }\r\n"
                "const answer: i32 = 42;\r\n"
                "fn f() {}\r\n"
                "const { let hidden = 1; }\r\n"
                "test { }\r\n"
            );
            update(host, "symbols.cv", 1, text);
            const auto query = host.snapshot().document_symbols("symbols.cv");
            if (!expect(query && query->result)) {
                return;
            }
            const auto& symbols = *query->result;
            if (!expect_equal(symbols.size(), 5uz)) {
                return;
            }
            expect_equal(symbols[0].kind, EditorSymbolKind::Structure);
            expect_equal(symbols[1].kind, EditorSymbolKind::Enumeration);
            expect_equal(symbols[2].kind, EditorSymbolKind::Class);
            expect_equal(symbols[3].kind, EditorSymbolKind::Constant);
            expect_equal(symbols[4].kind, EditorSymbolKind::Function);
            for (const auto& symbol : symbols) {
                scenario(symbol.name, [&] noexcept {
                    expect_equal(slice(text, symbol.selection), symbol.name);
                    expect(symbol.range.start() <= symbol.selection.start());
                    expect(symbol.selection.end() <= symbol.range.end());
                    for (const auto& child : symbol.children) {
                        scenario(child.name, [&] noexcept {
                            expect_equal(slice(text, child.selection), child.name);
                            expect(symbol.range.start() <= child.range.start());
                            expect(child.range.end() <= symbol.range.end());
                        });
                    }
                });
            }
            if (!expect_equal(symbols[0].children.size(), 2uz)
                || !expect_equal(symbols[1].children.size(), 2uz)
                || !expect_equal(symbols[2].children.size(), 2uz)) {
                return;
            }
            expect_equal(symbols[0].children[0].kind, EditorSymbolKind::Field);
            expect_equal(symbols[1].children[0].kind, EditorSymbolKind::EnumCase);
            expect_equal(symbols[2].children[1].name, "read");
            expect_equal(symbols[2].children[1].kind, EditorSymbolKind::Function);
        };

    "Editor analysis: empty syntax differs from missing and unavailable documents"_test =
        [] static noexcept {
            auto host = EditorAnalysisHost();
            update(host, "empty.cv", 1, "// empty\n");
            const auto snapshot = host.snapshot();
            const auto query = snapshot.document_symbols("empty.cv");
            if (!expect(query && query->result)) {
                return;
            }
            expect(query->result->empty());
            expect(query->document.result->syntax().has_value());
            expect(!snapshot.document_symbols("absent.cv"));
            expect(snapshot.workspace_symbols()->empty());
        };
});

} // namespace
