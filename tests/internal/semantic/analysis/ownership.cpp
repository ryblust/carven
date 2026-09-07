module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.ownership;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analysis.types.contents;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic availability: a loop backedge observes the second Take") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while true { consume(&&payload); }\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: a zero-iteration loop still joins with its Take path") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid(run: bool) {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while run { consume(&&payload); break; }\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: break and branch joins retain unavailable paths") {
    const auto break_diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while true { consume(&&payload); break; }\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(break_diagnostics, DiagnosticCode::AccessUnavailable));

    const auto join_diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid(flag: bool) {\n"
          "    let payload = Payload { value: 1 };\n"
          "    if flag { consume(&&payload); }\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(join_diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: continue reaches the loop condition after Take") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    while payload.value > 0 {\n"
          "        consume(&&payload);\n"
          "        continue;\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: continue reaches a c-style step before its condition") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    for var index: i32 = 0; index < 1; consume(&&payload) {\n"
          "        consume(&&payload);\n"
          "        continue;\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: assignment restores an owner consumed by a completed RHS") {
    static_cast<void>(analyze_test_program(
        "struct Payload { value: i32 }\n"
        "fn relay(&&payload: Payload) -> Payload { return payload; }\n"
        "fn valid() {\n"
        "    var payload = Payload { value: 1 };\n"
        "    payload = relay(&&payload);\n"
        "}\n"
    ));
}

TEST_CASE("Semantic availability: assignment restores an owner only after normal completion") {
    const auto restored = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    consume(&&payload);\n"
          "    payload = Payload { value: 2 };\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK_FALSE(contains_diagnostic_code(restored, DiagnosticCode::AccessUnavailable));

    const auto failure_path = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "struct Failure {}\n"
          "fn replacement() -> Payload throw Failure { throw Failure {}; }\n"
          "fn invalid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    consume(&&payload);\n"
          "    try { payload = replacement()?; } catch {\n"
          "        Failure(_) => { let observed = payload.value; },\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(failure_path, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: invocation failure edges retain Take state") {
    const auto diagnostics = analyze_test_errors(
        "struct Failure { code: i32 } struct Payload { value: i32 } "
        "fn fail(&&payload: Payload) throw Failure { "
        "throw Failure { code: payload.value }; } "
        "fn invalid(&&payload: Payload) { try { fail(&&payload)?; return; } "
        "catch { Failure(_) => {}, } let observed = payload.value; }"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: false catch guards carry Take state to fallback") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "struct Failure { code: i32 }\n"
          "fn fail() throw Failure { throw Failure { code: 1 }; }\n"
          "fn reject(&&payload: Payload) -> bool { return false; }\n"
          "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    try { fail()?; } catch {\n"
          "        Failure(_) if reject(&&payload) => {},\n"
          "        Failure(_) => { let observed = payload.value; },\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: rethrow carries Take state to an outer handler") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "struct Failure { code: i32 }\n"
          "fn fail() throw Failure { throw Failure { code: 1 }; }\n"
          "fn invalid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    try {\n"
          "        try { fail()?; } catch {\n"
          "            Failure(_) => { consume(&&payload); rethrow; },\n"
          "        }\n"
          "    } catch {\n"
          "        Failure(_) => { let observed = payload.value; },\n"
          "    }\n"
          "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: multi-word place state converges through a loop") {
    auto source = std::string(semantic_test_payload_prelude);
    source += "fn invalid(run: bool) {\n";
    for (auto index = 0; index < 70; ++index) {
        source += std::format("    let payload{} = Payload {{ value: {} }};\n", index, index);
    }
    source += "    while run { consume(&&payload69); break; }\n"
              "    let observed = payload69.value;\n"
              "}\n";
    const auto diagnostics = analyze_test_errors(std::move(source));
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: unreachable operations do not diagnose") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    return;\n"
          "    consume(&&payload);\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK_FALSE(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
    CHECK_FALSE(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessOperationConflict));
}

TEST_CASE("Semantic availability: joins choose the earliest structural Take witness") {
    const auto source = std::string(semantic_test_payload_prelude)
        + "fn invalid(flag: bool) {\n"
          "    let payload = Payload { value: 1 };\n"
          "    if flag { consume(&&payload); }\n"
          "    else { consume(&&payload); }\n"
          "    let observed = payload.value;\n"
          "}\n";
    const auto function_start = source.find("fn invalid");
    const auto first_take = source.find("&&payload", function_start);
    const auto second_take = source.find("&&payload", first_take + 1);
    REQUIRE_NE(first_take, std::string::npos);
    REQUIRE_NE(second_take, std::string::npos);
    const auto diagnostics = analyze_test_errors(source);
    const auto* unavailable = find_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable);
    REQUIRE(unavailable != nullptr);
    if (unavailable == nullptr) {
        return;
    }
    REQUIRE_FALSE(unavailable->attachment.related.empty());
    if (unavailable->attachment.related.empty()) {
        return;
    }
    const auto witness = unavailable->attachment.related.front().span.span.start();
    CHECK_GE(witness, first_take);
    CHECK_LT(witness, second_take);
}

TEST_CASE("Semantic availability: closure-local places are isolated from the outer body") {
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    let callback = []() {\n"
          "        let nested = Payload { value: 2 };\n"
          "        consume(&&nested);\n"
          "    };\n"
          "    let observed = payload.value;\n"
          "}\n"
    );
    CHECK_FALSE(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic availability: completed values release call accesses") {
    const auto prelude =
        std::string("fn pair(value: i32, &&owner: i32) -> i32 { return value + owner; }\n")
        + "fn identity(value: i32) -> i32 { return value; }\n";
    for (const auto* expression : {"x + 1", "identity(x)"}) {
        static_cast<void>(analyze_test_program(
            prelude + "fn valid() { let x = 2; let result = pair(" + expression + ", &&x); }"
        ));
    }
    const auto direct =
        analyze_test_errors(prelude + "fn invalid() { let x = 2; let result = pair(x, &&x); }");
    CHECK(contains_diagnostic_code(direct, DiagnosticCode::AccessOperationConflict));
}

TEST_CASE("Semantic availability: direct self transfer cannot restore its source") {
    for (const auto* expression : {"&&x", "(&&x)"}) {
        const auto diagnostics =
            analyze_test_errors(std::string("fn invalid() { var x = 2; x = ") + expression + "; }");
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessOperationConflict));
    }
}

TEST_CASE("Semantic availability: consuming replacement fails without restoring the owner") {
    const auto diagnostics = analyze_test_errors(
        "struct Error {}\n"
        "fn relay(&&x: i32) -> i32 throw Error { throw Error {}; }\n"
        "fn invalid() { var x = 1; try { x = relay(&&x)?; } catch { Error(_) => { let read = x; }, } }\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Type contents: type and declaration inputs belong to the same program") {
    const auto program = analyze_test_program("");
    const auto foreign = analyze_test_program("");
    CHECK(expect_termination("type-contents-foreign-declarations", [&] noexcept {
        static_cast<void>(compute_type_contents(program.types(), foreign.declarations()));
    }));
}

TEST_CASE("Semantic availability: nested terminating loops preserve transfers") {
    auto body = std::string("consume(&&payload);");
    for (auto depth = 0uz; depth < 16uz; ++depth) {
        body.insert(0uz, "while flag { ");
        body.append(" break; }");
    }
    static_cast<void>(analyze_test_program(
        std::string(semantic_test_payload_prelude)
        + "fn valid(flag: bool) { let payload = Payload { 1 }; " + body + " }"
    ));
    const auto diagnostics = analyze_test_errors(
        std::string(semantic_test_payload_prelude)
        + "fn invalid(flag: bool) { let payload = Payload { 1 }; " + body
        + " let observed = payload.value; }"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessUnavailable));
}

TEST_CASE("Semantic calls: recursive view replacement updates caller loans") {
    const auto left = std::string(
        "fn left(again: bool, &selected: fn() -> i32, replacement: fn() -> i32) { "
        "if again { right(false, &selected, replacement); } else { selected = replacement; } }"
    );
    const auto right = std::string(
        "fn right(again: bool, &selected: fn() -> i32, replacement: fn() -> i32) { "
        "if again { left(false, &selected, replacement); } else { selected = replacement; } }"
    );
    for (const auto reverse : {false, true}) {
        const auto declarations =
            (reverse ? right + left : left + right) + "fn one() -> i32 { return 1; } ";
        static_cast<void>(analyze_test_program(
            declarations
            + "fn valid() { let x = 2; let owner = [x]() { return x; }; "
              "var selected: fn() -> i32 = owner; left(true, &selected, one); "
              "let moved = &&owner; let result = selected(); }"
        ));
        const auto diagnostics = analyze_test_errors(
            declarations
            + "fn invalid() { let x = 2; let owner = [x]() { return x; }; "
              "var selected: fn() -> i32 = one; left(true, &selected, owner); "
              "let moved = &&owner; let result = selected(); }"
        );
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
    }
}
