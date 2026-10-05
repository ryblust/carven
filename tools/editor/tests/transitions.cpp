module carven:test.editor.transitions;

import :editor.analysis;
import :editor.document;
import :editor.symbols;
import :source.text;
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
        "Editor analysis: unqueried snapshots retain inputs across multiple edits",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn original() {}");
            update(host, "b.cv", 1, "fn independent() {}");
            const auto original = host.snapshot();
            update(host, "a.cv", 2, "fn intermediate() {}");
            const auto intermediate = host.snapshot();
            update(host, "a.cv", 3, "// shifted\nfn current() {}");
            const auto current = host.snapshot();
            ct::expect_equal(current.counts().syntax, 0uz);
            const auto old_symbols = original.workspace_symbols();
            const auto middle_symbols = intermediate.workspace_symbols();
            const auto new_symbols = current.workspace_symbols();
            if (!ct::expect_equal(old_symbols->size(), 2uz)
                || !ct::expect_equal(middle_symbols->size(), 2uz)
                || !ct::expect_equal(new_symbols->size(), 2uz)) {
                return;
            }
            ct::expect_equal((*old_symbols)[0].name, "original");
            ct::expect_equal((*middle_symbols)[0].name, "intermediate");
            ct::expect_equal((*new_symbols)[0].name, "current");
            const auto old_a = original.syntax("a.cv");
            const auto middle_a = intermediate.syntax("a.cv");
            const auto new_a = current.syntax("a.cv");
            if (!ct::expect(old_a && middle_a && new_a)) {
                return;
            }
            ct::expect_equal(old_a->version, 1ll);
            ct::expect_equal(middle_a->version, 2ll);
            ct::expect_equal(new_a->version, 3ll);
            ct::expect_equal(
                slice(old_a->result->source().text, (*old_symbols)[0].selection),
                "original"
            );
            ct::expect_equal(
                slice(middle_a->result->source().text, (*middle_symbols)[0].selection),
                "intermediate"
            );
            ct::expect_equal(
                slice(new_a->result->source().text, (*new_symbols)[0].selection),
                "current"
            );
            ct::expect_equal(current.counts().syntax, 4uz);
            ct::expect_equal(current.counts().document_symbols, 4uz);
            ct::expect_equal(current.counts().workspace_symbols, 3uz);
        }
    );

    ct::test(
        "Editor analysis: changed invalid syntax preserves an unchanged workspace index",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "broken.cv", 1, "@");
            update(host, "valid.cv", 1, "fn good() {}");
            const auto before = host.snapshot();
            const auto workspace = before.workspace_symbols();
            const auto old_broken = before.document_symbols("broken.cv");
            if (!ct::expect(old_broken && !old_broken->result)) {
                return;
            }
            update(host, "broken.cv", 2, "fn incomplete(");
            const auto after = host.snapshot();
            const auto new_broken = after.document_symbols("broken.cv");
            if (!ct::expect(new_broken && !new_broken->result)) {
                return;
            }
            ct::expect(new_broken->document.result != old_broken->document.result);
            ct::expect_equal(new_broken->document.version, 2ll);
            ct::expect(!new_broken->document.result->diagnostics().empty());
            ct::expect_equal(old_broken->document.result->source().text, "@");
            ct::expect_equal(new_broken->document.result->source().text, "fn incomplete(");
            ct::expect(after.workspace_symbols() == workspace);
            if (!ct::expect_equal(workspace->size(), 1uz)) {
                return;
            }
            ct::expect_equal(workspace->front().name, "good");
            ct::expect_equal(after.counts().syntax, 3uz);
            ct::expect_equal(after.counts().document_symbols, 1uz);
            ct::expect_equal(after.counts().workspace_symbols, 1uz);
        }
    );

    ct::test(
        "Editor analysis: broken syntax removes symbols before a later repair",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 1, "fn old() {}");
            const auto before = host.snapshot();
            const auto original = before.workspace_symbols();
            update(host, "a.cv", 2, "fn broken(");
            const auto broken = host.snapshot();
            const auto unavailable = broken.document_symbols("a.cv");
            if (!ct::expect(unavailable && !unavailable->result)) {
                return;
            }
            const auto empty = broken.workspace_symbols();
            ct::expect(empty->empty());
            ct::expect(empty != original);
            update(host, "a.cv", 3, "fn repaired() {}");
            const auto after = host.snapshot();
            const auto repaired = after.workspace_symbols();
            if (!ct::expect_equal(original->size(), 1uz)
                || !ct::expect_equal(repaired->size(), 1uz)) {
                return;
            }
            ct::expect_equal(original->front().name, "old");
            ct::expect_equal(repaired->front().name, "repaired");
            ct::expect(repaired != original);
            ct::expect(repaired != empty);
            ct::expect(before.workspace_symbols() == original);
            ct::expect(broken.workspace_symbols() == empty);
            ct::expect_equal(after.counts().syntax, 3uz);
            ct::expect_equal(after.counts().document_symbols, 2uz);
            ct::expect_equal(after.counts().workspace_symbols, 3uz);
        }
    );

    ct::test(
        "Editor analysis: reopened documents leave retained workspace locations unchanged",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "a.cv", 20, "// old location\nfn old() {}");
            const auto before = host.snapshot();
            const auto old_workspace = before.workspace_symbols();
            ct::require(host.remove("a.cv"));
            const auto removed = host.snapshot();
            update(host, "a.cv", 1, "fn fresh() {}");
            const auto after = host.snapshot();
            const auto new_workspace = after.workspace_symbols();
            const auto old_syntax = before.syntax("a.cv");
            const auto new_syntax = after.syntax("a.cv");
            if (!ct::expect(old_syntax && new_syntax)
                || !ct::expect_equal(old_workspace->size(), 1uz)
                || !ct::expect_equal(new_workspace->size(), 1uz)) {
                return;
            }
            ct::expect_equal(old_syntax->version, 20ll);
            ct::expect_equal(new_syntax->version, 1ll);
            ct::expect_equal(old_workspace->front().document, "a.cv");
            ct::expect_equal(new_workspace->front().document, "a.cv");
            ct::expect_equal(
                slice(old_syntax->result->source().text, old_workspace->front().selection),
                "old"
            );
            ct::expect_equal(
                slice(new_syntax->result->source().text, new_workspace->front().selection),
                "fresh"
            );
            ct::expect(old_workspace->front().selection != new_workspace->front().selection);
            ct::expect(before.workspace_symbols() == old_workspace);
            ct::expect(removed.workspace_symbols()->empty());
            ct::expect(!removed.syntax("a.cv"));
            ct::expect_equal(after.counts().syntax, 2uz);
            ct::expect_equal(after.counts().document_symbols, 2uz);
            ct::expect_equal(after.counts().workspace_symbols, 3uz);
        }
    );

    ct::test(
        "Editor analysis: document membership changes preserve unrelated computations",
        [] static noexcept {
            auto host = editor::AnalysisHost();
            update(host, "b.cv", 1, "fn retained() {}");
            const auto initial = host.snapshot();
            const auto b = initial.document_symbols("b.cv");
            const auto initial_index = initial.workspace_symbols();
            if (!ct::expect(b && b->result)) {
                return;
            }
            update(host, "a.cv", 1, "fn added() {}");
            const auto added = host.snapshot();
            const auto added_index = added.workspace_symbols();
            if (!ct::expect_equal(added_index->size(), 2uz)) {
                return;
            }
            ct::expect_equal((*added_index)[0].document, "a.cv");
            ct::expect_equal((*added_index)[1].document, "b.cv");
            const auto retained_after_add = added.document_symbols("b.cv");
            if (!ct::expect(retained_after_add && retained_after_add->result)) {
                return;
            }
            ct::expect(retained_after_add->document.result == b->document.result);
            ct::expect(retained_after_add->result == b->result);
            ct::require(host.remove("a.cv"));
            const auto removed = host.snapshot();
            const auto removed_index = removed.workspace_symbols();
            if (!ct::expect_equal(removed_index->size(), 1uz)) {
                return;
            }
            ct::expect_equal(removed_index->front().document, "b.cv");
            ct::expect_equal(removed_index->front().name, "retained");
            ct::expect(!removed.syntax("a.cv"));
            ct::expect_equal(initial_index->size(), 1uz);
            ct::expect_equal(added.workspace_symbols()->size(), 2uz);
            ct::expect_equal(removed.counts().syntax, 2uz);
            ct::expect_equal(removed.counts().document_symbols, 2uz);
            ct::expect_equal(removed.counts().workspace_symbols, 3uz);
        }
    );
});

} // namespace
