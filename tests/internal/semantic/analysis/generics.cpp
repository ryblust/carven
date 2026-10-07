module carven:test.internal.semantic.analysis.generics;

import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.analysis.construction.limits;
import :semantic.semir.generic;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto analyze_modules(
    std::span<const std::pair<std::string_view, std::string_view>> modules
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto sources = SourceManager();
    auto inputs = std::vector<SourceModuleInput>();
    for (const auto& [name, text] : modules) {
        const auto id = sources.append_virtual(std::format("{}.cv", name), std::string(text));
        const auto path = CanonicalModulePath::from_value(name);
        require(id.has_value());
        require(path.has_value());
        inputs.push_back({.source_id = *id, .module_path = *path});
    }
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    require(parsed.has_value());
    return analyze(std::move(*parsed));
}

const TestSuite suite([] static noexcept {
    "Generic definitions: unused heads enforce their type and storage contracts"_test =
        [] static noexcept {
            const auto invalid = std::array {
                std::pair {"struct Box<T, T> { value: T }", DiagnosticCode::TypeGenericDefinition},
                std::pair {"struct Box<T> { value: void }", DiagnosticCode::TypeValueRequired},
                std::pair {"enum Maybe<T> { None, Some(void) }", DiagnosticCode::TypeValueRequired},
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

    "Generic instances: normalized concrete arguments share one published identity"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Box<T> { value: T }
            fn first(value: Box<[i32; 1 + 1]>) -> Box<[i32; 2]> => value;
            fn second(value: Box<[i32; 2]>) -> Box<[i32; 1 + 1]> => value;
            fn distinct(value: Box<u32>) -> Box<u32> => value;
        )");
            const auto instances = program.generic_nominal_instances();
            if (!expect(instances.size() == 2uz)) {
                return;
            }
            expect(instances[0].definition == instances[1].definition);
            expect(instances[0].arguments != instances[1].arguments);
            expect(instances[0].declaration != instances[1].declaration);
            for (const auto& instance : instances) {
                expect(program.generic_nominal_instance(instance.declaration) == &instance);
            }
        };

    "Generic instances: nested fields retain their declaring argument environment"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Pair<T, U> { first: T, second: U }
            struct Group<T, U> {
                values: Pair<T, U>,
                swapped: Pair<U, T>,
                repeated: Pair<T, T>,
                nested: Pair<Pair<T, U>, Pair<U, T>>,
                arrays: Pair<[T; 2], [U; 3]>,
                views: Pair<[T], ptr<&U>>,
                sequence: Sequence<Pair<T, U>>,
                next: ptr<Group<T, U>>,
            }
            fn values(value: Group<i32, u8>) -> Pair<i32, u8> => value.values;
            fn swapped(value: Group<i32, u8>) -> Pair<u8, i32> => value.swapped;
            fn repeated(value: Group<i32, u8>) -> Pair<i32, i32> => value.repeated;
            fn nested(value: Group<i32, u8>) -> Pair<Pair<i32, u8>, Pair<u8, i32>> => value.nested;
            fn arrays(value: Group<i32, u8>) -> Pair<[i32; 2], [u8; 3]> => value.arrays;
            fn views(value: Group<i32, u8>) -> Pair<[i32], ptr<&u8>> => value.views;
            fn sequence(value: Group<i32, u8>) -> Sequence<Pair<i32, u8>> => value.sequence;
            fn next(value: Group<i32, u8>) -> ptr<Group<i32, u8>> => value.next;
        )");
            // Explicit result types and substituted fields select the same seven nominals.
            expect_equal(program.generic_nominal_instances().size(), 7uz);
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

    "Generic pointer targets: indirect callable Read preserves the borrow boundary"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            struct Pointer<T> { value: ptr<T> }
            fn identity(value: i32) -> i32 => value;
            fn rejected() {
                let callback: fn(i32) -> i32 = identity;
                let pointer = Pointer<fn(i32) -> i32> { value: addressof(callback) };
                if pointer.value != nullptr { let result = (*pointer.value)(1); let _ = result; }
            }
        )"),
                DiagnosticCode::TypeCallableViewEscape
            );
        };

    "Generic storage: callable views propagate through values and stop at pointers"_test =
        [] static noexcept {
            const auto invalid = std::array<std::string_view, 8> {
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
            struct T {}
            struct Box<T> { callback: fn(T) -> T }
        )"),
                DiagnosticCode::TypeGenericDefinition
            );
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
            const auto invalid = std::array<std::string_view, 4> {
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

    "Generic modules: application audience is independent of the first caller"_test =
        [] static noexcept {
            const auto modules = std::array {
                std::pair<std::string_view, std::string_view> {
                    "shared",
                    "export struct Box<T> { value: T } export enum Maybe<T> { None, Some(T) }"
                },
                std::pair<std::string_view, std::string_view> {"first", R"(
                import shared using { Box, Maybe };
                private struct Local { value: i32 }
                private fn local(value: Box<Local>) -> Box<Local> => value;
                fn integer(value: Box<i32>) -> Box<i32> => value;
                fn some() -> Maybe<i32> => Maybe<i32>::Some(1);
            )"},
                std::pair<std::string_view, std::string_view> {
                    "second",
                    "import shared using Box; fn integer(value: Box<i32>) -> Box<i32> => value;"
                },
            };
            auto result = analyze_modules(modules);
            if (!expect(result.has_value())) {
                return;
            }
            const auto& program = result->value;
            expect(program.generic_nominal_instances().size() == 3uz);
            auto box_i32_count = 0uz;
            for (const auto& instance : program.generic_nominal_instances()) {
                const auto& contract = program.generic_declaration_contract(instance.definition);
                const auto* builtin = instance.arguments.size() == 1uz
                    ? std::get_if<BuiltinTypeValue>(
                          &program.types().type(instance.arguments.front()).value
                      )
                    : nullptr;
                if (program.provenance().spelling(contract.name) == "Box"
                    && builtin
                    && builtin->kind == BuiltinType::I32) {
                    ++box_i32_count;
                }
            }
            expect(box_i32_count == 1uz);
        };
});

} // namespace
