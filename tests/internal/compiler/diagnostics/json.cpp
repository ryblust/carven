module carven:test.internal.compiler.diagnostics.json;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

constexpr auto json_modules = std::to_array<std::string_view>({
    "json.error",
    "json.node",
    "json.parse",
    "json.serialize",
    "json.string",
    "json.syntax",
    "json.value",
    "number.parse",
    "simd.bytes",
    "simd.scan",
    "utf.block",
    "utf.error",
    "utf.scan",
    "utf.validation",
});

constexpr auto json_imports = "import std::json.value using Value; "
                              "import std::json.error using ValueError; ";

const TestSuite suite([] static noexcept {
    "JSON craft: text views retain their value and member storage"_test = [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
        };
        const auto cases = std::to_array<Case>({
            {.name = "temporary string value cannot back a saved view",
             .source = "fn invalid() throw ValueError { "
                       "let text = Value::string(\"hello\").as_str()?; }"},
            {.name = "local number value cannot back a returned view",
             .source = "fn invalid() -> str throw ValueError { "
                       "let value = Value::integer(42); return value.number_text()?; }"},
            {.name = "number view prevents replacing its value",
             .source = "fn invalid() throw ValueError { "
                       "var value = Value::integer(42); let text = value.number_text()?; "
                       "value = Value::null(); }"},
            {.name = "key view prevents removing object members",
             .source = "fn invalid() throw ValueError { "
                       "var value = Value::object(); value.set(\"key\", &&Value::null())?; "
                       "let key = value.key_at(0)?; value.erase(\"key\")?; }"},
            {.name = "temporary child cannot back a saved view",
             .source = "fn invalid(value: Value) throw ValueError { "
                       "let text = value.get(\"key\")?.as_str()?; }"},
        });
        each(cases, &Case::name, [](const Case& input) static noexcept {
            with_compiled_source(
                std::string(json_imports) + std::string(input.source),
                [](const auto&, const auto& result) static noexcept {
                    if (!expect(!result.has_value())) {
                        return;
                    }
                    expect_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
                },
                json_modules
            );
        });
    };
});

} // namespace
