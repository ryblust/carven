module carven:test.internal.compiler.diagnostics.addressof;

import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: addressof requires a live addressable place and matching access"_test =
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "temporary value",
                 .source = "fn invalid() { let address = addressof(1); }",
                 .code = DiagnosticCode::AccessNotAssignable,
                 .primary_text = "1"},
                {.name = "lexical constant has no storage",
                 .source = "fn invalid() { const value = 1; let address = addressof(value); }",
                 .code = DiagnosticCode::AccessNotAssignable,
                 .primary_text = "value"},
                {.name = "immutable owner cannot grant Write",
                 .source = "fn invalid() { let value = 1; let address = addressof(&value); }",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = "&value"},
                {.name = "address cannot take owner",
                 .source = "fn invalid() { var value = 1; let address = addressof(&&value); }",
                 .code = DiagnosticCode::AccessCallMismatch,
                 .primary_text = "&&value"},
            });
            check_compiler_errors(cases);
        };

    "Compiler diagnostics: static pointer targets end at scope exit and Take"_test =
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "temporary array ends after the full expression",
                 .source = R"(
             const fn first(values: [i32; 2]) -> ptr<i32> => addressof(values[0]);
             const test "temporary" {
                 let address = first([1, 2]);
                 if address != nullptr { check(*address == 1); }
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*address"},
                {.name = "branch local has ended",
                 .source = R"(
             const test "branch" {
                 var address: ptr<i32> = nullptr;
                 if true {
                     var value = 1;
                     address = addressof(value);
                 }
                 if address != nullptr { check(*address == 1); }
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*address"},
                {.name = "returned local has ended",
                 .source = R"(
             const fn escaped() -> ptr<i32> {
                 var value = 1;
                 return addressof(value);
             }
             const test "dangling" {
                 let address = escaped();
                 if address != nullptr { check(*address == 1); }
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*address"},
                {.name = "Take ends the old target",
                 .source = R"(
             const test "taken" {
                 var value = 1;
                 let address = addressof(value);
                 let moved = &&value;
                 if address != nullptr { check(*address == 1); }
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*address"},
                {.name = "reinitialization does not revive an old target",
                 .source = R"(
             const test "reinitialized" {
                 var value = 1;
                 let address = addressof(value);
                 let moved = &&value;
                 value = 2;
                 if address != nullptr { check(*address == 2); }
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*address"},
            });
            check_compiler_errors(cases);
        };

    "Compiler: addressof proves non-null and preserves cross-call aliases on assignment"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = *sources.append_virtual("addressof.cv", R"(
        const fn read_address(address: ptr<i32>) -> i32 {
            if address == nullptr { return -1; }
            return *address;
        }
        const fn write_address(address: ptr<&i32>) {
            if address != nullptr { *address = 8; }
        }
        const test "local addresses" {
            var value = 1;
            let address = addressof(&value);
            check(address != nullptr);
            *address = 2;
            check(value == 2);
            write_address(address);
            check(read_address(address) == 8);
            value = 9;
            check(read_address(address) == 9);
        }
    )");
            const auto input = SourceModuleInput {
                .source_id = source_id,
                .module_path = *CanonicalModulePath::from_value("addressof"),
            };
            const auto result = compile(
                sources,
                SourceBatch {.modules = std::span(&input, 1)},
                TargetPlanningRequest {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = *LinkageDomain::explicit_value("test:pointer-address-of"),
                }
            );
            auto diagnostic_report = std::string();
            if (!result.has_value()) {
                for (const auto& diagnostic : result.error()) {
                    diagnostic_report += std::format(
                        "{}: {}\n",
                        static_cast<int>(diagnostic.finding.code),
                        diagnostic.finding.message
                    );
                }
            }
            expect(result.has_value()).note(diagnostic_report);
        };
});

} // namespace
