module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.loans;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic availability: Write capture and Take conflict across a callable") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    let callback = [&payload]() { payload.value = 2; };\n"
          "    consume(&&payload);\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessCaptureConflict));
}

TEST_CASE("Semantic availability: aggregate and branch results retain Write captures") {
    const auto aggregate = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    let callbacks = [ [&payload]() { payload.value = 2; } ];\n"
          "    consume(&&payload);\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(aggregate, DiagnosticCode::AccessCaptureConflict));

    const auto projection = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    let callback = [ [&payload]() { payload.value = 2; } ][0];\n"
          "    consume(&&payload);\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(projection, DiagnosticCode::AccessCaptureConflict));

    const auto place_read = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    let callbacks = [ [&payload]() { payload.value = 2; } ];\n"
          "    let callback = callbacks[0];\n"
          "    &&callbacks;\n"
          "    consume(&&payload);\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(place_read, DiagnosticCode::AccessCaptureConflict));

    const auto forwarded = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid(flag: bool) {\n"
          "    var payload = Payload { value: 1 };\n"
          "    let callback = [&payload]() { payload.value = 2; };\n"
          "    let selected = if flag { [&&callback] } else { [&&callback] };\n"
          "    consume(&&payload);\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(forwarded, DiagnosticCode::AccessCaptureConflict));
}

TEST_CASE("Semantic availability: discarded owners retain Write captures until scope exit") {
    for (const auto* keyword : {"let", "var"}) {
        CAPTURE(keyword);
        const auto retained = analyze_test_errors(
            std::string(semantic_test_payload_prelude)
            + "fn invalid() { var payload = Payload { value: 1 }; " + keyword
            + " _ = [&payload]() { payload.value = 2; }; "
              "consume(&&payload); }"
        );
        CHECK(contains_diagnostic_code(retained, DiagnosticCode::AccessCaptureConflict));

        static_cast<void>(analyze_test_program(
            std::string(semantic_test_payload_prelude)
            + "fn valid() { var payload = Payload { value: 1 }; if true { " + keyword
            + " _ = [&payload]() { payload.value = 2; }; } "
              "consume(&&payload); }"
        ));
    }
}

TEST_CASE("Semantic availability: Write capture ends with its actual holder") {
    static_cast<void>(analyze_test_program(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    [&payload]() { payload.value = 2; }();\n"
          "    consume(&&payload);\n"
          "}\n"
    ));

    static_cast<void>(analyze_test_program(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    if true {\n"
          "        let callback = [&payload]() { payload.value = 2; };\n"
          "    }\n"
          "    consume(&&payload);\n"
          "}\n"
    ));
}

TEST_CASE("Semantic availability: control-result callable loans end with borrower scope") {
    static_cast<void>(analyze_test_program(
        "fn fallback(value: i32) -> i32 { return value; }\n"
        "fn valid(flag: bool) {\n"
        "    let offset = 1;\n"
        "    let owner = [offset](value: i32) { return value + offset; };\n"
        "    if true {\n"
        "        let view: fn(i32) -> i32 = if flag { owner } else { fallback };\n"
        "        let observed = view(1);\n"
        "    }\n"
        "    let moved = &&owner;\n"
        "}\n"
    ));
}

TEST_CASE("Semantic loans: callee retains its backing during nested arguments") {
    const auto nested = analyze_test_errors(
        "fn invalid() { let offset = 1; let owner = [offset](x: i32) { return x + offset; }; "
        "let view: fn(i32) -> i32 = owner; "
        "let result = view(if true { let moved = &&owner; 0 } else { 0 }); }"
    );
    CHECK(contains_diagnostic_code(nested, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Semantic captures: returned closure cannot retain a local Write target") {
    const auto diagnostics = analyze_test_errors(
        "fn invalid() { let factory = []() { var local = 1; "
        "return [&local]() { local += 1; }; }; }"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Semantic relationships: completed calls deliver only retained captures") {
    const auto* factory = "let factory = [](&t: i32) { return [&t]() { t += 1; }; }; ";
    const auto valid = std::array {
        std::string("fn valid() {") + factory
            + "var x = 1; let c = if true { let inner = factory(&x); inner } else { factory(&x) }; c(); }",
        std::string(
            "fn valid() { let make = [](&x: i32, &y: i32) { x += 1; return [&y]() { y += 1; }; }; "
            "var x = 1; var y = 2; let c = make(&x, &y); let moved = &&x; c(); }"
        ),
        std::string(
            "fn valid() { let make = [](&x: i32) { x += 1; return []() {}; }; "
            "var x = 1; let c = make(&x); let moved = &&x; c(); }"
        ),
        std::string("fn valid() {") + factory
            + "var x = 1; var y = 2; var a = [factory(&x)]; a[0] = factory(&y); let moved = &&x; a[0](); }",
        std::string("fn valid() {") + factory
            + "var x = 1; var y = 2; var c = factory(&x); let observer = [&c]() { c(); }; "
              "c = factory(&y); let moved = &&x; observer(); }",
        std::string(
            "fn one() -> i32 { return 1; } fn valid() { let n = 1; let c = [n]() { return n; }; "
            "var a: [fn() -> i32; 1] = [c]; a[0] = one; let moved = &&c; let result = a[0](); }"
        ),
    };
    for (const auto& source : valid) {
        CAPTURE(source);
        static_cast<void>(analyze_test_program(source));
    }
    const auto invalid = std::array {
        std::string("fn invalid() {") + factory
            + "var x = 1; let c = if true { var local = 1; let inner = factory(&local); inner } "
              "else { factory(&x) }; c(); }",
        std::string(
            "fn touch(&x: i32) { x += 1; } fn invalid() { var x = 1; "
            "let c = [&x]() { x += 1; }; touch(&x); let moved = &&x; c(); }"
        ),
        std::string("fn invalid() {") + factory
            + "var x = 1; var y = 2; var c = factory(&x); "
              "if true { let setter = [&c, &y, factory]() { c = factory(&y); }; setter(); } "
              "let moved = &&y; c(); }",
    };
    for (const auto& source : invalid) {
        CAPTURE(source);
        const auto diagnostics = analyze_test_errors(source);
        CHECK(
            (contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict)
             || contains_diagnostic_code(diagnostics, DiagnosticCode::AccessCaptureConflict))
        );
    }
}

TEST_CASE("Semantic stable selection: guard access follows actual storage aliases") {
    const auto invalid = std::array {
        "fn invalid() { var x = 1; match x { _ if if true { x = 2; true } else { false } => {}, _ => {}, } }",
        "fn set(&x: i32) -> bool { x = 2; return true; } "
        "fn invalid() { var x = 1; match x { _ if set(&x) => {}, _ => {}, } }",
        "fn invalid() { var x = 1; let set = [&x]() { x = 2; return true; }; "
        "match x { _ if set() => {}, _ => {}, } }",
        "fn invalid() { var x = 1; match x { _ if if true { let moved = &&x; true } else { false } => {}, _ => {}, } }",
        "fn invalid() { var a = [1]; for &e in a { let moved = &&a; e = 0; } }",
    };
    for (const auto* source : invalid) {
        CAPTURE(source);
        const auto diagnostics = analyze_test_errors(source);
        CHECK(
            (contains_diagnostic_code(diagnostics, DiagnosticCode::AccessOperationConflict)
             || contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable))
        );
    }
    static_cast<void>(analyze_test_program(
        "fn set(&x: i32) -> bool { x = 2; return true; } "
        "fn valid() { var x = 1; var y = 1; match x { _ if set(&y) => { x = 3; }, _ => {}, } "
        "var a = [1]; for &e in a { e = 0; } let moved = &&a; }"
    ));
}

TEST_CASE("Semantic callable views: copies retain the target instead of intermediate storage") {
    static_cast<void>(analyze_test_program(
        "fn one() -> i32 { return 1; } "
        "fn valid() { var x = 1; let owner = [&x]() { x += 1; return x; }; "
        "var selected: fn() -> i32 = one; "
        "if true { let intermediate: fn() -> i32 = owner; selected = intermediate; } "
        "let result = selected(); }"
    ));
    const auto diagnostics = analyze_test_errors(
        "fn one() -> i32 { return 1; } "
        "fn invalid() { var selected: fn() -> i32 = one; "
        "if true { var x = 1; let owner = [&x]() { x += 1; return x; }; "
        "let intermediate: fn() -> i32 = owner; selected = intermediate; } "
        "let result = selected(); }"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::TypeCallableViewEscape));
}
