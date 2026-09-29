module carven:test.internal.semantic.analysis.ownership;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.diagnostics;
import :semantic.analysis.ownership.context;
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
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Semantic availability: a loop backedge observes the second Take", [] static noexcept {
        const auto diagnostics = analyze_test_errors(
            std::string(semantic_test_payload_prelude)
            + "fn invalid() {\n"
              "    let payload = Payload { value: 1 };\n"
              "    while true { consume(&&payload); }\n"
              "}\n"
        );
        ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
    });

    ct::test(
        "Semantic availability: a zero-iteration loop still joins with its Take path",
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(
                std::string(semantic_test_payload_prelude)
                + "fn invalid(run: bool) {\n"
                  "    let payload = Payload { value: 1 };\n"
                  "    while run { consume(&&payload); break; }\n"
                  "    let observed = payload.value;\n"
                  "}\n"
            );
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: break and branch joins retain unavailable paths",
        [] static noexcept {
            const auto break_diagnostics = analyze_test_errors(
                std::string(semantic_test_payload_prelude)
                + "fn invalid() {\n"
                  "    let payload = Payload { value: 1 };\n"
                  "    while true { consume(&&payload); break; }\n"
                  "    let observed = payload.value;\n"
                  "}\n"
            );
            ct::expect_diagnostic(break_diagnostics, DiagnosticCode::AccessUnavailable);

            const auto join_diagnostics = analyze_test_errors(
                std::string(semantic_test_payload_prelude)
                + "fn invalid(flag: bool) {\n"
                  "    let payload = Payload { value: 1 };\n"
                  "    if flag { consume(&&payload); }\n"
                  "    let observed = payload.value;\n"
                  "}\n"
            );
            ct::expect_diagnostic(join_diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: continue reaches the loop condition after Take",
        [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: continue reaches a c-style step before its condition",
        [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: assignment restores an owner consumed by a completed RHS",
        [] static noexcept {
            static_cast<void>(analyze_test_program(
                "struct Payload { value: i32 }\n"
                "fn relay(&&payload: Payload) -> Payload { return payload; }\n"
                "fn valid() {\n"
                "    var payload = Payload { value: 1 };\n"
                "    payload = relay(&&payload);\n"
                "}\n"
            ));
        }
    );

    ct::test(
        "Semantic availability: assignment restores an owner only after normal completion",
        [] static noexcept {
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
            ct::expect_diagnostic(failure_path, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: invocation failure edges retain Take state",
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(
                "struct Failure { code: i32 } struct Payload { value: i32 } "
                "fn fail(&&payload: Payload) throw Failure { "
                "throw Failure { code: payload.value }; } "
                "fn invalid(&&payload: Payload) { try { fail(&&payload)?; return; } "
                "catch { Failure(_) => {}, } let observed = payload.value; }"
            );
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: false catch guards carry Take state to fallback",
        [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: rethrow carries Take state to an outer handler",
        [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Semantic availability: multi-word place state converges through a loop",
        [] static noexcept {
            auto source = std::string(semantic_test_payload_prelude);
            source += "fn invalid(run: bool) {\n";
            for (auto index = 0; index < 70; ++index) {
                source +=
                    std::format("    let payload{} = Payload {{ value: {} }};\n", index, index);
            }
            source += "    while run { consume(&&payload69); break; }\n"
                      "    let observed = payload69.value;\n"
                      "}\n";
            const auto diagnostics = analyze_test_errors(std::move(source));
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test("Semantic availability: unreachable Take and reads remain valid", [] static noexcept {
        static_cast<void>(analyze_test_program(
            std::string(semantic_test_payload_prelude)
            + "fn valid() {\n"
              "    let payload = Payload { value: 1 };\n"
              "    return;\n"
              "    consume(&&payload);\n"
              "    let observed = payload.value;\n"
              "}\n"
        ));
    });

    ct::test(
        "Semantic availability: joins choose the earliest structural Take witness",
        [] static noexcept {
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
            if (!ct::expect_not_equal(first_take, std::string::npos)) {
                return;
            }
            if (!ct::expect_not_equal(second_take, std::string::npos)) {
                return;
            }
            const auto diagnostics = analyze_test_errors(source);
            const auto* unavailable =
                ct::find_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
            if (!ct::expect(unavailable != nullptr)) {
                return;
            }
            if (!ct::expect(!(unavailable->attachment.related.empty()))) {
                return;
            }
            const auto witness = unavailable->attachment.related.front().span.span.start();
            ct::expect_greater_equal(witness, first_take);
            ct::expect_less(witness, second_take);
        }
    );

    ct::test(
        "Semantic availability: closure-local places are isolated from the outer body",
        [] static noexcept {
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
    );

    ct::test("Semantic availability: completed values release call accesses", [] static noexcept {
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
        ct::expect_diagnostic(direct, DiagnosticCode::AccessOperationConflict);
    });

    ct::test(
        "Semantic availability: direct self transfer cannot restore its source",
        [] static noexcept {
            for (const auto* expression : {"&&x", "(&&x)"}) {
                const auto diagnostics = analyze_test_errors(
                    std::string("fn invalid() { var x = 2; x = ") + expression + "; }"
                );
                ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessOperationConflict);
            }
        }
    );

    ct::test(
        "Semantic availability: consuming replacement fails without restoring the owner",
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(
                "struct Error {}\n"
                "fn relay(&&x: i32) -> i32 throw Error { throw Error {}; }\n"
                "fn invalid() { var x = 1; try { x = relay(&&x)?; } catch { Error(_) => { let read = x; }, } }\n"
            );
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test(
        "Type contents: type and declaration inputs belong to the same program",
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto foreign = analyze_test_program("");
            ct::expect(expect_termination("type-contents-foreign-declarations", [&] noexcept {
                static_cast<void>(compute_type_contents(program.types(), foreign.declarations()));
            }));
        }
    );

    ct::test(
        "Semantic availability: nested terminating loops preserve transfers",
        [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        }
    );

    ct::test("Semantic calls: recursive view replacement updates caller loans", [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        }
    });

    ct::test(
        "Semantic ownership: recursive returned views require live backing",
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(
                "fn recurse(flag: bool, input: [i32]) -> [i32] { "
                "let local = [1]; "
                "if flag { return recurse(false, local); } "
                "return input; }"
            );
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
            static_cast<void>(analyze_test_program(
                "fn recurse(flag: bool, input: [i32]) -> [i32] { "
                "if flag { return recurse(false, input); } return input; }"
            ));
        }
    );

    ct::test(
        "Semantic ownership: recursive backing and callable graphs have finite query domains",
        [] static noexcept {
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
    );

    ct::test(
        "Semantic ownership: known recursive projections preserve returned backing",
        [] static noexcept {
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
    );

    ct::test(
        "Semantic ownership: unique callback slots retain definite loan release",
        [] static noexcept {
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
    );

    ct::test("Semantic ownership: definite aliases preserve ordered loan replacement", [] static noexcept {
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
        ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
    });

    ct::test(
        "Semantic ownership: later capture reads follow closure rebinding",
        [] static noexcept {
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        }
    );

    ct::test(
        "Semantic ownership: forwarding chains reuse linearly bounded query contexts",
        [] static noexcept {
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
                    .forward =
                        "var local: String = \"y\"; f{}(&local); value.append(local.as_str());",
                    .last = "value.append(\"x\");",
                },
            };
            const auto chain_lengths = std::array {16uz, 32uz};
            const auto declaration_orders = std::array {false, true};
            for (const auto& shape : shapes) {
                for (const auto count : chain_lengths) {
                    for (const auto reversed : declaration_orders) {
                        auto source = std::string("struct Value { number: i32 }\n");
                        for (auto ordinal = 0uz; ordinal < count; ++ordinal) {
                            const auto index = reversed ? count - ordinal - 1uz : ordinal;
                            const auto next = index + 1uz;
                            source += std::format(
                                "fn f{}{} {{ {} }}\n",
                                index,
                                shape.declaration,
                                next == count
                                    ? std::string(shape.last)
                                    : std::vformat(shape.forward, std::make_format_args(next))
                            );
                        }
                        const auto program = analyze_test_program(std::move(source));
                        auto diagnostics = DiagnosticSink();
                        const auto summary =
                            OwnershipBatchAnalyzer(program, AnalysisDiagnostics(diagnostics)).run();
                        if (!(ct::expect(summary.has_value())
                                  .note(
                                      "shape.name = ",
                                      shape.name,
                                      "count = ",
                                      count,
                                      "reversed = ",
                                      reversed
                                  ))) {
                            return;
                        }
                        ct::expect(!(diagnostics.has_errors()))
                            .note(
                                "shape.name = ",
                                shape.name,
                                "count = ",
                                count,
                                "reversed = ",
                                reversed
                            );
                        ct::expect(summary->query_count <= 2uz * count)
                            .note(
                                "shape.name = ",
                                shape.name,
                                "count = ",
                                count,
                                "reversed = ",
                                reversed
                            );
                        ct::expect(summary->evaluation_count <= 4uz * count)
                            .note(
                                "shape.name = ",
                                shape.name,
                                "count = ",
                                count,
                                "reversed = ",
                                reversed
                            );
                    }
                }
            }
        }
    );

    ct::test(
        "Semantic ownership: recursive local forwarding preserves valid borrows",
        [] static noexcept {
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
    );

    ct::test(
        "Semantic ownership: forwarded Write reaches a live borrow through a call chain",
        [] static noexcept {
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
            ct::expect_diagnostic(text, DiagnosticCode::AccessBorrowConflict);
        }
    );

    ct::test(
        "Semantic ownership: Read view snapshots retain backing after source holder replacement",
        [] static noexcept {
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
            ct::expect_diagnostic(text, DiagnosticCode::AccessBorrowConflict);
        }
    );

    ct::test(
        "Semantic ownership: indirect calls preserve recursive components",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn apply(action: fn() -> void) { action(); }
        fn recursive() -> void { apply(recursive); }
        fn leaf() {}
        fn caller() { apply(leaf); }
    )");
            const auto components = ownership_recursion_components(program);
            const auto callables = test_function_callables(program);
            if (!ct::expect_equal(callables.size(), 4uz)) {
                return;
            }
            const auto component = [&](std::size_t index) noexcept {
                const auto body = program.declarations().body_for_callable(callables[index]);
                ct::require(body.has_value());
                return components.at(*body);
            };
            ct::expect(((component(0uz)) == (component(1uz))))
                .note("component(0uz) == component(1uz)");
            ct::expect(component(0uz) != component(2uz)).note("components 0 and 2 differ");
            ct::expect(component(0uz) != component(3uz)).note("components 0 and 3 differ");
            ct::expect(component(2uz) != component(3uz)).note("components 2 and 3 differ");
        }
    );

    ct::test(
        "Semantic ownership: immediately called closures are not dynamic call targets",
        [] static noexcept {
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
            if (!ct::expect_equal(components.size(), 4uz)) {
                return;
            }
            ct::expect_equal(distinct.size(), components.size());
        }
    );
});

} // namespace
