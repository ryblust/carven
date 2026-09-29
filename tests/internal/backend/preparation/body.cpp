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

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Preparation: effects and operand access belong to semantic occurrences",
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
                ct::expect(std::addressof(preparation.body()) == std::addressof(entry.value));
                visit_semantic_nodes(
                    entry.value.region(),
                    [&](const SemanticExpression& source) noexcept {
                        const auto& expression = preparation.prepare(source);
                        if (const auto* marker = std::get_if<SemPropagate>(&source.value)) {
                            ct::expect(
                                std::addressof(preparation.summary(source))
                                == std::addressof(preparation.summary(*marker->operand))
                            );
                            ct::expect(
                                std::addressof(expression.operation)
                                == std::addressof(*marker->operand)
                            );
                            propagation = true;
                            return;
                        }
                        ct::expect(std::addressof(expression.operation) == std::addressof(source));
                        const auto& inputs = expression.operands;
                        if (const auto* binary = std::get_if<SemBinary>(&source.value)) {
                            if (binary->operation == BinaryOperator::Add && !addition) {
                                ct::expect(!(expression.executes_operation));
                                ct::expect(expression.requires_execution);
                                ct::expect(
                                    preparation.prepare(*inputs[0].expression).executes_operation
                                );
                                ct::expect(inputs[0].expression == std::addressof(*binary->left));
                                ct::expect(inputs[1].expression == std::addressof(*binary->right));
                                addition = true;
                            }
                            if (binary->operation == BinaryOperator::Divide) {
                                ct::expect(expression.executes_operation);
                                division = true;
                            }
                        }
                        if (std::holds_alternative<SemCppCall>(source.value)) {
                            if (!ct::expect(inputs.size() == 1uz)) {
                                return;
                            }
                            ct::expect(inputs.front().use == PreparedUse::NativeTake);
                            native = true;
                        }
                        if (const auto* call = std::get_if<SemCall>(&source.value); call != nullptr
                            && !call->arguments.empty()
                            && call->arguments.front().access == AccessMode::Take) {
                            ct::expect(inputs.back().use == PreparedUse::Consume);
                            carven = true;
                        }
                    }
                );
            }
            ct::expect(propagation);
            ct::expect(addition);
            ct::expect(division);
            ct::expect(native);
            ct::expect(carven);
        }
    );

    ct::test(
        "Preparation: a foreign occurrence cannot acquire another body's facts",
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
            if (!ct::expect(first.has_value())) {
                return;
            }
            if (!ct::expect(foreign != nullptr)) {
                return;
            }
            const auto preparation = BodyPreparation(semantic, *first);
            ct::expect(expect_termination("foreign preparation occurrence", [&] noexcept {
                static_cast<void>(preparation.prepare(*foreign));
            }));
        }
    );

    ct::test(
        "Preparation: Read snapshots are stable while pointed-to storage remains observable",
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
                                ct::expect(summary.reads_storage);
                                ++borrowed;
                            } else {
                                ct::expect(!(summary.reads_storage));
                                ++stable;
                            }
                        } else if (std::holds_alternative<SemDereference>(source.value)
                                   || std::holds_alternative<SemIndex>(source.value)) {
                            ct::expect(summary.reads_storage);
                            ++indirect;
                        } else if (std::holds_alternative<SemField>(source.value)) {
                            ct::expect(!(summary.reads_storage));
                        }
                    }
                );
            }
            ct::expect(stable >= 5uz);
            ct::expect(borrowed == 3uz);
            ct::expect(indirect == 2uz);
        }
    );
});

} // namespace
