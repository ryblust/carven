module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.ownership;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.analysis.diagnostics;
import :semantic.analysis.ownership.context;
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
        "    let owner: String = \"hello\";\n"
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
        "    var first: String = \"first\";\n"
        "    var second: String = \"second\";\n"
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
        "    var first: String = \"first\";\n"
        "    var second: String = \"second\";\n"
        "    var closure = maker(&first);\n"
        "    let view = second.as_str();\n"
        "    let change = [&closure, &second]() { closure = maker(&second); };\n"
        "    closure(change);\n"
        "    let length = view.len();\n"
        "}\n"
    );
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Semantic ownership: forwarding chains reuse linearly bounded query contexts") {
    // Each shape forwards caller storage through a call chain. Query identity must
    // not depend on which ancestor allocated the forwarded object.
    struct ChainShape final {
        const char* name;
        const char* declaration;
        const char* forward;
        const char* last;
    };

    const auto shapes = std::array {
        ChainShape {
            .name = "i32 Read",
            .declaration = "(value: i32) -> i32",
            .forward = "return f{}(value);",
            .last = "return value;"
        },
        ChainShape {
            .name = "Value Read",
            .declaration = "(value: Value) -> Value",
            .forward = "return f{}(value);",
            .last = "return value;"
        },
        ChainShape {
            .name = "str Read",
            .declaration = "(value: str) -> str",
            .forward = "return f{}(value);",
            .last = "return value;"
        },
        ChainShape {
            .name = "String Read",
            .declaration = "(value: String) -> usize",
            .forward = "return f{}(value);",
            .last = "return 0;"
        },
        ChainShape {
            .name = "i32 Write",
            .declaration = "(&value: i32)",
            .forward = "f{}(&value);",
            .last = "value = 1;"
        },
        ChainShape {
            .name = "String Write",
            .declaration = "(&value: String)",
            .forward = "f{}(&value);",
            .last = "value.append(\"x\");"
        },
        ChainShape {
            .name = "local String Write",
            .declaration = "(&value: String)",
            .forward = "var local: String = \"y\"; f{}(&local); value.append(local.as_str());",
            .last = "value.append(\"x\");",
        },
    };
    const auto chain_lengths = std::array {16uz, 32uz};
    const auto declaration_orders = std::array {false, true};
    for (const auto& shape : shapes) {
        for (const auto count : chain_lengths) {
            for (const auto reversed : declaration_orders) {
                CAPTURE(shape.name);
                CAPTURE(count);
                CAPTURE(reversed);
                auto source = std::string("struct Value { number: i32 }\n");
                for (auto ordinal = 0uz; ordinal < count; ++ordinal) {
                    const auto index = reversed ? count - ordinal - 1uz : ordinal;
                    const auto next = index + 1uz;
                    source += std::format(
                        "fn f{}{} {{ {} }}\n",
                        index,
                        shape.declaration,
                        next == count ? std::string(shape.last)
                                      : std::vformat(shape.forward, std::make_format_args(next))
                    );
                }
                const auto program = analyze_test_program(std::move(source));
                auto diagnostics = DiagnosticSink();
                const auto summary =
                    OwnershipBatchAnalyzer(program, AnalysisDiagnostics(diagnostics)).run();
                REQUIRE(summary.has_value());
                CHECK_FALSE(diagnostics.has_errors());
                CHECK(summary->query_count <= 2uz * count);
                CHECK(summary->evaluation_count <= 4uz * count);
            }
        }
    }
}

TEST_CASE("Semantic ownership: recursive local forwarding preserves valid borrows") {
    static_cast<void>(analyze_test_program(R"(
        fn f(&text: String, depth: i32) -> void {
            var local: String = "a";
            local.append(text.as_str());
            if depth > 0 { f(&local, depth - 1); g(&text, depth - 1); }
        }
        fn g(&text: String, depth: i32) -> void {
            var other: String = "b";
            if depth > 0 { f(&other, depth - 1); }
            text.append(other.as_str());
        }
        fn apply(action: fn(&String) -> void, &text: String) { action(&text); }
        fn step(&text: String) -> void {
            var next: String = "d";
            apply(step, &next);
            text.append(next.as_str());
        }
    )"));
}

TEST_CASE("Semantic ownership: forwarded Write reaches a live borrow through a call chain") {
    const auto text = analyze_test_errors(R"(
        fn f3(&text: String) { text.append("x"); }
        fn f2(&text: String) { f3(&text); }
        fn f1(&text: String) { f2(&text); }
        fn keep(&text: String, view: str) -> str { f1(&text); return view; }
        fn invalid() -> usize {
            var owner: String = "hello";
            let view = keep(&owner, owner.as_str());
            return view.len();
        }
    )");
    CHECK(contains_diagnostic_code(text, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE(
    "Semantic ownership: Read view snapshots retain backing after source holder replacement"
) {
    const auto text = analyze_test_errors(R"(
        fn pick(value: str, effect: fn() -> void) -> str { effect(); return value; }
        fn invalid() {
            let owner: String = "hello";
            var selected: str = owner.as_str();
            let reset = [&selected]() { selected = ""; };
            let kept = pick(selected, reset);
            let moved = &&owner;
            let length = kept.len();
        }
    )");
    CHECK(contains_diagnostic_code(text, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Semantic ownership: indirect calls preserve recursive components") {
    const auto program = analyze_test_program(R"(
        fn apply(action: fn() -> void) { action(); }
        fn recursive() -> void { apply(recursive); }
        fn leaf() {}
        fn caller() { apply(leaf); }
    )");
    const auto components = ownership_recursion_components(program);
    const auto callables = test_function_callables(program);
    REQUIRE_EQ(callables.size(), 4uz);
    const auto component = [&](std::size_t index) noexcept {
        const auto body = program.declarations().body_for_callable(callables[index]);
        REQUIRE(body.has_value());
        return components.at(*body);
    };
    CHECK_EQ(component(0uz), component(1uz));
    CHECK_NE(component(0uz), component(2uz));
    CHECK_NE(component(0uz), component(3uz));
    CHECK_NE(component(2uz), component(3uz));
}

TEST_CASE("Semantic ownership: immediately called closures are not dynamic call targets") {
    const auto program = analyze_test_program(R"(
        fn apply(action: fn() -> void) { action(); }
        fn leaf() {}
        fn caller() { []() { apply(leaf); }(); }
    )");
    const auto components = ownership_recursion_components(program);
    auto distinct = std::flat_set<std::uint32_t>();
    for (const auto& [body, component] : components) {
        static_cast<void>(body);
        distinct.insert(component);
    }
    REQUIRE_EQ(components.size(), 4uz);
    CHECK_EQ(distinct.size(), components.size());
}
