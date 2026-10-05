module carven:test.analyzer.session;

import :analyzer.session;
import :editor.analysis;
import :source.text;
import :test.harness.framework;
import std;

namespace {

auto update(
    AnalyzerSession& session,
    std::string document,
    std::int64_t version,
    std::string text
) noexcept -> void {
    const auto response = session.execute(
        AnalyzerUpdate {
            .document = std::move(document),
            .version = version,
            .text = std::move(text)
        }
    );
    require(std::holds_alternative<AnalyzerAcknowledgement>(response.result));
    expect(response.document_versions.empty());
}

auto select_project(AnalyzerSession& session) noexcept -> void {
    const auto response = session.execute(
        AnalyzerReplaceProject {
            .modules = {
                {.document = "lib", .module_path = "lib"},
                {.document = "app", .module_path = "app"}
            }
        }
    );
    require(std::holds_alternative<AnalyzerAcknowledgement>(response.result));
    expect(response.document_versions.empty());
}

auto hover(AnalyzerSession& session, std::string document, std::uint32_t offset) noexcept
    -> AnalyzerResponse {
    return session.execute(AnalyzerHover {.document = std::move(document), .offset = offset});
}

auto expect_hover_text(const AnalyzerResponse& response, std::string_view text) noexcept -> void {
    const auto* result = std::get_if<AnalyzerHoverResult>(&response.result);
    if (!expect(result != nullptr && result->information.has_value())) {
        return;
    }
    expect_equal(result->information->type_text, text);
}

const TestSuite tests([] static noexcept {
    "Analyzer session: responses own locations and report current selected input versions"_test =
        [] static noexcept {
            auto session = AnalyzerSession();
            const auto library = std::string("export fn answer() -> i32 { return 42; }");
            const auto caller =
                std::string("import lib using answer; fn f() -> i32 { return answer(); }");
            update(session, "lib", 1, library);
            update(session, "app", 4, caller);
            select_project(session);
            const auto use = static_cast<std::uint32_t>(caller.rfind("answer"));
            const auto old = session.execute(AnalyzerDefinition {.document = "app", .offset = use});
            const auto* first = std::get_if<AnalyzerDefinitionResult>(&old.result);
            if (!expect(first != nullptr && first->location.has_value())) {
                return;
            }
            expect_equal(first->location->document, "lib");
            expect_equal(first->location->version, 1ll);
            expect_equal(session.counts().semantic, 1uz);

            update(session, "lib", 2, library);
            const auto stale = session.execute(
                AnalyzerUpdate {.document = "lib", .version = 2, .text = "invalid"}
            );
            const auto* error = std::get_if<AnalyzerFailure>(&stale.result);
            if (!expect(error != nullptr)) {
                return;
            }
            expect_equal(error->code, "stale_version");
            expect(stale.document_versions.empty());
            const auto current =
                session.execute(AnalyzerDefinition {.document = "app", .offset = use});
            const auto* definition = std::get_if<AnalyzerDefinitionResult>(&current.result);
            if (!expect(definition != nullptr && definition->location.has_value())) {
                return;
            }
            expect_equal(definition->location->version, 2ll);
            expect_equal(current.document_versions.size(), 2uz);
            const auto library_version = std::ranges::find(
                current.document_versions,
                "lib",
                &EditorDocumentVersion::document
            );
            const auto caller_version = std::ranges::find(
                current.document_versions,
                "app",
                &EditorDocumentVersion::document
            );
            if (!expect(library_version != current.document_versions.end())
                || !expect(caller_version != current.document_versions.end())) {
                return;
            }
            expect_equal(library_version->version, 2ll);
            expect_equal(caller_version->version, 4ll);
            expect_equal(session.counts().semantic, 1uz);

            update(session, "lib", 3, "// moved\n" + library);
            const auto moved =
                session.execute(AnalyzerDefinition {.document = "app", .offset = use});
            const auto* moved_result = std::get_if<AnalyzerDefinitionResult>(&moved.result);
            if (!expect(moved_result != nullptr && moved_result->location.has_value())) {
                return;
            }
            expect_equal(
                moved_result->location->range.start(),
                first->location->range.start() + 9u
            );
            expect_equal(session.counts().semantic, 2uz);
            expect_equal(first->location->version, 1ll);
            expect_equal(
                first->location->range.start(),
                static_cast<std::uint32_t>(library.find("answer"))
            );
        };

    "Analyzer session: type text remains valid after its semantic session is destroyed"_test =
        [] static noexcept {
            const auto responses = [] static noexcept {
                auto session = AnalyzerSession();
                const auto source = std::string(
                    "struct Pair { value: i32, } fn f(record: Pair, values: [i32; 2], callback: fn() -> i32) { let first = record; let copied = values; let cb = callback; }"
                );
                update(session, "lib", 1, source);
                const auto selected = session.execute(
                    AnalyzerReplaceProject {.modules = {{.document = "lib", .module_path = "lib"}}}
                );
                require(std::holds_alternative<AnalyzerAcknowledgement>(selected.result));
                auto named =
                    hover(session, "lib", static_cast<std::uint32_t>(source.rfind("record")));
                auto array =
                    hover(session, "lib", static_cast<std::uint32_t>(source.rfind("values")));
                auto callable =
                    hover(session, "lib", static_cast<std::uint32_t>(source.rfind("callback")));
                const auto partial_source = std::string(
                    "fn f() -> i32 { let v = 1; return v; } fn bad() -> i32 { return unknown; }"
                );
                update(session, "lib", 2, partial_source);
                auto partial =
                    hover(session, "lib", static_cast<std::uint32_t>(partial_source.find("1;")));
                return std::array {
                    std::move(named),
                    std::move(array),
                    std::move(callable),
                    std::move(partial)
                };
            }();
            expect_hover_text(responses[0], "Pair");
            expect_hover_text(responses[1], "[i32; 2]");
            expect_hover_text(responses[2], "fn() -> i32");
            expect_hover_text(responses[3], "i32");
        };

    "Analyzer session: project rejection is atomic and reopening resets document versions"_test =
        [] static noexcept {
            auto session = AnalyzerSession();
            const auto library = std::string("export fn answer() -> i32 { return 42; }");
            update(session, "lib", 1, library);
            update(
                session,
                "app",
                1,
                "import lib using answer; fn f() -> i32 { return answer(); }"
            );
            select_project(session);
            const auto rejected = session.execute(
                AnalyzerReplaceProject {
                    .modules = {
                        {.document = "lib", .module_path = "other"},
                        {.document = "app", .module_path = "invalid..path"}
                    }
                }
            );
            const auto* error = std::get_if<AnalyzerFailure>(&rejected.result);
            if (!expect(error != nullptr)) {
                return;
            }
            expect_equal(error->code, "module_path");
            const auto unchanged = session.execute(AnalyzerCheck {});
            const auto* check = std::get_if<AnalyzerCheckResult>(&unchanged.result);
            if (!expect(check != nullptr)) {
                return;
            }
            expect(check->published);

            const auto closed = session.execute(AnalyzerClose {.document = "lib"});
            if (!expect(std::holds_alternative<AnalyzerAcknowledgement>(closed.result))) {
                return;
            }
            expect(closed.document_versions.empty());
            const auto missing = session.execute(AnalyzerCheck {});
            const auto* failed = std::get_if<AnalyzerCheckResult>(&missing.result);
            if (!expect(failed != nullptr && !failed->diagnostics.empty())) {
                return;
            }
            expect(!failed->published);
            expect_equal(failed->diagnostics.front().code, "CV-COMPILATION-INPUT");
            expect(!failed->diagnostics.front().primary.has_value());

            update(session, "lib", -1, library);
            const auto reopened = session.execute(AnalyzerCheck {});
            const auto* restored = std::get_if<AnalyzerCheckResult>(&reopened.result);
            if (!expect(restored != nullptr)) {
                return;
            }
            expect(restored->published);
        };
});

} // namespace
