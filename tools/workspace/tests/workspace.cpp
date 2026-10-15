module carven:test.workspace;

import :diagnostics.code;
import :frontend.document;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.module_path;
import :source.standard;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :workspace.analysis;
import :workspace.semantic;
import :workspace.symbols;
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
    std::string document,
    std::int64_t version,
    std::string text
) noexcept -> void {
    require(host.update(std::move(document), version, std::move(text)).has_value());
}

auto offset(std::string_view text, std::string_view needle) noexcept -> std::uint32_t {
    const auto found = text.find(needle);
    require(found != std::string_view::npos);
    return static_cast<std::uint32_t>(found);
}

auto expect_type(const WorkspaceHoverQuery& query, BuiltinType type) noexcept -> void {
    require(query.analysis.result && query.result);
    const auto* program = query.analysis.result->program();
    require(program != nullptr);
    const auto* published =
        query.result->type ? std::get_if<TypeID>(&*query.result->type) : nullptr;
    expect(published != nullptr && *published == program->types().builtin_type(type));
}

const TestSuite tests([] static noexcept {
    "Workspace: analysis captures pure static output without executing ordinary host calls"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto project = std::array {
                project_module("app.cv", "app"),
                project_module("time.cv", "crafts.carven.std.time")
            };
            update(host, "time.cv", 1, "export const fn sample() -> i64 => monotonic_now();");
            update(
                host,
                "app.cv",
                1,
                "import std::time using sample; "
                "const { assert(6 * 7 == 42); println(\"checked\"); } "
                "fn run() -> i64 => sample();"
            );
            const auto analysis = host.snapshot().semantic(project);
            require(analysis.result->program() != nullptr);
            auto output = std::string();
            for (const auto& item : analysis.result->output()) {
                output += item.bytes;
            }
            expect_equal(output, "checked\n");
        };

    "Workspace: explicit static host effects require caller permission"_test = [] static noexcept {
        auto host = WorkspaceAnalysisHost();
        const auto project = std::array {
            project_module("app.cv", "app"),
            project_module("time.cv", "crafts.carven.std.time"),
            project_module("io.cv", "crafts.carven.std.io")
        };
        update(
            host,
            "time.cv",
            1,
            "export const fn sample() -> i64 => monotonic_now(); "
            "export const async fn pause() { await wait_until(0); }"
        );
        update(
            host,
            "io.cv",
            1,
            "export const async fn read() { let _ = await read_input(0, 0); }"
        );
        auto version = std::int64_t {0};
        for (const auto source : {
                 "import std::time using sample; const value = sample();",
                 "import std::time using pause; const { await pause(); }",
                 "import std::io using read; const { await read(); }",
             }) {
            update(host, "app.cv", ++version, source);
            const auto analysis = host.snapshot().semantic(project);
            expect(analysis.result->program() == nullptr).note("source = ", source);
            expect_diagnostic(analysis.result->diagnostics(), DiagnosticCode::ConstAdmission)
                .note("source = ", source);
        }
    };

    "Workspace: provided aliases preserve entity references and local rename scope"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto project = std::array {project_module("app.cv", "app")};
            const auto text = std::string(
                "import std::core using { String as Text, String as Other }; fn build() -> Text => Other::from_str(\"value\");"
            );
            update(host, "app.cv", 1, text);
            const auto snapshot = host.snapshot();
            const auto hover = snapshot.hover(project, "app.cv", offset(text, "Text =>"));
            require(hover.result && hover.result->entity);
            const auto* identity = std::get_if<ProvidedExport>(&*hover.result->entity);
            expect(identity && *identity == ProvidedExport::String);
            const auto definition = snapshot.definition(project, "app.cv", offset(text, "Text =>"));
            require(definition.result.has_value());
            expect_equal(definition.result->range.start(), offset(text, "Text,"));
            const auto references = snapshot.references(project, "app.cv", offset(text, "Text =>"));
            require(references.result.has_value());
            auto reference_offsets = std::vector<std::uint32_t>();
            for (const auto& location : *references.result) {
                expect_equal(location.document, "app.cv");
                reference_offsets.push_back(location.range.start());
            }
            expect_equal(
                reference_offsets,
                (std::vector<std::uint32_t> {
                    offset(text, "String as Text"),
                    offset(text, "Text,"),
                    offset(text, "String as Other"),
                    offset(text, "Other }"),
                    offset(text, "Text =>"),
                    offset(text, "Other::")
                })
            );
            const auto aliases =
                hover.analysis.result->alias_references("app.cv", offset(text, "Text =>"));
            require(aliases.has_value());
            auto alias_offsets = std::vector<std::uint32_t>();
            for (const auto& location : *aliases) {
                expect_equal(location.document, "app.cv");
                alias_offsets.push_back(location.range.start());
            }
            expect_equal(
                alias_offsets,
                (std::vector<std::uint32_t> {offset(text, "Text,"), offset(text, "Text =>")})
            );
            expect(!snapshot.definition(project, "app.cv", offset(text, "String as")).result);
        };
    "Workspace: duration literals discover their standard source declaration"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto project = std::array {
                project_module("app.cv", "app"),
                project_module("time.cv", "crafts.carven.std.time")
            };
            update(
                host,
                "time.cv",
                1,
                "export struct Duration { nanoseconds: i64 } "
                "export struct RangeError {} export struct Timeout {}"
            );
            const auto literal = std::string("fn run() { let ticks = (1ms).nanoseconds; }");
            update(host, "app.cv", 1, literal);
            expect_type(
                host.snapshot().hover(project, "app.cv", offset(literal, "ticks =")),
                BuiltinType::I64
            );
            const auto text = std::string(
                "import std::time using timeout as limit; async fn one() -> i32 => 1; "
                "async fn run() { let ticks = (1ms).nanoseconds; "
                "let result = try { await limit(one(), 1ms)? } catch { _ => 0, }; }"
            );
            update(host, "app.cv", 2, text);
            const auto snapshot = host.snapshot();
            expect_type(
                snapshot.hover(project, "app.cv", offset(text, "ticks =")),
                BuiltinType::I64
            );
            expect_type(
                snapshot.hover(project, "app.cv", offset(text, "result =")),
                BuiltinType::I32
            );
        };

    "Workspace: numeric parsing discovers source errors without importing their names"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto project = std::array {
                project_module("app.cv", "app"),
                project_module("core.cv", "crafts.carven.std.core")
            };
            const auto core = std::string("export enum NumberParseError { Invalid }");
            const auto text = std::string(
                "import std::core using i32 as Int; "
                "private fn parse(text: str) -> i32 => Int::parse(text)?; "
                "fn run() { let value = try { parse(\"42\")? } catch { _ => 0, }; }"
            );
            update(host, "core.cv", 1, core);
            update(host, "app.cv", 1, text);
            expect_type(
                host.snapshot().hover(project, "app.cv", offset(text, "value =")),
                BuiltinType::I32
            );
            const auto typed = std::string(
                "import std::core using NumberParseError as ParseError; "
                "fn run() throw ParseError { let value = 1; }"
            );
            update(host, "app.cv", 2, typed);
            const auto definition =
                host.snapshot().definition(project, "app.cv", offset(typed, "NumberParseError"));
            if (expect(definition.result.has_value())) {
                expect_equal(definition.result->document, std::string("core.cv"));
                expect_equal(definition.result->range.start(), offset(core, "NumberParseError"));
            }
        };

    "Workspace: standard associated calls discover source declarations through aliases"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            const auto project = std::array {
                project_module("app.cv", "app"),
                project_module("utf.cv", "crafts.carven.std.utf"),
                project_module("other.cv", "other")
            };
            const auto text = std::string(
                "import std::core using char as Scalar; "
                "fn run() { let character = Scalar::from_u32(65); }"
            );
            const auto utf = std::string(
                "export const fn char_from_u32(value: u32) -> char "
                "=> char::from_u32_unchecked(value);"
            );
            update(host, "app.cv", 1, text);
            update(host, "utf.cv", 1, utf);
            update(host, "other.cv", 1, "fn unfinished(");
            const auto snapshot = host.snapshot();
            expect_type(
                snapshot.hover(project, "app.cv", offset(text, "character =")),
                BuiltinType::Char
            );
            const auto definition =
                snapshot.definition(project, "app.cv", offset(text, "from_u32"));
            if (expect(definition.result.has_value())) {
                expect_equal(definition.result->document, std::string("utf.cv"));
                expect_equal(definition.result->range.start(), offset(utf, "char_from_u32"));
            }
        };

    "Workspace: editing dependencies updates navigation while retained results stay valid"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost(1);
            const auto project = std::array {
                project_module("lib.cv", "pkg.lib"),
                project_module("app.cv", "pkg.app"),
                project_module("other.cv", "other"),
            };
            constexpr auto app = std::string_view(
                "import .lib using answer; fn read() { let result = answer(); return result; }"
            );
            const auto loaded = host.update({
                {.document = "lib.cv", .version = 1, .text = "export fn answer() -> i32 => 1;"},
                {.document = "app.cv", .version = 1, .text = std::string(app)},
                {.document = "other.cv", .version = 1, .text = "fn unfinished("},
            });
            require(loaded.has_value());
            const auto before = host.snapshot();
            const auto old_hover = before.hover(project, "app.cv", offset(app, "result"));
            expect_type(old_hover, BuiltinType::I32);
            const auto old_definition =
                before.definition(project, "app.cv", offset(app, "answer()"));
            require(old_definition.result.has_value());
            expect_equal(old_definition.result->document, "lib.cv");
            expect_equal(old_definition.result->version, 1ll);
            const auto original = old_definition.analysis.result->source("lib.cv");
            require(original.has_value());
            expect_equal(slice(original->text, old_definition.result->range), "answer");
            // Navigation excludes an unrelated unfinished document; a project check includes it.
            const auto failed = before.semantic(project);
            expect(failed.result->program() == nullptr);
            expect(!failed.result->diagnostics().empty());

            const auto rejected = host.update({
                {.document = "lib.cv", .version = 2, .text = "export fn answer() -> i64 => 2;"},
                {.document = "other.cv", .version = 1, .text = "stale edit"},
            });
            require(!rejected.has_value());
            expect_equal(rejected.error().document, "other.cv");
            const auto unchanged = host.snapshot().syntax("lib.cv");
            require(unchanged.has_value());
            expect_equal(unchanged->version, 1ll);
            expect_equal(unchanged->result->source().text, original->text);

            const auto changed = host.update({
                {.document = "lib.cv", .version = 2, .text = "export fn answer() -> i64 => 2;"},
                {
                    .document = "other.cv",
                    .version = 2,
                    .text = "import pkg.lib using answer; fn other() => answer();",
                },
                {.document = "app.cv", .version = 2, .text = std::string(app)},
            });
            require(changed.has_value());
            expect(changed->back() == WorkspaceDocumentChange::VersionOnly);
            const auto after = host.snapshot();
            expect_type(after.hover(project, "app.cv", offset(app, "result")), BuiltinType::I64);
            const auto references = after.references(project, "app.cv", offset(app, "answer()"));
            require(references.result.has_value());
            auto locations = std::vector<std::string>();
            for (const auto& location : *references.result) {
                locations.push_back(location.document);
                const auto source = references.analysis.result->source(location.document);
                require(source.has_value());
                expect_equal(slice(source->text, location.range), "answer");
                expect_equal(location.version, 2ll);
            }
            const auto expected =
                std::vector<std::string> {"app.cv", "app.cv", "lib.cv", "other.cv", "other.cv"};
            expect(locations == expected);
            expect_type(old_hover, BuiltinType::I32);
            expect_type(before.hover(project, "app.cv", offset(app, "result")), BuiltinType::I32);
            require(host.remove("lib.cv"));
            const auto missing =
                host.snapshot().definition(project, "app.cv", offset(app, "answer()"));
            expect(!missing.result);
            expect(!missing.analysis.result->diagnostics().empty());
            expect_equal(slice(original->text, old_definition.result->range), "answer");
        };

    "Workspace: source outlines recover during editing and survive document closure"_test =
        [] static noexcept {
            auto host = WorkspaceAnalysisHost();
            update(
                host,
                "untitled:buffer",
                1,
                "// 中文\nstruct Pair { value: i32 } const broken = ; fn after() {}"
            );
            const auto old = host.snapshot().document_symbols("untitled:buffer");
            require(old && old->result);
            expect(!old->document.result->diagnostics().empty());
            require_equal(old->result->size(), 2uz);
            const auto& structure = old->result->front();
            expect_equal(structure.name, "Pair");
            require_equal(structure.children.size(), 1uz);
            expect_equal(
                slice(old->document.result->source().text, structure.children.front().selection),
                "value"
            );
            expect_equal(old->result->back().name, "after");

            update(
                host,
                "untitled:buffer",
                2,
                "// 中文\nstruct Pair { value: i32 } const repaired = 1; fn after() {}"
            );
            const auto repaired = host.snapshot().document_symbols("untitled:buffer");
            require(repaired && repaired->result);
            expect(repaired->document.result->diagnostics().empty());
            expect_equal(repaired->document.version, 2ll);
            expect_equal(repaired->result->size(), 3uz);
            require(host.remove("untitled:buffer"));
            expect(host.snapshot().workspace_symbols()->empty());
            update(host, "untitled:buffer", 1, "fn reopened() {}");
            const auto current = host.snapshot().workspace_symbols();
            require_equal(current->size(), 1uz);
            expect_equal(current->front().name, "reopened");
            expect_equal(old->result->back().name, "after");
            expect(!old->document.result->diagnostics().empty());
        };
});

} // namespace
