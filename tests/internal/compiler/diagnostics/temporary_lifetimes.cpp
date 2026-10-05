module carven:test.internal.compiler.diagnostics.temporary_lifetimes;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: condition temporaries and rejected match bindings end"_test =
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "if condition array backing",
                 .source = R"(
             const fn expose(values: [i32; 2], &out: ptr<i32>) -> bool {
                 out = addressof(values[0]);
                 return true;
             }
             const test "if temporary" {
                 var saved: ptr<i32> = nullptr;
                 if expose([1, 2], &saved) {
                     if saved != nullptr { check(*saved == 1); }
                 }
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*saved"},
                {.name = "rejected match guard binding",
                 .source = R"(
             const fn reject(address: ptr<i32>, &out: ptr<i32>) -> bool {
                 out = address;
                 return false;
             }
             const test "match guard" {
                 var saved: ptr<i32> = nullptr;
                 let selected = match 1 {
                     candidate if reject(addressof(candidate), &saved) => 0,
                     _ => {
                         if saved != nullptr { check(*saved == 1); }
                         1
                     },
                 };
                 check(selected == 1);
             }
         )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "*saved"},
            });
            check_compiler_errors(cases);
        };

    "Compiler diagnostics: slice element addresses grant only Read access"_test =
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "slice index cannot grant Write pointer",
                 .source = R"(
             fn invalid() {
                 var values = [1, 2];
                 var view = values.as_slice();
                 let address = addressof(&view[0]);
             }
         )",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = "&view[0]"},
                {.name = "slice index cannot transfer backing owner",
                 .source = R"(
             fn invalid() {
                 var values = [1, 2];
                 let view = values.as_slice();
                 let element = &&view[0];
             }
         )",
                 .code = DiagnosticCode::AccessTakeOperand,
                 .primary_text = "&&"},
                {.name = "slice element fields stay read-only",
                 .source = R"(
             struct Item { number: i32 }
             fn invalid() {
                 var values = [Item { number: 1 }];
                 var view = values.as_slice();
                 view[0].number = 2;
             }
         )",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = "view[0].number"},
                {.name = "native slice element member cannot grant Write pointer",
                 .source = R"(
             import <utility>;
             fn invalid() {
                 var values = [::std::pair<i32, i32> { 1, 2 }];
                 let view = values.as_slice();
                 let address = addressof(&view[0].first);
             }
         )",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = "&view[0].first"},
                {.name = "native slice element index cannot grant Write pointer",
                 .source = R"(
             import <vector>;
             fn invalid() {
                 var values = [::std::vector<i32> { 1, 2 }];
                 let view = values.as_slice();
                 let address = addressof(&view[0][0]);
             }
         )",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = "&view[0][0]"},
            });
            check_compiler_errors(cases);
        };
});

} // namespace
