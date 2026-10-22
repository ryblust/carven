module carven:test.internal.semantic.analysis.generics;

import :diagnostics.code;
import :semantic.analysis.construction.limits;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generic closures: ownership requirements apply before concrete application"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
fn inspect<T>(value: T) {}
fn invalid<T>(value: T) {
    let callback = [value]() { inspect(value); };
}
)"),
                DiagnosticCode::TypeGenericDefinition
            );
            expect_diagnostic(
                analyze_test_errors(R"(
fn invalid<T>(&value: T) {
    let callback = [&value]() { let _ = &&value; };
}
)"),
                DiagnosticCode::AccessTakeOperand
            );
            expect_diagnostic(
                analyze_test_errors(R"(
fn touch<T>(&value: T) {}
fn invalid<T>(&&value: T) {
    var local = &&value;
    return [&local]() { touch(&local); };
}
)"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Generic capabilities: unknown and unsupported operands require declared evidence"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
                    fn invalid<T>(value: T) -> u64 => Hash::hash(value);
                )"),
                DiagnosticCode::TypeGenericArguments
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                    fn hash<T: Hash>(value: T) -> u64 => Hash::hash(value);
                    fn invalid() -> u64 => hash<f64>(1.0);
                )"),
                DiagnosticCode::TypeGenericArguments
            );
        };
    "Generic static calls: canonical environments close completed operation dependencies"_test =
        [] static noexcept {
            for (const auto initializer : std::array<std::string_view, 3> {
                     "await relay(&&produce())",
                     "await consume(&&produce())",
                     "await consume<i32, throw ()>(&&produce())",
                 }) {
                const auto program = analyze_test_program(
                    std::format(
                        R"(import std::async using Operation;

                    const async fn produce() -> i32 => 42;
                    const fn relay<T>(&&value: T) -> T => &&value;
                    const async fn consume<T, throw E>(&&operation: Operation<T, throw E>) -> T throw E {{
                        return (await operation)?;
                    }}
                    const result = {};
                )",
                        initializer
                    )
                );
                require_equal(program.declarations().module_constants().size(), 1uz);
                for (const auto [id, declaration] : program.declarations().module_constants()) {
                    static_cast<void>(id);
                    const auto* value = std::get_if<IntegerConstant>(
                        &program.constants().constant(declaration.value).value
                    );
                    require(value != nullptr);
                    expect_equal(value->as_signed(), 42ll);
                }
            }
        };
    "Generic effects: operation contracts carry an independent inferred failure set"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(import std::async using Operation;

                struct Error { code: i32 }
                struct Holder<T, throw E> { pending: Operation<T, throw E> }
                fn forward<T, throw E>(&&operation: Operation<T, throw E>) -> Operation<T, throw E> => &&operation;
                fn hold<T, throw E>(&&operation: Operation<T, throw E>) -> Holder<T, throw E> {
                    return Holder<T, throw E> { pending: &&operation };
                }
                fn release<T, throw E>(&&holder: Holder<T, throw E>) -> Operation<T, throw E> => (&&holder).pending;
                async fn success() -> i32 => 42;
                async fn fallible() -> i32 throw Error => 42;
                async fn consume<T, throw E>(&&operation: Operation<T, throw E>) -> T throw E {
                    return (await operation)?;
                }
                async fn use() -> i32 throw Error {
                    let empty = success();
                    let owned = forward(&&empty);
                    let value = await consume(&&owned);
                    let pending = fallible();
                    let holder = hold<i32, throw Error>(&&pending);
                    let next = release(&&holder);
                    return value + (await consume(&&next))?;
                }
            )"));
        };
    "Generic effects: parameter kinds are checked at their source positions"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
                fn invalid<T>() throw T {}
            )"),
                DiagnosticCode::TypeGenericArguments
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                fn invalid<throw E>(value: E) {}
            )"),
                DiagnosticCode::TypeGenericDefinition
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                struct Error {}
                fn identity<T>(&&value: T) -> T => &&value;
                fn invalid() { var value = 1; let _ = identity<throw Error>(&&value); }
            )"),
                DiagnosticCode::TypeGenericArguments
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                struct Error {}
                fn unused<throw E>() {}
                fn invalid() { unused<Error>(); }
            )"),
                DiagnosticCode::TypeGenericArguments
            );
        };
    "Generic effects: independent producer equations infer one shared contract"_test =
        [] static noexcept {
            const auto shared = std::string(R"(import std::async using Operation;

                struct Error {}
                struct OtherError {}
                fn discard<T, throw E>(&&left: Operation<T, throw E>, &&right: Operation<T, throw E>) {}
                const async fn first() -> i32 => 42;
                const async fn second() -> i32 => 7;
                async fn fallible() -> i32 throw Error => 42;
                async fn also_fallible() -> i32 throw Error => 7;
                async fn different() -> i32 throw OtherError => 7;
                const async fn both<T, throw E>(&&left: Operation<T, throw E>, &&right: Operation<T, throw E>) -> T throw E {
                    let value = (await left)?;
                    let _ = (await right)?;
                    return &&value;
                }
            )");
            static_cast<void>(analyze_test_program(shared + R"(
                fn use() {
                    discard(&&first(), &&second());
                    discard(&&fallible(), &&also_fallible());
                }
                const answer = await both(&&first(), &&second());
            )"));
            expect_diagnostic(
                analyze_test_errors(shared + R"(
                fn invalid() { discard(&&fallible(), &&different()); }
            )"),
                DiagnosticCode::TypeMismatch
            );
        };
    "Generic effects: transparent inferred producers preserve recursive effect environments"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(import std::async using Operation;

                private async fn relay<throw F>(&&operation: Operation<i32, throw F>) -> i32 {
                    return (await operation)?;
                }
                private async fn recur<throw E>(&&operation: Operation<i32, throw E>, again: bool) -> i32 {
                    if again { return (await recur(&&relay(&&operation), false))?; }
                    return (await operation)?;
                }
                async fn produce() -> i32 => 42;
                async fn use() -> i32 => await recur(&&produce(), true);
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                fn checked<T>(pointer: ptr<T>) -> void {
                    if pointer != nullptr { let _ = addressof(*pointer); }
                }
                fn recur<T>(pointer: ptr<T>, again: bool) -> void {
                    checked<T>(pointer);
                    if again { recur<void>(nullptr, false); }
                }
            )"),
                DiagnosticCode::TypeValueRequired
            );
        };
    "Generic type formation: dereference requirements follow checked call edges"_test =
        [] static noexcept {
            const auto checked = std::string(R"(
                fn checked<T>(pointer: ptr<T>) -> bool {
                    if pointer != nullptr { let selected = addressof(*pointer); return selected != nullptr; }
                    return false;
                }
                fn wrap<U>(pointer: ptr<U>) -> bool => checked<U>(pointer);
            )");
            static_cast<void>(analyze_test_program(checked + R"(
                fn valid(pointer: ptr<i32>) -> bool => wrap<i32>(pointer);
                fn identity<T>(pointer: ptr<T>) -> ptr<T> => pointer;
                fn null_address() -> ptr<void> => identity<void>(nullptr);
            )"));
            for (const auto call : std::array<std::string_view, 3> {
                     "fn invalid(pointer: ptr<void>) -> bool => checked<void>(pointer);",
                     "fn unused<V>(pointer: ptr<void>) -> bool => checked<void>(pointer);",
                     "fn unused<V>(pointer: ptr<void>) -> bool => wrap<void>(pointer);",
                 }) {
                expect_diagnostic(
                    analyze_test_errors(checked + std::string(call)),
                    DiagnosticCode::TypeValueRequired
                );
            }
            expect_diagnostic(
                analyze_test_errors(R"(
                fn first<T>(pointer: ptr<T>, repeat: bool) -> bool {
                    if repeat { return second<T>(pointer, false); }
                    if pointer != nullptr { let selected = addressof(*pointer); return selected != nullptr; }
                    return false;
                }
                fn second<U>(pointer: ptr<U>, repeat: bool) -> bool => first<U>(pointer, repeat);
                fn unused<V>(pointer: ptr<void>) -> bool => second<void>(pointer, false);
            )"),
                DiagnosticCode::TypeValueRequired
            );
            for (const auto source : std::array<std::string_view, 2> {
                     "fn growing<T>() -> bool => growing<ptr<T>>();",
                     R"(
                         fn growing<T>(&&value: T) -> void {
                             let next = [](&&item: T) {};
                             growing(&&next);
                         }
                     )",
                 }) {
                expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::TypeGenericExpansion
                );
            }
        };
    "Generic expressions: ordinary pointer and scalar contexts retain their contracts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                fn sum<T>(left: i64, right: i64) -> i64 => left + right;
                fn narrow<T>(pointer: ptr<&T>) -> ptr<T> => pointer;
                fn address<T>(&value: T) -> ptr<&T> => addressof(&value);
                fn cleared<T>() -> bool {
                    let pointer: ptr<T> = nullptr;
                    return pointer == nullptr;
                }
            )"));
        };
    "Generic descriptors: unused bodies can hold a rigid value beside a cold operation"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                async fn produce() -> i32 => 42;
                fn assemble<T>(&&value: T) {
                    let product = (&&value, produce());
                    let _ = &&product;
                }
            )"));
        };
    "Generic functions: rigid definitions preserve access and backing relationships"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
                    private fn twice<T>(&&value: T) -> T {
                        let first = &&value;
                        return &&value;
                    }
                )"),
                DiagnosticCode::AccessUnavailable
            );
            expect_diagnostic(
                analyze_test_errors(R"(
                    private fn relay<T>(&&value: T) -> T => &&value;
                    fn run() {
                        let data = [1, 2];
                        let view = data.slice();
                        let forwarded = relay<[i32]>(&&view);
                        let owner = &&data;
                        print(forwarded[0]);
                    }
                )"),
                DiagnosticCode::AccessBorrowConflict
            );
            const auto diagnostics = analyze_test_errors(R"(
                private fn relay<T>(&&value: T) -> T => &&value;
                fn run() {
                    let data = [1, 2];
                    let view = data.slice();
                    if true {
                        let forwarded = relay<[i32]>(&&view);
                        print(forwarded[0]);
                    }
                    let owner = &&data;
                    print(owner[1]);
                }
            )");
            expect(diagnostics.empty());
        };

    "Generic definitions: unused heads enforce their type and storage contracts"_test = [] static noexcept {
        constexpr auto invalid = std::array {
            std::pair {"struct Box<T, T> { value: T }", DiagnosticCode::TypeGenericDefinition},
            std::pair {
                "struct Pair<T, U> {} struct Box<T> { value: Pair<T> }",
                DiagnosticCode::TypeGenericArguments
            },
            std::pair {"struct Box<T> { value: Box<T> }", DiagnosticCode::TypeRecursiveStorage},
            std::pair {
                "struct Box<T> { next: ptr<Box<[T; 1]>> }",
                DiagnosticCode::TypeGenericExpansion
            },
            std::pair {
                "fn identity<T>(value: T) -> T => value;",
                DiagnosticCode::TypeGenericDefinition
            },
            std::pair {
                "class Box<T> { value: T, fn read(self) -> T => self.value; }",
                DiagnosticCode::TypeGenericDefinition
            },
            std::pair {"enum Code<T> { Ready, Failed }", DiagnosticCode::TypeGenericDefinition},
            std::pair {
                "enum Box<T> { Hold(T) } fn wrap<T>(value: T) -> Box<T> { let create = Box<T>::Hold; return create(value); }",
                DiagnosticCode::TypeGenericDefinition
            },
            std::pair {
                "enum Maybe<T> { None, Some(T) } fn inspect<T>(value: Maybe<T>) { match value { .Some(_) => {} } }",
                DiagnosticCode::MatchNonExhaustive
            },
        };
        for (const auto& [text, code] : invalid) {
            expect_diagnostic(analyze_test_errors(text), code);
        }
        const auto accepted = analyze_test_errors(R"(
            struct Node<T> { value: T, next: ptr<Node<T>> }
            struct Left<T, U> { next: ptr<Right<U, T>> }
            struct Right<T, U> { next: ptr<Left<U, T>> }
            struct VoidPointer<T> { value: ptr<void> }
        )");
        expect(accepted.empty());
    };

    "Generic tuples: symbolic products retain unit slots and enforce storage boundaries"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                struct Box<T> { pair: (T, void), empty: () }
                enum Maybe<T> { None, Some((T, void)) }
                struct Tree<T> { pair: (T, Vector<Tree<T>>) }
                fn unit(value: Box<void>) -> Box<void> => value;
                fn pair(value: Box<i32>) -> (i32, void) => value.pair;
                fn nested(value: Box<(i32, void)>) -> Box<(i32, void)> => value;
                fn tree(value: Tree<(i32, void)>) -> Tree<(i32, void)> => value;
                fn maybe(value: Maybe<i32>) -> Maybe<i32> => value;
            )"));
            for (const auto text : {
                     "struct Box<T> { pair: (T, Box<T>) }",
                     "struct Box<T> { pair: (T, Box<T>, void) }",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::TypeRecursiveStorage);
            }
            expect_diagnostic(
                analyze_test_errors("struct Box<T> { next: ptr<Box<(T, void)>> }"),
                DiagnosticCode::TypeGenericExpansion
            );
            expect_diagnostic(
                analyze_test_errors("struct Box<T> { pair: (T, fn() -> void) }"),
                DiagnosticCode::TypeCallableViewEscape
            );
        };

    "Generic functions: array and slice construction keep rigid element shapes"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                fn inspect<T>(values: [T; 1]) -> usize {
                    let slice = values.slice();
                    return slice.length();
                }
                fn empty<T>() -> usize {
                    let values: [T] = [];
                    return values.length();
                }
                fn nested<T>(values: [[T; 1]; 1]) -> usize {
                    let slice = values.slice();
                    return slice.length();
                }
            )"));
        };

    "Generic functions: cold calls retain unresolved effects outside canonical types"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                async fn relay<T>(&&value: T) -> T => &&value;
                async fn forward<T>(&&value: T) -> T => await relay(&&value);
            )"));
        };

    "Generic instances: nested fields retain their declaring argument environment"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
            struct Pair<T, U> { first: T, second: U }
            struct Group<T, U> {
                values: Pair<T, U>,
                swapped: Pair<U, T>,
                repeated: Pair<T, T>,
                nested: Pair<Pair<T, U>, Pair<U, T>>,
                arrays: Pair<[T; 2], [U; 3]>,
                views: Pair<[T], ptr<&U>>,
                sequence: Vector<Pair<T, U>>,
                next: ptr<Group<T, U>>,
            }
            fn values(value: Group<i32, u8>) -> Pair<i32, u8> => value.values;
            fn swapped(value: Group<i32, u8>) -> Pair<u8, i32> => value.swapped;
            fn repeated(value: Group<i32, u8>) -> Pair<i32, i32> => value.repeated;
            fn nested(value: Group<i32, u8>) -> Pair<Pair<i32, u8>, Pair<u8, i32>> => value.nested;
            fn arrays(value: Group<i32, u8>) -> Pair<[i32; 2], [u8; 3]> => value.arrays;
            fn views(value: Group<i32, u8>) -> Pair<[i32], ptr<&u8>> => value.views;
            fn sequence(value: Group<i32, u8>) -> Vector<Pair<i32, u8>> => value.sequence;
            fn next(value: Group<i32, u8>) -> ptr<Group<i32, u8>> => value.next;
        )"));
        };

    "Callable views: canonical signatures obey adoption subsets and storage invariance"_test =
        [] static noexcept {
            const auto prelude = std::string(R"(
            struct FirstFailure {}
            struct SecondFailure {}
            struct Pointer<T> { value: ptr<&T> }
            fn source(value: i32) -> i32 throw FirstFailure => value;
            fn narrow(callback: fn(i32) -> i32 throw FirstFailure) { let _ = callback; }
            fn wide(&callback: fn(i32) -> i32 throw FirstFailure + SecondFailure) { let _ = callback; }
        )");
            expect(analyze_test_errors(prelude + R"(
            fn accepted() {
                let narrow: fn(i32) -> i32 throw FirstFailure = source;
                var callback: fn(i32) -> i32 throw FirstFailure + SecondFailure = source;
                let pointer = Pointer<fn(i32) -> i32 throw FirstFailure + SecondFailure> { value: addressof(&callback) };
                if pointer.value != nullptr {
                    callback = narrow;
                    let view: fn(i32) -> i32 throw FirstFailure + SecondFailure = callback;
                    let _ = view;
                }
                var inferred: fn(i32) -> i32 = [](value) => value + 1;
                let inferred_pointer = Pointer<fn(i32) -> i32> { value: addressof(&inferred) };
                inferred = [](value) => value + 2;
                if inferred_pointer.value != nullptr { let _ = inferred_pointer.value; }
                var builtin: fn(bool) -> void = assert;
                let builtin_pointer = Pointer<fn(bool) -> void> { value: addressof(&builtin) };
                builtin = assert;
                if builtin_pointer.value != nullptr { let _ = builtin_pointer.value; }
            }
        )")
                       .empty());
            expect_diagnostic(
                analyze_test_errors(prelude + R"(
            fn rejected() {
                var callback: fn(i32) -> i32 throw FirstFailure + SecondFailure = source;
                narrow(callback);
            }
        )"),
                DiagnosticCode::TypeMismatch
            );
            expect_diagnostic(
                analyze_test_errors(prelude + R"(
            fn rejected() {
                var callback: fn(i32) -> i32 throw FirstFailure = source;
                wide(&callback);
            }
        )"),
                DiagnosticCode::TypeMismatch
            );
        };

    "Generic pointer targets: indirect callable assignment preserves the borrow boundary"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            struct Pointer<T> { value: ptr<&T> }
            fn identity(value: i32) -> i32 => value;
            fn rejected() {
                var callback: fn(i32) -> i32 = identity;
                let pointer = Pointer<fn(i32) -> i32> { value: addressof(&callback) };
                if pointer.value != nullptr { *pointer.value = identity; }
            }
        )"),
                DiagnosticCode::TypeCallableViewEscape
            );
        };

    "Generic storage: callable views propagate through values and stop at pointers"_test =
        [] static noexcept {
            constexpr auto invalid = std::array<std::string_view, 8> {
                "struct Callback<T> { callback: fn() -> void }",
                "class Callback<T> { callback: fn() -> void }",
                "enum Callback<T> { None, Call(fn() -> void) }",
                "struct Callback<T> { callbacks: [fn() -> void] }",
                "struct Callback<T> { callbacks: [fn() -> void; 1] }",
                "struct Holder<T> { value: T } struct Callback<T> { value: Holder<fn() -> void> }",
                "struct SliceHolder<T> { values: [T] } struct Callback<T> { value: SliceHolder<fn() -> void> }",
                "struct Box<T> { value: T } fn rejected(value: Box<fn(i32) -> i32>) { let _ = value; }",
            };
            for (const auto source : invalid) {
                expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::TypeCallableViewEscape
                );
            }
            expect(analyze_test_errors(R"(
            struct Tag<T> {}
            struct Pointer<T> { value: ptr<T> }
            struct Forward<T> { callbacks: ptr<fn(Payload<i32>) -> i32> }
            struct Payload<T> { value: T }
            struct Left<T> { values: [Right<T>] }
            struct Right<T> { value: Left<T> }
            fn phantom(value: Tag<fn(i32) -> i32>) { let _ = value; }
            fn pointer(value: Pointer<fn(i32) -> i32>) { let _ = value; }
        )")
                       .empty());
        };

    "Generic definitions: finite source complexity reports a budget limit"_test =
        [] static noexcept {
            auto source = std::string();
            for (auto index = 0uz; index <= maximum_generic_depth; ++index) {
                source +=
                    std::format("struct Layer{}<T> {{ value: Layer{}<T> }}\n", index, index + 1uz);
            }
            source +=
                std::format("struct Layer{}<T> {{ value: T }}\n", maximum_generic_depth + 1uz);
            expect_diagnostic(
                analyze_test_errors(std::move(source)),
                DiagnosticCode::TypeGenericLimits
            );
        };

    "Generic extents: ordinary static bodies complete referenced generic heads"_test =
        [] static noexcept {
            const auto accepted = analyze_test_errors(R"(
            struct Fixed<T> { values: [T; width()] }
            const fn width() -> usize { let box = Box<usize> { value: 2usize }; return box.value; }
            struct Box<T> { value: T }
            fn use(value: Fixed<i32>) -> i32 => value.values[0];
        )");
            expect(accepted.empty());
            const auto cycle = analyze_test_errors(R"(
            struct Fixed<T> { values: [T; width()] }
            const fn width() -> usize { let box = Fixed<usize> { values: [2usize] }; return box.values[0]; }
        )");
            expect_diagnostic(cycle, DiagnosticCode::ConstCycle);
        };

    "Generic type scopes: rigid names shadow ordinary types without shadowing values"_test =
        [] static noexcept {
            expect(analyze_test_errors(R"(
            const T = 2usize;
            struct Box<T> { values: [T; T] }
            struct Callback<U> { callback: ptr<fn(Payload<i32>) -> i32> }
            struct Payload<U> { value: U }
        )")
                       .empty());
            expect_diagnostic(
                analyze_test_errors(R"(
            enum T { One = 1 }
            struct Box<T> { values: [i32; T::One as usize] }
        )"),
                DiagnosticCode::TypeGenericDefinition
            );
        };

    "Generic surfaces: transparent fixed dependencies and phantom arguments are checked"_test =
        [] static noexcept {
            constexpr auto invalid = std::array<std::string_view, 4> {
                "private struct Hidden {} export struct Box<T> { value: Hidden }",
                "private struct Hidden {} export struct Box<T> { callback: ptr<fn(i32) -> Hidden> }",
                "private struct Hidden {} export enum Maybe<T> { None, Some(Hidden) }",
                "private struct Hidden {} export struct Tag<T> {} export fn leak(value: Tag<Hidden>) {}",
            };
            for (const auto source : invalid) {
                expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::TypeVisibilityLeak
                );
            }
            expect(analyze_test_errors(
                       "private struct Hidden {} export class Box<T> { hidden: Hidden }"
            )
                       .empty());
        };
});

} // namespace
