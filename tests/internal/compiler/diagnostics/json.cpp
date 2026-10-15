module carven:test.internal.compiler.diagnostics.json;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

constexpr auto json_modules = std::to_array<std::string_view>({
    "core",
    "json",
    "json.node",
    "json.parse",
    "json.serialize",
    "json.string",
    "json.syntax",
    "simd.bytes",
    "simd.scan",
    "utf",
    "utf.block",
    "utf.scan",
});

constexpr auto json_imports = "import std::json using { Value, ValueError }; ";

const TestSuite suite([] static noexcept {
    "JSON craft: borrowed keys prevent removing object members"_test = [] static noexcept {
        with_compiled_source(
            std::string(json_imports)
                + "fn invalid() throw ValueError { "
                  "var value = Value::object(); value.set(\"key\", &&Value::null())?; "
                  "let key = value.key_at(0)?; value.erase(\"key\")?; }",
            [](const auto&, const auto& result) static noexcept {
                if (!expect(!result.has_value())) {
                    return;
                }
                expect_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
            },
            json_modules
        );
    };
});

} // namespace
