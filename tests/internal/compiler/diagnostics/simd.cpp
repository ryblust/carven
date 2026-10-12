module carven:test.internal.compiler.diagnostics.simd;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {
const TestSuite suite([] static noexcept {
    "Compiler: SIMD checks logical types and static bounds"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "wide byte lane bounds",
             .source = R"(import std::simd using *;
const x = u8x32::splat(7).lane(32);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "u8x32::splat(7).lane(32)"},
            {.name = "wide float lane bounds",
             .source = R"(import std::simd using *;
const x = f32x8::splat(1.0).lane(8);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "f32x8::splat(1.0).lane(8)"},
            {.name = "wide mask prefix bounds",
             .source = R"(import std::simd using *;
const x = mask32::prefix(33);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "mask32::prefix(33)"},
            {.name = "mixed vector widths remain incompatible",
             .source = R"(import std::simd using u8x16;
import std::simd using u8x32;

fn f(a: u8x32, b: u8x16) -> u8x32 => a + b;)",
             .code = DiagnosticCode::TypeBinary,
             .primary_text = "+"},
            {.name = "lookup rejects floating indices",
             .source = R"(import std::simd using u8x16;
import std::simd using f32x4;

fn f(a: u8x16, indices: f32x4) -> u8x16 => a.lookup(indices);)",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "indices"},
            {.name = "lookup rejects scalar broadcast",
             .source = R"(import std::simd using *;
const result = u8x16::splat(7).lookup(0);)",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "0"},
            {.name = "lookup rejects mask indices",
             .source = R"(import std::simd using *;
const result = u8x32::splat(7).lookup(mask32::from_bits(0));)",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "mask32::from_bits(0)"},
            {.name = "mask is not a scalar condition",
             .source = R"(import std::simd using u8x16;

fn f(v: u8x16) { if v == 0 {} })",
             .code = DiagnosticCode::TypeConditionBool,
             .primary_text = "v == 0"},
            {.name = "out of range lane",
             .source = R"(import std::simd using *;
const x = u8x16::splat(7).lane(16);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "u8x16::splat(7).lane(16)"},
            {.name = "invalid mask prefix",
             .source = R"(import std::simd using *;
const x = mask16::prefix(17);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "mask16::prefix(17)"},
            {.name = "invalid left shift",
             .source = R"(import std::simd using *;
const x = u8x16::splat(7).shift_left(8);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "u8x16::splat(7).shift_left(8)"},
            {.name = "invalid right shift",
             .source = R"(import std::simd using *;
const x = u8x16::splat(7).shift_right(255);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "u8x16::splat(7).shift_right(255)"},
            {.name = "invalid extraction",
             .source = R"(import std::simd using *;
const x = u8x16::splat(0).extract(u8x16::splat(1), 17);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "u8x16::splat(0).extract(u8x16::splat(1), 17)"},
            {.name = "no implicit narrowing broadcast",
             .source = R"(import std::simd using u8x16;

fn f(v: u8x16, n: u16) -> u8x16 => v + n;)",
             .code = DiagnosticCode::TypeBinary,
             .primary_text = "+"},
            {.name = "sum rejects masks",
             .source = R"(import std::simd using mask16;

fn f(v: mask16) -> usize => v.sum();)",
             .code = DiagnosticCode::TypeMethodCall,
             .primary_text = "v.sum()"},
            {.name = "sum rejects floating vectors",
             .source = R"(import std::simd using f32x8;

fn f(v: f32x8) -> usize => v.sum();)",
             .code = DiagnosticCode::TypeMethodCall,
             .primary_text = "v.sum()"},
            {.name = "unsupported arithmetic",
             .source = R"(import std::simd using u8x16;

fn f(v: u8x16) -> u8x16 => v / v;)",
             .code = DiagnosticCode::TypeBinary,
             .primary_text = "/"},
            {.name = "short full load",
             .source = R"(import std::simd using *;
const bytes: [u8; 1] = [1]; const x = u8x16::load(bytes.as_slice(), 0);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "u8x16::load(bytes.as_slice(), 0)"},
            {.name = "float mask is not a condition",
             .source = R"(import std::simd using f32x4;

fn f(v: f32x4) { if v < 0.0 {} })",
             .code = DiagnosticCode::TypeConditionBool,
             .primary_text = "v < 0.0"},
            {.name = "float broadcast does not narrow",
             .source = R"(import std::simd using f32x4;

fn f(v: f32x4, n: f64) -> f32x4 => v + n;)",
             .code = DiagnosticCode::TypeBinary,
             .primary_text = "+"},
            {.name = "float bitwise requires explicit representation",
             .source = R"(import std::simd using f32x4;

fn f(v: f32x4) -> f32x4 => v & v;)",
             .code = DiagnosticCode::TypeBinary,
             .primary_text = "&"},
            {.name = "float lane bounds",
             .source = R"(import std::simd using *;
const x = f32x4::splat(1.0).lane(4);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "f32x4::splat(1.0).lane(4)"},
            {.name = "four lane mask bounds",
             .source = R"(import std::simd using *;
const x = mask4::prefix(5);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "mask4::prefix(5)"},
            {.name = "float extraction bounds",
             .source = R"(import std::simd using *;
const x = f32x4::splat(0.0).extract(f32x4::splat(1.0), 5);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "f32x4::splat(0.0).extract(f32x4::splat(1.0), 5)"},
            {.name = "float short load",
             .source = R"(import std::simd using *;
const data: [f32; 3] = [1.0, 2.0, 3.0]; const x = f32x4::load(data, 0);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "f32x4::load(data, 0)"},
            {.name = "runtime extract offset is rejected",
             .source = R"(import std::simd using u8x32;

fn f(a: u8x32, offset: usize) -> u8x32 => a.extract(a, offset);)",
             .code = DiagnosticCode::ConstAdmission,
             .primary_text = "offset"},
            {.name = "ordinary binding shift count is rejected",
             .source =
                 R"(import std::simd using u8x32;

fn f(a: u8x32) -> u8x32 { let count: usize = 1; return a.shift_left(count); })",
             .code = DiagnosticCode::ConstAdmission,
             .primary_text = "count"},
            {.name = "static shift bounds apply in runtime functions",
             .source = R"(import std::simd using u8x32;

fn f(a: u8x32) -> u8x32 => a.shift_right(8);)",
             .code = DiagnosticCode::ConstIndexBounds,
             .primary_text = "a.shift_right(8)"},
        });
        check_compiler_errors(cases);
    };
});
}
