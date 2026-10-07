module carven:test.internal.semantic.analysis.payload_borrows;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Borrowed payload ownership: projections pin ancestors and end with their arm"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            enum Node { Values(Sequence<String>), Empty }
            fn invalid() {
                var value: Node = .Values(Sequence<String> {});
                match value {
                    .Values(&items) => { value = .Empty; },
                    .Empty => {},
                }
            }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
            static_cast<void>(analyze_test_program(R"(
            enum Node { Values(Sequence<String>), Empty }
            fn accepted() {
                var value: Node = .Values(Sequence<String> {});
                match value {
                    .Values(&items) => { items.push("first" as String); },
                    .Empty => {},
                }
                value = .Empty;
            }
        )"));
        };

    "Borrowed payload nullability: writes invalidate shared proofs"_test = [] static noexcept {
        expect_diagnostic(
            analyze_test_errors(R"(
            enum Slot { Value(ptr<i32>), Empty }
            fn invalid(pointer: ptr<i32>) {
                var slot: Slot = .Value(pointer);
                match slot {
                    .Value(ref outer) if outer != nullptr => {
                        match slot {
                            .Value(&inner) => { inner = nullptr; },
                            .Empty => {},
                        }
                        let observed = *outer;
                    },
                    _ => {},
                }
            }
        )"),
            DiagnosticCode::PointerNonNull
        );
        static_cast<void>(analyze_test_program(R"(
            enum Slot { Value(ptr<i32>), Empty }
            fn accepted(pointer: ptr<i32>) {
                var slot: Slot = .Value(pointer);
                match slot {
                    .Value(ref outer) if outer != nullptr => {
                        match slot {
                            .Value(&inner) => { inner = nullptr; },
                            .Empty => {},
                        }
                        if outer != nullptr { let observed = *outer; }
                    },
                    _ => {},
                }
            }
        )"));
    };

    "Borrowed payload nullability: binding another Read alias preserves observed facts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Slot { Value(ptr<i32>), Empty }
                fn accepted(pointer: ptr<i32>) {
                    var slot: Slot = .Value(pointer);
                    match slot {
                        .Value(ref outer) if outer != nullptr => {
                            match slot {
                                .Value(ref inner) => { let observed = *outer; },
                                .Empty => {},
                            }
                            let observed = *outer;
                        },
                        _ => {},
                    }
                }
            )"));
        };
});

} // namespace
