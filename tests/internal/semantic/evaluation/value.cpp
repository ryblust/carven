module carven:test.internal.semantic.evaluation.value;

import :semantic.evaluation.value;
import :semantic.semir.delegation;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.semantic.evaluation.fixture;
import std;

static_assert(!std::is_copy_constructible_v<ExecutionValue>);
static_assert(!std::is_copy_assignable_v<ExecutionValue>);
static_assert(!std::is_copy_constructible_v<ExecutionOwnedText>);

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Constant values: text observation and equality are independent of storage representation",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto bytes = std::string("a\0我", 5uz);
            const auto fact = ConstantFact {
                .type = values.builtin_type(BuiltinType::Str),
                .value = StringConstant {.value = fixture.compilation.intern_spelling(bytes)}
            };
            const auto retained = fixture.compilation.intern_constant(fact);
            const auto representations = std::array<ExecutionValue, 4> {
                retained,
                *constant_atom(fact),
                ExecutionText(bytes),
                ExecutionOwnedText(bytes)
            };
            for (auto index = 0uz; index < representations.size(); ++index) {
                const auto text = execution_text(values, representations[index]);
                if (!(ct::expect(text.has_value()).note("index = ", index))) {
                    return;
                }
                ct::expect(*text == bytes).note("index = ", index);
                auto steps = 0uz;
                ct::expect(
                    execution_equal(values, representations[index], retained, steps, 1uz) == true
                )
                    .note("index = ", index);
                ct::expect(steps == 1uz).note("index = ", index);
            }
            const auto nontext = ExecutionValue(
                ConstantAtom {
                    .type = values.builtin_type(BuiltinType::I32),
                    .value = IntegerConstant::from_signed(1)
                }
            );
            ct::expect(!(execution_text(values, nontext).has_value()));
            auto steps = 0uz;
            ct::expect(execution_equal(values, nontext, retained, steps, 1uz) == false);
        }
    );

    ct::test(
        "Constant values: comparison distinguishes expired text, unsupported values and limits",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            auto storage = std::optional(ExecutionText(std::string("text")));
            const auto borrowed = ExecutionValue(storage->borrow());
            const auto owned = ExecutionValue(ExecutionOwnedText("text"));
            auto steps = 0uz;
            ct::expect(execution_equal(values, borrowed, owned, steps, 1uz) == true);
            const auto exhausted = execution_equal(values, borrowed, owned, steps, 1uz);
            if (!ct::expect(!(exhausted.has_value()))) {
                return;
            }
            ct::expect(exhausted.error() == ExecutionComparisonFailure::StepLimit);

            storage.reset();
            for (const auto reverse : {false, true}) {
                steps = 0uz;
                const auto result = execution_equal(
                    values,
                    reverse ? owned : borrowed,
                    reverse ? borrowed : owned,
                    steps,
                    1uz
                );
                if (!(ct::expect(!(result.has_value())).note("reverse = ", reverse))) {
                    return;
                }
                ct::expect(result.error() == ExecutionComparisonFailure::ExpiredText)
                    .note("reverse = ", reverse);
            }

            const auto cstring = ExecutionValue(
                ConstantAtom {
                    .type = fixture.compilation.intern_type(
                        {.value = CppTypeValue {.form = CppConstCharPointerType {}}}
                    ),
                    .value = CStringConstant {.value = fixture.compilation.intern_spelling("text")}
                }
            );
            steps = 0uz;
            const auto unsupported = execution_equal(values, cstring, cstring, steps, 1uz);
            if (!ct::expect(!(unsupported.has_value()))) {
                return;
            }
            ct::expect(unsupported.error() == ExecutionComparisonFailure::Unsupported);
        }
    );

    ct::test(
        "Execution text: shared content and String borrows have separate lifetimes",
        [] static noexcept {
            auto owner = ExecutionOwnedText("a");
            const auto borrowed = owner.borrow();
            // Copying the borrow is part of the lifetime contract under test.
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
            const auto copied_borrow = borrowed;
            if (!ct::expect(borrowed.bytes() == "a")) {
                return;
            }
            auto moved_owner = std::move(owner);
            ct::expect(copied_borrow.bytes() == "a");
            moved_owner.append("b");
            ct::expect(moved_owner.bytes() == "ab");
            ct::expect(!(borrowed.bytes().has_value()));
            ct::expect(!(copied_borrow.bytes().has_value()));

            const auto before_take = moved_owner.borrow();
            moved_owner.transfer();
            ct::expect(moved_owner.bytes() == "ab");
            ct::expect(!(before_take.bytes().has_value()));

            auto text = std::optional(ExecutionText(std::string("shared")));
            auto shared = std::optional(*text);
            const auto view = text->borrow();
            text.reset();
            ct::expect(shared->bytes() == "shared");
            ct::expect(view.bytes() == "shared");
            shared.reset();
            ct::expect(!(view.bytes().has_value()));
        }
    );
});

} // namespace
