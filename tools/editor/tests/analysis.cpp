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

namespace ct = carven::testing;

auto update(
    editor::AnalysisHost& host,
    std::string_view document,
    std::int64_t version,
    std::string_view text
) noexcept -> void {
    ct::require(host.update(std::string(document), version, std::string(text)).has_value());
}

const ct::Suite tests([] static noexcept {
    ct::test(
        "Editor analysis: queries lazily cache syntax and derived symbols",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "import missing using *; fn f() {}");
            const auto snapshot = host.snapshot();
            ct::expect_equal(snapshot.counts().syntax, 0uz);
            ct::expect(!snapshot.syntax("absent.cv"));
            ct::expect(!snapshot.document_symbols("absent.cv"));
            const auto parsed = snapshot.syntax("a.cv");
            if (!ct::expect(parsed.has_value())) {
                return;
            }
            ct::expect_equal(parsed->version, 1ll);
            ct::expect(parsed->result->syntax().has_value());
            ct::expect(parsed->result->diagnostics().empty());
            ct::expect_equal(snapshot.counts().syntax, 1uz);
            ct::expect_equal(snapshot.counts().document_symbols, 0uz);
            const auto repeated = snapshot.syntax("a.cv");
            if (!ct::expect(repeated.has_value())) {
                return;
            }
            ct::expect(repeated->result == parsed->result);
            const auto symbols = snapshot.document_symbols("a.cv");
            if (!ct::expect(symbols && symbols->result)) {
                return;
            }
            if (!ct::expect_equal(symbols->result->size(), 1uz)) {
                return;
            }
            ct::expect_equal(symbols->result->front().name, "f");
            const auto workspace = snapshot.workspace_symbols();
            ct::expect(snapshot.workspace_symbols() == workspace);
            ct::expect_equal(snapshot.counts().syntax, 1uz);
            ct::expect_equal(snapshot.counts().document_symbols, 1uz);
            ct::expect_equal(snapshot.counts().workspace_symbols, 1uz);
        }
    );

    ct::test(
        "Editor analysis: editing one document preserves unrelated computations",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn f() {}");
            update(host, "b.cv", 1, "fn g() {}");
            const auto before = host.snapshot();
            const auto old_a = before.syntax("a.cv");
            const auto old_b = before.syntax("b.cv");
            if (!ct::expect(old_a && old_b)) {
                return;
            }
            update(host, "a.cv", 2, "fn renamed() {}");
            const auto after = host.snapshot();
            ct::expect_equal(after.counts().syntax, 2uz);
            const auto new_b = after.syntax("b.cv");
            const auto new_a = after.syntax("a.cv");
            if (!ct::expect(new_a && new_b)) {
                return;
            }
            ct::expect(new_b->result == old_b->result);
            ct::expect(new_a->result != old_a->result);
            ct::expect_equal(new_a->version, 2ll);
            ct::expect_equal(new_a->result->source().text, "fn renamed() {}");
            ct::expect_equal(old_a->result->source().text, "fn f() {}");
            ct::expect_equal(after.counts().syntax, 3uz);
        }
    );

    ct::test(
        "Editor analysis: identical bytes advance the version without recomputation",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 10, "fn f() {}");
            const auto before = host.snapshot();
            const auto original = before.syntax("a.cv");
            const auto workspace = before.workspace_symbols();
            const auto changed = host.update("a.cv", 11, "fn f() {}");
            if (!ct::expect(changed.has_value())) {
                return;
            }
            ct::expect_equal(*changed, editor::DocumentChange::VersionOnly);
            const auto after = host.snapshot();
            const auto current = after.syntax("a.cv");
            if (!ct::expect(original && current)) {
                return;
            }
            ct::expect(current->result == original->result);
            ct::expect_equal(original->version, 10ll);
            ct::expect_equal(current->version, 11ll);
            ct::expect(after.workspace_symbols() == workspace);
            ct::expect_equal(after.counts().syntax, 1uz);
            ct::expect_equal(after.counts().document_symbols, 1uz);
            ct::expect_equal(after.counts().workspace_symbols, 1uz);
            const auto versions = std::to_array<std::int64_t>({11, 9});
            ct::each(
                versions,
                [](std::int64_t version) static noexcept {
                    return version == 11 ? "Equal version" : "Older version";
                },
                [&](std::int64_t version) noexcept {
                    const auto rejected = host.update("a.cv", version, "fn stale() {}");
                    if (!ct::expect(!rejected.has_value())) {
                        return;
                    }
                    const auto* error =
                        std::get_if<editor::StaleDocumentVersion>(&rejected.error());
                    if (!ct::expect(error != nullptr)) {
                        return;
                    }
                    ct::expect_equal(error->current, 11ll);
                    ct::expect_equal(error->received, version);
                    const auto retained = host.snapshot().syntax("a.cv");
                    if (!ct::expect(retained.has_value())) {
                        return;
                    }
                    ct::expect_equal(retained->version, 11ll);
                    ct::expect_equal(retained->result->source().text, "fn f() {}");
                }
            );
        }
    );

    ct::test(
        "Editor analysis: equal symbol results stop workspace invalidation",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn f() -> i32 { return 1; }");
            const auto before = host.snapshot();
            const auto original = before.document_symbols("a.cv");
            const auto workspace = before.workspace_symbols();
            if (!ct::expect(original && original->result)) {
                return;
            }
            update(host, "a.cv", 2, "fn f() -> i32 { return 2; }");
            const auto after = host.snapshot();
            const auto current = after.document_symbols("a.cv");
            if (!ct::expect(current && current->result)) {
                return;
            }
            ct::expect(current->document.result != original->document.result);
            ct::expect_equal(
                current->document.result->source().text,
                "fn f() -> i32 { return 2; }"
            );
            ct::expect(current->result == original->result);
            ct::expect(after.workspace_symbols() == workspace);
            ct::expect_equal(after.counts().syntax, 2uz);
            ct::expect_equal(after.counts().document_symbols, 2uz);
            ct::expect_equal(after.counts().workspace_symbols, 1uz);
        }
    );

    ct::test(
        "Editor analysis: symbol changes and shifted ranges invalidate workspace results",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn f() {}");
            auto previous = host.snapshot().workspace_symbols();
            const auto edits = std::to_array<std::pair<std::string_view, std::string_view>>({
                {"Rename", "fn g() {}"},
                {"Shift", "// comment\nfn g() {}"},
                {"Longer body", "// comment\nfn g() { let x = 1; }"},
            });
            auto version = 1ll;
            ct::each(
                edits,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    update(host, "a.cv", ++version, entry.second);
                    const auto snapshot = host.snapshot();
                    const auto current = snapshot.workspace_symbols();
                    if (!ct::expect_equal(current->size(), 1uz)) {
                        return;
                    }
                    ct::expect(current != previous);
                    ct::expect_equal(current->front().name, "g");
                    ct::expect_equal(slice(entry.second, current->front().selection), "g");
                    ct::expect_equal(current->front().range.end(), entry.second.size());
                    previous = current;
                }
            );
            ct::expect_equal(host.snapshot().counts().workspace_symbols, 4uz);
        }
    );

    ct::test(
        "Editor analysis: invalid files cache diagnostics without blocking independent files",
        [] static noexcept {
            const auto cases =
                std::to_array<std::tuple<std::string_view, std::string_view, DiagnosticCode>>({
                    {"Lexical error", "@", DiagnosticCode::Lexical},
                    {"Incomplete function", "fn broken(", DiagnosticCode::Syntax},
                });
            ct::each(
                cases,
                [](const auto& entry) static noexcept { return std::get<0>(entry); },
                [&](const auto& entry) noexcept {
                    auto host = editor::AnalysisHost();
                    update(host, "valid.cv", 1, "fn good() {}");
                    update(host, "broken.cv", 1, std::get<1>(entry));
                    const auto before = host.snapshot();
                    const auto broken = before.document_symbols("broken.cv");
                    if (!ct::expect(broken.has_value())) {
                        return;
                    }
                    ct::expect(!broken->result);
                    ct::expect(!broken->document.result->syntax());
                    const auto* diagnostic = ct::find_diagnostic(
                        broken->document.result->diagnostics(),
                        std::get<2>(entry)
                    );
                    if (!ct::expect(diagnostic != nullptr)) {
                        return;
                    }
                    ct::expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
                    if (!ct::expect(diagnostic->attachment.primary.has_value())) {
                        return;
                    }
                    ct::expect_equal(
                        broken->document.result->sources()
                            .view(diagnostic->attachment.primary->span.source_id)
                            .origin,
                        "broken.cv"
                    );
                    const auto workspace = before.workspace_symbols();
                    if (!ct::expect_equal(workspace->size(), 1uz)) {
                        return;
                    }
                    ct::expect_equal(workspace->front().name, "good");
                    const auto repeated = before.syntax("broken.cv");
                    if (!ct::expect(repeated.has_value())) {
                        return;
                    }
                    ct::expect(repeated->result == broken->document.result);
                    ct::expect_equal(before.counts().syntax, 2uz);
                    update(host, "broken.cv", 2, "fn repaired() {}");
                    const auto after = host.snapshot();
                    ct::expect_equal(after.workspace_symbols()->size(), 2uz);
                    const auto repaired = after.syntax("broken.cv");
                    if (!ct::expect(repaired.has_value())) {
                        return;
                    }
                    ct::expect(repaired->result->diagnostics().empty());
                    ct::expect_equal(broken->document.result->source().text, std::get<1>(entry));
                    ct::expect_equal(after.counts().syntax, 3uz);
                }
            );
        }
    );

    ct::test(
        "Editor analysis: removing and reopening documents preserves old snapshots",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 20, "fn old() {}");
            const auto before = host.snapshot();
            ct::expect(host.remove("a.cv"));
            ct::expect(!host.remove("a.cv"));
            const auto removed = host.snapshot();
            ct::expect(!removed.syntax("a.cv"));
            ct::expect(removed.workspace_symbols()->empty());
            update(host, "a.cv", 1, "fn fresh() {}");
            const auto old = before.document_symbols("a.cv");
            const auto fresh = host.snapshot().document_symbols("a.cv");
            if (!ct::expect(old && old->result && fresh && fresh->result)) {
                return;
            }
            ct::expect_equal(old->document.version, 20ll);
            ct::expect_equal(old->result->front().name, "old");
            ct::expect_equal(fresh->document.version, 1ll);
            ct::expect_equal(fresh->result->front().name, "fresh");
            ct::expect(!removed.syntax("a.cv"));
        }
    );

    ct::test(
        "Editor analysis: results own their sources after hosts and snapshots die",
        [] static noexcept {
            const auto snapshot = []() static noexcept {
                auto host = editor::AnalysisHost();
                update(host, "owned.cv", 1, "// source\nfn retained() {}");
                return host.snapshot();
            }();
            const auto retained = snapshot.document_symbols("owned.cv");
            if (!ct::expect(retained && retained->result)) {
                return;
            }
            const auto& symbol = retained->result->front();
            ct::expect_equal(
                slice(retained->document.result->source().text, symbol.selection),
                "retained"
            );
            const auto result = []() static noexcept {
                auto host = editor::AnalysisHost();
                update(host, "diagnostic.cv", 1, "\n@");
                return host.snapshot().syntax("diagnostic.cv");
            }();
            if (!ct::expect(result.has_value())) {
                return;
            }
            const auto* diagnostic =
                ct::find_diagnostic(result->result->diagnostics(), DiagnosticCode::Lexical);
            if (!ct::expect(diagnostic && diagnostic->attachment.primary)) {
                return;
            }
            const auto location =
                result->result->sources().location(diagnostic->attachment.primary->span);
            ct::expect_equal(location.line, 2u);
            ct::expect_equal(location.column, 1u);
        }
    );

    ct::test(
        "Editor analysis: caches release obsolete source trees without retained snapshots",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn f() {}");
            auto old_syntax = std::weak_ptr<const editor::DocumentSyntax>();
            {
                const auto snapshot = host.snapshot();
                const auto old = snapshot.syntax("a.cv");
                if (!ct::expect(old.has_value())) {
                    return;
                }
                old_syntax = old->result;
                static_cast<void>(snapshot.workspace_symbols());
            }
            update(host, "a.cv", 2, "fn g() {}");
            ct::expect(old_syntax.expired());
            update(host, "a.cv", 3, "fn h() {}");
            const auto current = host.snapshot().workspace_symbols();
            if (!ct::expect_equal(current->size(), 1uz)) {
                return;
            }
            ct::expect_equal(current->front().name, "h");
            ct::expect_equal(host.snapshot().counts().syntax, 2uz);
        }
    );

    ct::test(
        "Editor symbols: source declarations retain hierarchy and byte selections",
        [] static noexcept {
            auto host = editor::AnalysisHost();
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
            if (!ct::expect(query && query->result)) {
                return;
            }
            const auto& symbols = *query->result;
            if (!ct::expect_equal(symbols.size(), 5uz)) {
                return;
            }
            ct::expect_equal(symbols[0].kind, editor::SymbolKind::Structure);
            ct::expect_equal(symbols[1].kind, editor::SymbolKind::Enumeration);
            ct::expect_equal(symbols[2].kind, editor::SymbolKind::Class);
            ct::expect_equal(symbols[3].kind, editor::SymbolKind::Constant);
            ct::expect_equal(symbols[4].kind, editor::SymbolKind::Function);
            for (const auto& symbol : symbols) {
                ct::scenario(symbol.name, [&] noexcept {
                    ct::expect_equal(slice(text, symbol.selection), symbol.name);
                    ct::expect(symbol.range.start() <= symbol.selection.start());
                    ct::expect(symbol.selection.end() <= symbol.range.end());
                    for (const auto& child : symbol.children) {
                        ct::scenario(child.name, [&] noexcept {
                            ct::expect_equal(slice(text, child.selection), child.name);
                            ct::expect(symbol.range.start() <= child.range.start());
                            ct::expect(child.range.end() <= symbol.range.end());
                        });
                    }
                });
            }
            if (!ct::expect_equal(symbols[0].children.size(), 2uz)
                || !ct::expect_equal(symbols[1].children.size(), 2uz)
                || !ct::expect_equal(symbols[2].children.size(), 2uz)) {
                return;
            }
            ct::expect_equal(symbols[0].children[0].kind, editor::SymbolKind::Field);
            ct::expect_equal(symbols[1].children[0].kind, editor::SymbolKind::EnumCase);
            ct::expect_equal(symbols[2].children[1].name, "read");
            ct::expect_equal(symbols[2].children[1].kind, editor::SymbolKind::Function);
        }
    );

    ct::test(
        "Editor analysis: empty syntax differs from missing and unavailable documents",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "empty.cv", 1, "// empty\n");
            const auto snapshot = host.snapshot();
            const auto query = snapshot.document_symbols("empty.cv");
            if (!ct::expect(query && query->result)) {
                return;
            }
            ct::expect(query->result->empty());
            ct::expect(query->document.result->syntax().has_value());
            ct::expect(!snapshot.document_symbols("absent.cv"));
            ct::expect(snapshot.workspace_symbols()->empty());
        }
    );
});

} // namespace
