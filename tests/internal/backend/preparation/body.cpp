module carven:test.internal.backend.preparation.body;

import :backend.preparation.body;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Preparation: effects and operand access belong to semantic occurrences"_test =
        [] static noexcept {
            const auto semantic = analyze_test_program(
                "struct Failure {} fn fallible() -> i32 throw Failure { return 1; } "
                "fn propagated() -> i32 throw Failure { return fallible()?; } "
                "fn touch(&n: i32) -> i32 { n += 1; return n; } "
                "fn consume(&&n: i32) {} "
                "fn probe(&n: i32) { let _ = touch(&n) + 1; let _ = 1 / n; "
                "var first = 1; var second = 2; consume(&&first); ::native_take(&&second); }"
            );
            auto propagation = false;
            auto addition = false;
            auto division = false;
            auto native = false;
            auto carven = false;
            for (const auto entry : semantic.bodies().entries()) {
                const auto preparation = BodyPreparation(semantic, entry.id);
                expect(std::addressof(preparation.body()) == std::addressof(entry.value));
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& source) noexcept {
                        const auto& expression = preparation.prepare(source);
                        if (const auto* marker = std::get_if<SemPropagate>(&source.value)) {
                            expect(
                                std::addressof(preparation.summary(source))
                                == std::addressof(preparation.summary(*marker->operand))
                            );
                            expect(
                                std::addressof(expression.operation)
                                == std::addressof(*marker->operand)
                            );
                            propagation = true;
                            return;
                        }
                        expect(std::addressof(expression.operation) == std::addressof(source));
                        const auto& inputs = expression.operands;
                        if (const auto* binary = std::get_if<SemBinary>(&source.value)) {
                            if (binary->operation == BinaryOperator::Add && !addition) {
                                expect(!(expression.executes_operation));
                                expect(expression.requires_execution);
                                expect(
                                    preparation.prepare(*inputs[0].expression).executes_operation
                                );
                                expect(inputs[0].expression == std::addressof(*binary->left));
                                expect(inputs[1].expression == std::addressof(*binary->right));
                                addition = true;
                            }
                            if (binary->operation == BinaryOperator::Divide) {
                                expect(expression.executes_operation);
                                division = true;
                            }
                        }
                        if (std::holds_alternative<SemCppCall>(source.value)) {
                            if (!expect(inputs.size() == 1uz)) {
                                return;
                            }
                            expect(inputs.front().use == PreparedUse::NativeTake);
                            native = true;
                        }
                        if (const auto* call = std::get_if<SemCall>(&source.value); call != nullptr
                            && !call->arguments.empty()
                            && call->arguments.front().access == AccessMode::Take) {
                            expect(inputs.back().use == PreparedUse::Consume);
                            carven = true;
                        }
                    }
                );
            }
            expect(propagation);
            expect(addition);
            expect(division);
            expect(native);
            expect(carven);
        };

    "Preparation: a foreign occurrence cannot acquire another body's facts"_test =
        [] static noexcept {
            const auto semantic = analyze_test_program(
                "fn first() -> i32 { return 1; } fn second() -> i32 { return 2; }"
            );
            const SemanticExpression* foreign = nullptr;
            auto first = std::optional<BodyID>();
            for (const auto entry : semantic.bodies().entries()) {
                if (!first) {
                    first = entry.id;
                    continue;
                }
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& source) noexcept {
                        foreign = std::addressof(source);
                    }
                );
            }
            if (!expect(first.has_value())) {
                return;
            }
            if (!expect(foreign != nullptr)) {
                return;
            }
            const auto preparation = BodyPreparation(semantic, *first);
            expect(expect_termination("foreign preparation occurrence", [&] noexcept {
                static_cast<void>(preparation.prepare(*foreign));
            }));
        };

    "Preparation: Read snapshots are stable while pointed-to storage remains observable"_test =
        [] static noexcept {
            const auto semantic = analyze_test_program(R"(
        struct Record { value: i32 }
        fn stable(record: Record, address: ptr<i32>, text: str, interval: range<i32>, view: [i32]) {
            let _ = record.value;
            let _ = text.len();
            let _ = interval;
            if address != nullptr { let _ = *address; }
            let _ = view[0];
        }
        fn borrowed(values: [i32; 2], owned: String, native: ::Native) {
            let _ = values;
            let _ = owned;
            let _ = native;
        }
    )");
            auto stable = 0uz;
            auto borrowed = 0uz;
            auto indirect = 0uz;
            for (const auto entry : semantic.bodies().entries()) {
                const auto preparation = BodyPreparation(semantic, entry.id);
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& source) noexcept {
                        const auto& summary = preparation.summary(source);
                        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
                            const auto& local = entry.value.binding(binding->binding);
                            const auto* parameter =
                                std::get_if<ParameterBindingStorage>(&local.storage);
                            if (parameter == nullptr) {
                                return;
                            }
                            const auto name = semantic.provenance().spelling(local.name);
                            if (name == "values" || name == "owned" || name == "native") {
                                expect(summary.reads_storage);
                                ++borrowed;
                            } else {
                                expect(!(summary.reads_storage));
                                ++stable;
                            }
                        } else if (std::holds_alternative<SemDereference>(source.value)
                                   || std::holds_alternative<SemIndex>(source.value)) {
                            expect(summary.reads_storage);
                            ++indirect;
                        } else if (std::holds_alternative<SemField>(source.value)) {
                            expect(!(summary.reads_storage));
                        }
                    }
                );
            }
            expect(stable >= 5uz);
            expect(borrowed == 3uz);
            expect(indirect == 2uz);
        };

    "Preparation: binding storage stability controls observations"_test = [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view source;
            bool observes;
        };
        const auto cases = std::array {
            Case {
                .name = "immutable owner",
                .source = "fn probe(input: i32) { let value = input; let _ = value; }",
                .observes = false
            },
            Case {
                .name = "unmodified mutable owner",
                .source = "fn probe(input: i32) { var value = input; let _ = value; }",
                .observes = false
            },
            Case {
                .name = "unexposed Take parameter",
                .source = "fn probe(&&value: i32) { let _ = value; }",
                .observes = false
            },
            Case {
                .name = "Read iteration value snapshot",
                .source = "fn probe(input: i32) { let values = [input]; "
                          "for value in values { let _ = value; } }",
                .observes = false
            },
            Case {
                .name = "Write iteration storage alias",
                .source = "fn probe(input: i32) { var values = [input]; "
                          "for &value in values { let _ = value; } }",
                .observes = true
            },
            Case {
                .name = "Read address",
                .source = "fn probe(input: i32) { let value = input; "
                          "let _ = addressof(value); let _ = value; }",
                .observes = false
            },
            Case {
                .name = "assignment",
                .source = "fn probe(input: i32) { var value = input; "
                          "value += 1; let _ = value; }",
                .observes = true
            },
            Case {
                .name = "Write argument",
                .source = "fn write(&target: i32) { target = 2; } "
                          "fn probe(input: i32) { var value = input; "
                          "write(&value); let _ = value; }",
                .observes = true
            },
            Case {
                .name = "writable address",
                .source = "fn probe(input: i32) { var value = input; "
                          "let _ = addressof(&value); let _ = value; }",
                .observes = true
            },
            Case {
                .name = "Write capture",
                .source = "fn probe(input: i32) { var value = input; "
                          "let callback = [&value]() -> i32 { value = 2; return value; }; "
                          "let _ = value; let _ = callback(); }",
                .observes = true
            },
            Case {
                .name = "projected assignment",
                .source = "struct Record { field: i32 } "
                          "fn probe(input: i32) { var value = Record { input }; "
                          "value.field = 2; let _ = value.field; }",
                .observes = true
            },
            Case {
                .name = "native Take of immutable owner",
                .source = "fn probe(input: i32) { let value = input; "
                          "let _ = value + 1; ::native_take(&&value); }",
                .observes = true
            },
            Case {
                .name = "native Take of Take parameter",
                .source = "fn probe(&&value: i32) { "
                          "let _ = value + 1; ::native_take(&&value); }",
                .observes = true
            },
            Case {
                .name = "native value",
                .source = "fn probe(input: ::Native) { let value = input; let _ = value; }",
                .observes = true
            },
        };
        each(cases, &Case::name, [](const Case& input) static noexcept {
            const auto semantic = analyze_test_program(std::string(input.source));
            auto reads = 0uz;
            for (const auto entry : semantic.bodies().entries()) {
                const auto preparation = BodyPreparation(semantic, entry.id);
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& source) noexcept {
                        const auto* binding = std::get_if<SemBinding>(&source.value);
                        if (binding == nullptr) {
                            return;
                        }
                        const auto& local = entry.value.binding(binding->binding);
                        if (semantic.provenance().spelling(local.name) != "value") {
                            return;
                        }
                        ++reads;
                        const auto& summary = preparation.summary(source);
                        expect_equal(summary.reads_storage, input.observes);
                        expect_equal(summary.requires_execution, false);
                    }
                );
            }
            expect_greater(reads, 0uz);
        });
    };
});

} // namespace
