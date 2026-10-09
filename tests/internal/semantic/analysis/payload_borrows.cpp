module carven:test.internal.semantic.analysis.payload_borrows;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generic failure ownership: typed catch and rethrow preserve payload backing"_test =
        [] static noexcept {
            const auto declarations = std::string(R"(
                struct ViewFailure { text: str }
                fn raise<throw E>(owner: String) throw E + ViewFailure {
                    throw ViewFailure { text: owner.as_str() };
                }
                fn forward<throw E>(owner: String) throw E + ViewFailure {
                    try { raise<throw E>(owner)?; } catch {
                        ViewFailure(error) => { rethrow; },
                        _ => { rethrow; },
                    }
                }
            )");
            expect_diagnostic(
                analyze_test_errors(declarations + R"(
                fn invalid() {
                    var owner: String = "owned";
                    try { forward<throw ()>(owner)?; } catch {
                        ViewFailure(error) => { owner.clear(); print(error.text); },
                    }
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
            static_cast<void>(analyze_test_program(declarations + R"(
                fn accepted() {
                    var owner: String = "owned";
                    try { forward<throw ()>(owner)?; } catch {
                        ViewFailure(error) => { print(error.text); },
                    }
                    owner.clear();
                }
            )"));
        };

    "Pointer nullability: unused generic definitions require the ordinary proof"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
                    fn unchecked<T>(pointer: ptr<i32>) -> i32 => *pointer;
                )"),
                DiagnosticCode::PointerNonNull
            );
            static_cast<void>(analyze_test_program(R"(
                fn checked<T>(pointer: ptr<T>) -> bool {
                    if pointer != nullptr {
                        let selected = addressof(*pointer);
                        return selected != nullptr;
                    }
                    return false;
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                    fn clear<T>(&pointer: ptr<T>) { pointer = nullptr; }
                    fn invalid<T>(&pointer: ptr<T>) -> bool {
                        if pointer != nullptr {
                            clear<T>(&pointer);
                            return addressof(*pointer) != nullptr;
                        }
                        return false;
                    }
                )"),
                DiagnosticCode::PointerNonNull
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                    fn clear<T>(&pointer: ptr<T>) { pointer = nullptr; }
                    fn invalid<T>(&&value: T) {
                        var pointer = addressof(value);
                        var count = 0;
                        while count < 2 {
                            let selected = addressof(*pointer);
                            clear<T>(&pointer);
                            count += 1;
                        }
                    }
                )"),
                DiagnosticCode::PointerNonNull
            );
        };

    "Borrowed payload ownership: projections pin ancestors and end with their arm"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            enum Node { Values(Vector<String>), Empty }
            fn invalid() {
                var value: Node = .Values(Vector<String> {});
                match value {
                    .Values(&items) => { value = .Empty; },
                    .Empty => {},
                }
            }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                    enum Node { Values(Vector<String>), Empty }
                    fn invalid() {
                        var value: Node = .Values(Vector<String> {});
                        match value {
                            .Values(items) => {
                                value = .Empty;
                                let observed = items.length();
                            },
                            .Empty => {},
                        }
                    }
                )"),
                DiagnosticCode::AccessBorrowConflict
            );
            static_cast<void>(analyze_test_program(R"(
            enum Node { Values(Vector<String>), Empty }
            fn accepted() {
                var value: Node = .Values(Vector<String> {});
                match value {
                    .Values(&items) => { items.push("first" as String); },
                    .Empty => {},
                }
                value = .Empty;
            }
            fn read_arm() {
                var value: Node = .Values(Vector<String> {});
                match value {
                    .Values(items) => { let observed = items.length(); },
                    .Empty => {},
                }
                value = .Empty;
            }
        )"));
        };

    "Payload ownership: temporary match backing cannot escape through Read bindings"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(
                        enum Text { Value(String) }
                        fn invalid() -> str => match Text::Value("temporary" as String) {
                            .Value(text) => text.as_str(),
                        };
                    )",
                     R"(
                        fn invalid() -> str => match ("temporary" as String,) {
                            (text,) => text.as_str(),
                        };
                    )",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                enum Text { Value(String) }
                fn enum_copy() -> String => match Text::Value("temporary" as String) {
                    .Value(text) => text,
                };
                fn tuple_copy() -> String => match ("temporary" as String,) {
                    (text,) => text,
                };
            )"));
        };

    "Payload ownership: caught Read bindings retain their backing until handler exit"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(
                        struct TextError { text: String }
                        fn fail() throw TextError { throw TextError { text: "caught" as String }; }
                        fn invalid() -> str {
                            try { fail()?; } catch {
                                TextError(error) => { return error.text.as_str(); },
                            }
                            return "";
                        }
                    )",
                     R"(
                        enum TextError { Text(String) }
                        fn fail() throw TextError { throw TextError::Text("caught" as String); }
                        fn invalid() -> str {
                            try { fail()?; } catch {
                                TextError(.Text(text)) => { return text.as_str(); },
                            }
                            return "";
                        }
                    )",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                struct RecordError { text: String }
                enum EnumError { Text(String) }
                fn record_fail() throw RecordError {
                    throw RecordError { text: "caught" as String };
                }
                fn enum_fail() throw EnumError { throw EnumError::Text("caught" as String); }
                fn record_copy() -> String {
                    try { record_fail()?; } catch {
                        RecordError(error) => { return error.text; },
                    }
                    return "" as String;
                }
                fn enum_copy() -> String {
                    try { enum_fail()?; } catch {
                        EnumError(.Text(text)) => { return text; },
                    }
                    return "" as String;
                }
            )"));
        };

    "Payload nullability: Write aliases invalidate shared proofs"_test = [] static noexcept {
        expect_diagnostic(
            analyze_test_errors(R"(
            enum Slot { Value(ptr<i32>), Empty }
            fn invalid(pointer: ptr<i32>) {
                var slot: Slot = .Value(pointer);
                match slot {
                    .Value(&outer) if outer != nullptr => {
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
                    .Value(&outer) if outer != nullptr => {
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

    "Payload nullability: default Read snapshots survive writes to their source"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Slot { Value(ptr<i32>), Empty }
                fn accepted(pointer: ptr<i32>) {
                    var slot: Slot = .Value(pointer);
                    match slot {
                        .Value(outer) if outer != nullptr => {
                            match slot {
                                .Value(&inner) => { inner = nullptr; },
                                .Empty => {},
                            }
                            let observed = *outer;
                        },
                        _ => {},
                    }
                }
            )"));
        };

    "Payload ownership: default Read bindings cannot be Taken from a consumed subject"_test =
        [] static noexcept {
            for (const auto subject : {"value", "&&value"}) {
                for (const auto type : {"i32", "Vector<String>"}) {
                    const auto source = std::format(
                        R"(
                        enum Payload {{ Value({}) }}
                        fn invalid(&&value: Payload) {{
                            match {} {{ .Value(item) => {{ let moved = &&item; }} }}
                        }}
                    )",
                        type,
                        subject
                    );
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessTakeOperand
                    );
                }
            }
        };

    "Payload nullability: another Read snapshot preserves observed facts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Slot { Value(ptr<i32>), Empty }
                fn accepted(pointer: ptr<i32>) {
                    var slot: Slot = .Value(pointer);
                    match slot {
                        .Value(outer) if outer != nullptr => {
                            match slot {
                                .Value(inner) => { let observed = *outer; },
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
