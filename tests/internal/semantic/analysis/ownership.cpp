module carven:test.internal.semantic.analysis.ownership;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.diagnostics;
import :semantic.analysis.ownership.context;
import :semantic.analysis.program;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Semantic ownership: a condition retains both ownership paths"_test = [] static noexcept {
        for (const auto keyword : {"let", "const"}) {
            expect_diagnostic(
                analyze_test_errors(
                    std::format(
                        "fn probe() {{ let x = 1; {} flag = false; "
                        "if flag {{ let moved = &&x; }} let read = x; }}",
                        keyword
                    )
                ),
                DiagnosticCode::AccessUnavailable
            );
        }
    };

    "Semantic availability: a loop backedge observes the second Take"_test = [] static noexcept {
        const auto diagnostics = analyze_test_errors(
            std::string(semantic_test_payload_prelude)
            + "fn invalid() {\n"
              "    let payload = Payload { value: 1 };\n"
              "    while true { consume(&&payload); }\n"
              "}\n"
        );
        expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
    };

    "Semantic availability: a zero-iteration loop still joins with its Take path"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(
                std::string(semantic_test_payload_prelude)
                + "fn invalid(run: bool) {\n"
                  "    let payload = Payload { value: 1 };\n"
                  "    while run { consume(&&payload); break; }\n"
                  "    let observed = payload.value;\n"
                  "}\n"
            );
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: break and branch joins retain unavailable paths"_test =
        [] static noexcept {
            const auto break_diagnostics = analyze_test_errors(
                std::string(semantic_test_payload_prelude)
                + "fn invalid() {\n"
                  "    let payload = Payload { value: 1 };\n"
                  "    while true { consume(&&payload); break; }\n"
                  "    let observed = payload.value;\n"
                  "}\n"
            );
            expect_diagnostic(break_diagnostics, DiagnosticCode::AccessUnavailable);

            const auto join_diagnostics = analyze_test_errors(
                std::string(semantic_test_payload_prelude)
                + "fn invalid(flag: bool) {\n"
                  "    let payload = Payload { value: 1 };\n"
                  "    if flag { consume(&&payload); }\n"
                  "    let observed = payload.value;\n"
                  "}\n"
            );
            expect_diagnostic(join_diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: continue reaches the loop condition after Take"_test =
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
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: continue reaches a c-style step before its condition"_test =
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
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: assignment restores an owner consumed by a completed RHS"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(
                "struct Payload { value: i32 }\n"
                "fn relay(&&payload: Payload) -> Payload { return payload; }\n"
                "fn valid() {\n"
                "    var payload = Payload { value: 1 };\n"
                "    payload = relay(&&payload);\n"
                "}\n"
            ));
        };

    "Semantic availability: assignment restores an owner only after normal completion"_test =
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
            expect_diagnostic(failure_path, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: invocation failure edges retain Take state"_test = [] static noexcept {
        const auto diagnostics = analyze_test_errors(
            "struct Failure { code: i32 } struct Payload { value: i32 } "
            "fn fail(&&payload: Payload) throw Failure { "
            "throw Failure { code: payload.value }; } "
            "fn invalid(&&payload: Payload) { try { fail(&&payload)?; return; } "
            "catch { Failure(_) => {}, } let observed = payload.value; }"
        );
        expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
    };

    "Semantic availability: false catch guards carry Take state to fallback"_test =
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
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: rethrow carries Take state to an outer handler"_test =
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
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: multi-word place state converges through a loop"_test =
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
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Semantic availability: unreachable Take and reads remain valid"_test = [] static noexcept {
        static_cast<void>(analyze_test_program(
            std::string(semantic_test_payload_prelude)
            + "fn valid() {\n"
              "    let payload = Payload { value: 1 };\n"
              "    return;\n"
              "    consume(&&payload);\n"
              "    let observed = payload.value;\n"
              "}\n"
        ));
    };

    "Semantic availability: joins choose the earliest structural Take witness"_test =
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
            if (!expect_not_equal(first_take, std::string::npos)) {
                return;
            }
            if (!expect_not_equal(second_take, std::string::npos)) {
                return;
            }
            const auto diagnostics = analyze_test_errors(source);
            const auto* unavailable =
                find_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
            if (!expect(unavailable != nullptr)) {
                return;
            }
            if (!expect(!(unavailable->attachment.related.empty()))) {
                return;
            }
            const auto witness = unavailable->attachment.related.front().span.span.start();
            expect_greater_equal(witness, first_take);
            expect_less(witness, second_take);
        };

    "Semantic availability: closure-local places are isolated from the outer body"_test =
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
        };

    "Semantic availability: completed values release call accesses"_test = [] static noexcept {
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
        expect_diagnostic(direct, DiagnosticCode::AccessOperationConflict);
    };

    "Semantic availability: direct self transfer cannot restore its source"_test =
        [] static noexcept {
            for (const auto* expression : {"&&x", "(&&x)"}) {
                const auto diagnostics = analyze_test_errors(
                    std::string("fn invalid() { var x = 2; x = ") + expression + "; }"
                );
                expect_diagnostic(diagnostics, DiagnosticCode::AccessOperationConflict);
            }
        };

    "Semantic availability: consuming replacement fails without restoring the owner"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(
                "struct Error {}\n"
                "fn relay(&&x: i32) -> i32 throw Error { throw Error {}; }\n"
                "fn invalid() { var x = 1; try { x = relay(&&x)?; } catch { Error(_) => { let read = x; }, } }\n"
            );
            expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
        };

    "Type contents: type and declaration inputs belong to the same program"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto foreign = analyze_test_program("");
            expect(expect_termination("type-contents-foreign-declarations", [&] noexcept {
                static_cast<void>(compute_type_contents(program.types(), foreign.declarations()));
            }));
        };

    "Semantic availability: nested terminating loops preserve transfers"_test = [] static noexcept {
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
        expect_diagnostic(diagnostics, DiagnosticCode::AccessUnavailable);
    };

    "Semantic calls: recursive view replacement updates caller loans"_test = [] static noexcept {
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
            expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        }
    };

    "Semantic ownership: recursive returned views require live backing"_test = [] static noexcept {
        const auto diagnostics = analyze_test_errors(
            "fn recurse(flag: bool, input: [i32]) -> [i32] { "
            "let local = [1]; "
            "if flag { return recurse(false, local); } "
            "return input; }"
        );
        expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        static_cast<void>(analyze_test_program(
            "fn recurse(flag: bool, input: [i32]) -> [i32] { "
            "if flag { return recurse(false, input); } return input; }"
        ));
    };

    "Semantic ownership: recursive backing and callable graphs have finite query domains"_test =
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
        };

    "Semantic ownership: known recursive projections preserve returned backing"_test =
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
        };

    "Semantic ownership: unique callback slots retain definite loan release"_test =
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
        };

    "Semantic calls: normal and failure completions restore separate callable loans"_test =
        [] static noexcept {
            const auto declarations = std::string(R"(
        struct Failure {}
        fn empty() -> i32 => 0;
        fn replace(&selected: fn() -> i32, replacement: fn() -> i32, stop: bool) throw Failure {
            if stop {
                selected = replacement;
                throw Failure {};
            }
            selected = empty;
        }
    )");
            // Normal completion releases the old loan. Failure keeps the
            // replacement owner live and callable in the catch branch.
            static_cast<void>(analyze_test_program(declarations + R"(
        fn valid(flag: bool) {
            let value = 1;
            let owner = [value]() => value;
            var selected: fn() -> i32 = owner;
            try {
                replace(&selected, owner, flag)?;
                let moved = &&owner;
                let observed = selected();
            } catch {
                Failure(_) => { let observed = selected(); },
            }
        }
    )"));
            // That retained failure-path loan prevents taking its backing,
            // independently of the normal-path state from the same call.
            const auto diagnostics = analyze_test_errors(declarations + R"(
        fn invalid(flag: bool) {
            let value = 1;
            let owner = [value]() => value;
            var selected: fn() -> i32 = empty;
            try {
                replace(&selected, owner, flag)?;
            } catch {
                Failure(_) => { let moved = &&owner; },
            }
        }
    )");
            expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        };

    "Semantic ownership: definite aliases preserve ordered loan replacement"_test = [] static noexcept {
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
        expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
    };

    "Semantic ownership: later capture reads follow closure rebinding"_test = [] static noexcept {
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
        expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
    };

    "Semantic ownership: forwarding chains reuse linearly bounded query contexts"_test =
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
                        if (!(expect(summary.has_value())
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
                        expect(!(diagnostics.has_errors()))
                            .note(
                                "shape.name = ",
                                shape.name,
                                "count = ",
                                count,
                                "reversed = ",
                                reversed
                            );
                        expect(summary->query_count <= 2uz * count)
                            .note(
                                "shape.name = ",
                                shape.name,
                                "count = ",
                                count,
                                "reversed = ",
                                reversed
                            );
                        expect(summary->evaluation_count <= 4uz * count)
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
        };

    "Semantic ownership: recursive local forwarding preserves valid borrows"_test =
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
        };

    "Semantic ownership: forwarded Write reaches a live borrow through a call chain"_test =
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
            expect_diagnostic(text, DiagnosticCode::AccessBorrowConflict);
        };

    "Semantic ownership: Read view snapshots retain backing after source holder replacement"_test =
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
            expect_diagnostic(text, DiagnosticCode::AccessBorrowConflict);
        };

    "Semantic ownership: indirect calls preserve recursive components"_test = [] static noexcept {
        const auto program = analyze_test_program(R"(
        fn apply(action: fn() -> void) { action(); }
        fn recursive() -> void { apply(recursive); }
        fn leaf() {}
        fn caller() { apply(leaf); }
    )");
        const auto components = prepare_ownership_analysis(program).recursion_components;
        const auto callables = test_function_callables(program);
        if (!expect_equal(callables.size(), 4uz)) {
            return;
        }
        const auto component = [&](std::size_t index) noexcept {
            const auto body = program.declarations().body_for_callable(callables[index]);
            require(body.has_value());
            return components.at(*body);
        };
        expect(((component(0uz)) == (component(1uz)))).note("component(0uz) == component(1uz)");
        expect(component(0uz) != component(2uz)).note("components 0 and 2 differ");
        expect(component(0uz) != component(3uz)).note("components 0 and 3 differ");
        expect(component(2uz) != component(3uz)).note("components 2 and 3 differ");
    };

    "Semantic ownership: immediately called closures are not dynamic call targets"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        fn apply(action: fn() -> void) { action(); }
        fn leaf() {}
        fn caller() { []() { apply(leaf); }(); }
    )");
            const auto components = prepare_ownership_analysis(program).recursion_components;
            auto distinct = std::flat_set<std::uint32_t>();
            for (const auto& [body, component] : components) {
                static_cast<void>(body);
                distinct.insert(component);
            }
            if (!expect_equal(components.size(), 4uz)) {
                return;
            }
            expect_equal(distinct.size(), components.size());
        };

    "Semantic ownership: constant array backing permits immediate views but rejects holders"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const owned = [1, 2];
        fn view_of(values: [i32; 2]) -> [i32] => values;
        fn valid() -> usize { return view_of(owned).len(); }
    )");
            auto constant_arguments = 0uz;
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) != "valid") {
                    continue;
                }
                const auto body = program.declarations().body_for_callable(declaration.callable);
                require(body.has_value());
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* call = std::get_if<SemCall>(&expression.value);
                        if (call == nullptr) {
                            return;
                        }
                        if (!expect_equal(call->arguments.size(), 1uz)) {
                            return;
                        }
                        const auto& argument = call->arguments.front();
                        expect_equal(argument.access, AccessMode::Read);
                        expect_equal(argument.expression.category, SemanticValueCategory::Value);
                        const auto* constant = std::get_if<SemConstant>(&argument.expression.value);
                        if (!expect(constant != nullptr)) {
                            return;
                        }
                        const auto* type = std::get_if<ArrayTypeValue>(
                            &program.types().type(argument.expression.type.resolved()).value
                        );
                        if (!expect(type != nullptr)) {
                            return;
                        }
                        expect_equal(type->extent, 2ull);
                        const auto* element = std::get_if<BuiltinTypeValue>(
                            &program.types().type(type->element).value
                        );
                        if (!expect(element != nullptr)) {
                            return;
                        }
                        expect_equal(element->kind, BuiltinType::I32);
                        const auto* values = std::get_if<ArrayConstant>(
                            &program.constants().constant(constant->constant).value
                        );
                        if (!expect(values != nullptr)) {
                            return;
                        }
                        expect_equal(values->elements.size(), 2uz);
                        ++constant_arguments;
                    }
                );
            }
            expect_equal(constant_arguments, 1uz);

            auto sources = SourceManager();
            const auto source_id = sources.append_virtual("analysis.cv", R"(
        const owned = [1, 2];
        fn view_of(values: [i32; 2]) -> [i32] => values;
        fn invalid() {
            let escaped = view_of(owned);
            let length = escaped.len();
        }
    )");
            require(source_id.has_value());
            const auto input = SourceModuleInput {
                .source_id = *source_id,
                .module_path = semantic_test_module_path(),
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
            require(parsed.has_value());
            const auto result = analyze(std::move(*parsed));
            if (!expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
            expect_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
            if (diagnostic == nullptr) {
                return;
            }
            expect_equal(diagnostic->finding.severity, DiagnosticSeverity::Error);
            if (!expect(diagnostic->attachment.primary.has_value())) {
                return;
            }
            expect_equal(
                sources.slice(diagnostic->attachment.primary->span),
                "let escaped = view_of(owned)"
            );
        };
    "Semantic ownership: caller diagnosis observes completed dependency answers"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(fn valid() {
    let owner: String = "text";
    var view = owner.as_str();
    forward(&view);
    let moved = &&owner;
}
fn forward(&view: str) { release(&view); }
fn release(&view: str) { view = ""; }
)"));
            const auto diagnostics = analyze_test_errors(R"(fn invalid(flag: bool) {
    let owner: String = "text";
    var view = owner.as_str();
    forward(&view, flag);
    let moved = &&owner;
}
fn forward(&view: str, flag: bool) { release(&view, flag); }
fn release(&view: str, flag: bool) { if flag { view = ""; } }
)");
            expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        };

    "Semantic ownership: diagnosis publication follows query order and invalid completion"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
                DiagnosticCode code;
                std::string_view primary_range;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "earlier query precedes an independently solved error",
                    .source = R"(fn plain() -> i32 => 1;
fn earlier() {
    let x = 1;
    let moved = &&x;
    let observed = x;
    let result = plain();
}
fn later() {
    let y = 2;
    let moved = &&y;
    let observed = y;
}
)",
                    .code = DiagnosticCode::AccessUnavailable,
                    .primary_range = "let observed = x;",
                },
                Scenario {
                    .name = "invalid completion precedes earlier deferred error",
                    .source = R"(fn copied(&&text: String) -> String => text;
fn earlier() {
    let owner = 1;
    let moved = &&owner;
    let observed = owner;
}
fn escaping() {
    let factory = []() {
        var local = 1;
        return [&local]() { local += 1; };
    };
}
)",
                    .code = DiagnosticCode::AccessBorrowConflict,
                    .primary_range = "",
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto sources = SourceManager();
                const auto source_id =
                    sources.append_virtual("analysis.cv", std::string(scenario.source));
                require(source_id.has_value());
                const auto input = SourceModuleInput {
                    .source_id = *source_id,
                    .module_path = semantic_test_module_path(),
                };
                auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
                require(parsed.has_value());
                const auto result = analyze(std::move(*parsed));
                if (!expect(!result.has_value())) {
                    return;
                }
                const auto* diagnostic = find_diagnostic(result.error(), scenario.code);
                if (!expect_diagnostic(result.error(), scenario.code)
                    || diagnostic == nullptr
                    || !expect(diagnostic->attachment.primary.has_value())) {
                    return;
                }
                const auto& primary = diagnostic->attachment.primary->span;
                expect(primary.source_id == *source_id);
                const auto first = scenario.source.find(scenario.primary_range);
                require(first != std::string_view::npos);
                expect_greater_equal(primary.span.start(), first);
                if (scenario.code == DiagnosticCode::AccessUnavailable) {
                    expect_less(primary.span.start(), first + scenario.primary_range.size());
                } else {
                    expect_equal(primary.span.start(), 0u);
                    expect_greater(primary.span.end(), scenario.source.find("return [&local]"));
                    if (!expect(!diagnostic->attachment.related.empty())) {
                        return;
                    }
                    const auto& related = diagnostic->attachment.related.front().span;
                    expect(related.source_id == *source_id);
                    expect_equal(sources.slice(related), "[&local]() { local += 1; }");
                    expect_no_diagnostic(result.error(), DiagnosticCode::AccessUnavailable);
                }
                expect_no_diagnostic(result.error(), DiagnosticCode::LintReturnCopy);
            });
        };

    "Semantic availability: converged states preserve direct Take witnesses"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "loop backedge observes its Take",
                    .source = R"(struct Payload { value: i32 }
fn invalid(flag: bool) {
    let payload = Payload { value: 1 };
    while flag { let moved = &&payload; }
}
)",
                },
                Scenario {
                    .name = "branch joins keep the earliest Take",
                    .source = R"(struct Payload { value: i32 }
fn invalid(flag: bool) {
    let payload = Payload { value: 1 };
    if flag { let first = &&payload; }
    else { let second = &&payload; }
    let observed = payload.value;
}
)",
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                auto sources = SourceManager();
                const auto source_id =
                    sources.append_virtual("analysis.cv", std::string(scenario.source));
                require(source_id.has_value());
                const auto input = SourceModuleInput {
                    .source_id = *source_id,
                    .module_path = semantic_test_module_path(),
                };
                auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
                require(parsed.has_value());
                const auto result = analyze(std::move(*parsed));
                if (!expect(!result.has_value())) {
                    return;
                }
                const auto* diagnostic =
                    find_diagnostic(result.error(), DiagnosticCode::AccessUnavailable);
                if (!expect_diagnostic(result.error(), DiagnosticCode::AccessUnavailable)
                    || diagnostic == nullptr
                    || !expect(diagnostic->attachment.primary.has_value())
                    || !expect(!diagnostic->attachment.related.empty())) {
                    return;
                }
                const auto& primary = diagnostic->attachment.primary->span;
                const auto& related = diagnostic->attachment.related.front().span;
                expect(primary.source_id == *source_id);
                expect(related.source_id == *source_id);
                const auto declaration = scenario.source.find("let payload") + 4uz;
                expect_equal(primary.span.start(), declaration);
                expect_equal(sources.slice(primary), "payload");
                expect_equal(sources.slice(related), "&&");
                expect_equal(related.span.start(), scenario.source.find("&&payload"));
            });
        };

    "Semantic ownership: return-copy observations deduplicate across borrowed inputs"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto source_id = sources.append_virtual(
                "analysis.cv",
                R"(fn copied(&&copied_text: String, tokens: [i32]) -> String => copied_text;
fn blocked(&&blocked_text: String, tokens: [i32]) -> String {
    let held = blocked_text.as_str();
    let length = held.len();
    return blocked_text;
}
fn forward_copied(&&text: String, tokens: [i32]) -> String => copied(&&text, tokens);
fn forward_blocked(&&text: String, tokens: [i32]) -> String => blocked(&&text, tokens);
fn invoke() {
    let first: String = "first";
    let second: String = "second";
    let values = [1, 2];
    let copied_value = forward_copied(&&first, values);
    let blocked_value = forward_blocked(&&second, values);
}
)"
            );
            require(source_id.has_value());
            const auto input = SourceModuleInput {
                .source_id = *source_id,
                .module_path = semantic_test_module_path(),
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
            require(parsed.has_value());
            const auto result = analyze(std::move(*parsed));
            if (!expect(result.has_value())) {
                return;
            }
            auto returned = std::vector<std::string_view>();
            for (const auto& diagnostic : result->diagnostics) {
                if (diagnostic.finding.code != DiagnosticCode::LintReturnCopy) {
                    continue;
                }
                if (!expect(diagnostic.attachment.primary.has_value())) {
                    return;
                }
                expect(diagnostic.attachment.primary->span.source_id == *source_id);
                returned.push_back(sources.slice(diagnostic.attachment.primary->span));
            }
            expect(returned == std::vector<std::string_view> {"copied_text"});
        };

    "Semantic ownership: direct enum values retain caller backing and reject local escape"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(enum Values { Borrowed([i32]), Empty }
fn pack(values: [i32]) -> Values => Values::Borrowed(values);
fn valid(values: [i32]) -> usize {
    let wrapped = pack(values);
    return match wrapped { .Borrowed(items) => items.len(), .Empty => 0usize };
}
)");
            auto constructors = 0uz;
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) != "pack") {
                    continue;
                }
                const auto body = program.declarations().body_for_callable(declaration.callable);
                require(body.has_value());
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        expect_equal(std::holds_alternative<SemCall>(expression.value), false);
                        const auto* value = std::get_if<SemEnumCase>(&expression.value);
                        if (value == nullptr) {
                            return;
                        }
                        ++constructors;
                        if (!expect_equal(value->payload.size(), 1uz)) {
                            return;
                        }
                        expect_equal(
                            std::holds_alternative<SliceTypeValue>(
                                program.types().type(value->payload.front().type.resolved()).value
                            ),
                            true
                        );
                    }
                );
            }
            expect_equal(constructors, 1uz);
            const auto diagnostics = analyze_test_errors(R"(enum Values { Borrowed([i32]), Empty }
fn invalid() -> Values {
    let values = [1, 2];
    return Values::Borrowed(values);
}
)");
            expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        };

    "Semantic callable views: a known native target keeps its view representation"_test =
        [] static noexcept {
            const auto program =
                analyze_test_program(R"(private import(cpp) fn native_step(value: i32) -> i32;
fn known_view(value: i32) -> i32 {
    let view: fn(i32) -> i32 = native_step;
    return view(value);
}
)");
            auto calls = 0uz;
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) != "known_view") {
                    continue;
                }
                const auto body = program.declarations().body_for_callable(declaration.callable);
                require(body.has_value());
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* call = std::get_if<SemCall>(&expression.value);
                        if (call == nullptr) {
                            return;
                        }
                        expect_equal(
                            std::holds_alternative<CallableViewTypeValue>(
                                program.types().type(call->callee->type.resolved()).value
                            ),
                            true
                        );
                        if (!expect(call->target.has_value())) {
                            return;
                        }
                        expect_equal(
                            std::holds_alternative<CppImportImplementation>(
                                program.declarations().callable(*call->target).implementation
                            ),
                            true
                        );
                        ++calls;
                    }
                );
            }
            expect_equal(calls, 1uz);
        };

    "Semantic stable selection: native Write respects the protected selector"_test =
        [] static noexcept {
            static_cast<void>(
                analyze_test_program(R"(private import(cpp) fn native_write(&value: i32) -> bool;
fn valid() {
    var selected = 1;
    var other = 1;
    match selected { _ if native_write(&other) => {}, _ => {} }
}
)")
            );
            const auto diagnostics =
                analyze_test_errors(R"(private import(cpp) fn native_write(&value: i32) -> bool;
fn invalid() {
    var selected = 1;
    match selected { _ if native_write(&selected) => {}, _ => {} }
}
)");
            expect_diagnostic(diagnostics, DiagnosticCode::AccessOperationConflict);
        };

    "Semantic ownership: binary operands protect a view until the right operand finishes"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(fn valid() {
    var owner: String = "abc";
    var view = owner.as_str();
    let compared = view == "";
    view = "";
    owner.clear();
}
)");
            auto comparisons = 0uz;
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) != "valid") {
                    continue;
                }
                const auto body = program.declarations().body_for_callable(declaration.callable);
                require(body.has_value());
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* comparison = std::get_if<SemBinary>(&expression.value);
                        if (comparison == nullptr) {
                            return;
                        }
                        expect_equal(
                            std::holds_alternative<SemBinding>(comparison->left->value),
                            true
                        );
                        expect_equal(
                            std::holds_alternative<SemConstant>(comparison->right->value),
                            true
                        );
                        ++comparisons;
                    }
                );
            }
            expect_equal(comparisons, 1uz);

            auto sources = SourceManager();
            const auto source_id = sources.append_virtual("analysis.cv", R"(fn invalid() {
    var owner: String = "abc";
    var view = owner.as_str();
    let compared = view == (if true {
        view = "";
        owner.clear();
        ""
    } else { "" });
}
)");
            require(source_id.has_value());
            const auto input = SourceModuleInput {
                .source_id = *source_id,
                .module_path = semantic_test_module_path(),
            };
            auto parsed = parse_program(sources, SourceBatch {.modules = std::span(&input, 1)});
            require(parsed.has_value());
            const auto result = analyze(std::move(*parsed));
            if (!expect(!result.has_value())) {
                return;
            }
            const auto* diagnostic =
                find_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
            if (!expect_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict)
                || diagnostic == nullptr
                || !expect(diagnostic->attachment.primary.has_value())
                || !expect(!diagnostic->attachment.related.empty())) {
                return;
            }
            const auto& primary = diagnostic->attachment.primary->span;
            const auto& related = diagnostic->attachment.related.front().span;
            expect(primary.source_id == *source_id);
            expect(related.source_id == *source_id);
            expect_equal(sources.slice(primary), "owner.clear()");
            expect_equal(sources.slice(related), "owner.as_str()");
        };

    "Semantic availability: a failing binary left operand skips its unavailable right binding"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(struct Failure {}
fn stop() -> i32 throw Failure { throw Failure {}; }
fn valid() -> i32 throw Failure {
    let value = 1;
    let moved = &&value;
    return stop()? + value;
}
)");
            auto additions = 0uz;
            for (const auto [id, declaration] : program.declarations().functions()) {
                static_cast<void>(id);
                if (program.provenance().spelling(declaration.name) != "valid") {
                    continue;
                }
                const auto body = program.declarations().body_for_callable(declaration.callable);
                require(body.has_value());
                visit_semantic_nodes(
                    program.bodies().body(*body).region(),
                    [&](const SemanticExpression& expression) noexcept {
                        const auto* addition = std::get_if<SemBinary>(&expression.value);
                        if (addition == nullptr) {
                            return;
                        }
                        expect_equal(
                            std::holds_alternative<SemPropagate>(addition->left->value),
                            true
                        );
                        expect_equal(
                            std::holds_alternative<SemBinding>(addition->right->value),
                            true
                        );
                        ++additions;
                    }
                );
            }
            expect_equal(additions, 1uz);
        };
});

} // namespace
