module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.ownership;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
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
    static_cast<void>(analyze_test_program(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    var payload = Payload { value: 1 };\n"
          "    consume(&&payload);\n"
          "    payload = Payload { value: 2 };\n"
          "    let observed = payload.value;\n"
          "}\n"
    ));

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

TEST_CASE("Semantic availability: unreachable Take and reads remain valid") {
    static_cast<void>(analyze_test_program(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    return;\n"
          "    consume(&&payload);\n"
          "    let observed = payload.value;\n"
          "}\n"
    ));
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
    static_cast<void>(analyze_test_program(
        std::string(semantic_test_payload_prelude)
        + "fn valid() {\n"
          "    let payload = Payload { value: 1 };\n"
          "    let callback = []() {\n"
          "        let nested = Payload { value: 2 };\n"
          "        consume(&&nested);\n"
          "    };\n"
          "    let observed = payload.value;\n"
          "}\n"
    ));
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
        "fn left(again: bool, &selected: fn() -> i32, replacement: fn() -> i32) -> void { "
        "if again { right(false, &selected, replacement); } else { selected = replacement; } }"
    );
    const auto right = std::string(
        "fn right(again: bool, &selected: fn() -> i32, replacement: fn() -> i32) -> void { "
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

TEST_CASE("Nominal capabilities: shared type dependencies retain equality and ownership facts") {
    for (const auto supported : {true, false}) {
        CAPTURE(supported);
        auto source =
            std::string(supported ? "struct N0 { value: i32 }\n" : "struct N0 { value: [i32] }\n");
        for (auto index = 1uz; index < 28uz; ++index) {
            source += std::format(
                "struct N{} {{ left: N{}, right: N{} }}\n",
                index,
                index - 1,
                index - 1
            );
        }
        source += "fn accept(value: N27) {}\n";
        const auto program = analyze_test_program(source);
        for (const auto [id, declaration] : program.declarations().structures()) {
            static_cast<void>(id);
            CHECK_EQ(declaration.capabilities.equality, supported);
        }
        source += "fn equal(left: N27, right: N27) -> bool { return left == right; }\n";
        const auto diagnostics = analyze_test_errors(std::move(source));
        CHECK_EQ(
            contains_diagnostic_code(diagnostics, DiagnosticCode::TypeEqualityUnsupported),
            !supported
        );
    }
}

TEST_CASE("Semantic ownership: recursive returned views require live backing") {
    const auto diagnostics = analyze_test_errors(
        "fn recurse(flag: bool, input: [i32]) -> [i32] { "
        "let local = [1]; "
        "if flag { return recurse(false, local); } "
        "return input; }"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
    static_cast<void>(analyze_test_program(
        "fn recurse(flag: bool, input: [i32]) -> [i32] { "
        "if flag { return recurse(false, input); } return input; }"
    ));
}

TEST_CASE("Semantic ownership: recursive backing and callable graphs have finite query domains") {
    static_cast<void>(analyze_test_program(
        "struct Node { children: [Node] }\n"
        "fn descend(nodes: [Node], depth: i32) -> void {\n"
        " if depth > 0 { let local = [Node { nodes }]; descend(local, depth - 1); }\n"
        "}\n"
    ));
    static_cast<void>(analyze_test_program(
        "fn descend(callbacks: [fn() -> void], index: usize, depth: i32) -> void {\n"
        "    var count = 0;\n"
        "    let current = [&count]() { count += 1; };\n"
        "    let next: [fn() -> void; 2] = [callbacks[index], current];\n"
        "    if depth > 0 {\n"
        "        descend(next, index, depth - 1);\n"
        "    } else {\n"
        "        callbacks[index]();\n"
        "    }\n"
        "}\n"
    ));
}

TEST_CASE("Semantic ownership: known recursive projections preserve returned backing") {
    static_cast<void>(analyze_test_program(
        "struct Node { children: [Node] }\n"
        "fn second(nodes: [Node]) -> [Node] => nodes[0].children[0].children;\n"
        "fn valid(external: [Node]) -> [Node] {\n"
        " let middle = [Node { external }];\n"
        " let root = [Node { middle }];\n"
        " return second(root);\n"
        "}\n"
    ));
    static_cast<void>(analyze_test_program(
        "struct Node { left: [Node], right: [Node], end: [Node] }\n"
        "fn balanced(nodes: [Node], depth: i32) -> [Node] {\n"
        "    if depth > 0 {\n"
        "        return balanced(nodes[0].left, depth - 1)[0].right;\n"
        "    }\n"
        "    return nodes[0].end;\n"
        "}\n"
        "fn valid(external: [Node], depth: i32) -> [Node] {\n"
        "    let none = external.slice(0, 0);\n"
        "    let y = [Node { none, external, none }];\n"
        "    let w = [Node { none, y, none }];\n"
        "    let z = [Node { none, none, w }];\n"
        "    let x = [Node { z, none, y }];\n"
        "    let root = [Node { x, none, external }];\n"
        "    return balanced(root, depth);\n"
        "}\n"
    ));
}

TEST_CASE("Semantic ownership: unique callback slots retain definite loan release") {
    static_cast<void>(analyze_test_program(
        "fn empty() -> i32 => 0;\n"
        "fn noop() -> void {}\n"
        "fn clear_second(callbacks: [fn() -> void; 2]) { callbacks[1](); }\n"
        "fn descend(callbacks: [fn() -> void; 2], depth: i32) -> void {\n"
        "    let owner = String::from_str(\"hello\");\n"
        "    var view: str = owner.as_str();\n"
        "    let clear = [&view]() { view = \"\"; };\n"
        "    let next: [fn() -> void; 2] = [callbacks[1], clear];\n"
        "    if depth > 0 { descend(next, depth - 1); }\n"
        "    clear_second(next);\n"
        "    let moved = &&owner;\n"
        "}\n"
        "fn start(depth: i32) { descend([noop, noop], depth); }\n"
    ));
}

TEST_CASE("Semantic ownership: definite aliases preserve ordered loan replacement") {
    static_cast<void>(analyze_test_program(
        "fn empty() -> i32 => 0;\n"
        "fn set(&a: fn() -> i32, &b: fn() -> i32, source: fn() -> i32) { a = source; b = empty; }\n"
        "fn probe() { let n = 1; let closure = [n]() => n; var target: fn() -> i32 = empty; set(&target, &target, closure); let moved = &&closure; }\n"
    ));
    const auto diagnostics = analyze_test_errors(
        "fn empty() -> i32 => 0;\n"
        "fn set(&a: fn() -> i32, &b: fn() -> i32, source: fn() -> i32) { b = empty; a = source; }\n"
        "fn probe() { let n = 1; let closure = [n]() => n; var target: fn() -> i32 = empty; set(&target, &target, closure); let moved = &&closure; }\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Semantic ownership: later capture reads follow closure rebinding") {
    static_cast<void>(analyze_test_program(
        "fn maker(&text: String) {\n"
        "    return [&text](effect: fn() -> void) { effect(); text.clear(); };\n"
        "}\n"
        "fn probe() {\n"
        "    var first = String::from_str(\"first\");\n"
        "    var second = String::from_str(\"second\");\n"
        "    var closure = maker(&first);\n"
        "    let view = first.as_str();\n"
        "    let change = [&closure, &second]() { closure = maker(&second); };\n"
        "    closure(change);\n"
        "    let length = view.len();\n"
        "}\n"
    ));
    const auto diagnostics = analyze_test_errors(
        "fn maker(&text: String) {\n"
        "    return [&text](effect: fn() -> void) { effect(); text.clear(); };\n"
        "}\n"
        "fn probe() {\n"
        "    var first = String::from_str(\"first\");\n"
        "    var second = String::from_str(\"second\");\n"
        "    var closure = maker(&first);\n"
        "    let view = second.as_str();\n"
        "    let change = [&closure, &second]() { closure = maker(&second); };\n"
        "    closure(change);\n"
        "    let length = view.len();\n"
        "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
}
