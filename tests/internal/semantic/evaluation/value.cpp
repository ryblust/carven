module carven:test.internal.semantic.evaluation.value;

import :semantic.evaluation.display;
import :semantic.evaluation.shape;
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

const TestSuite suite([] static noexcept {
    "Constant values: unit and tuples compare across retained and local representations"_test =
        [] static noexcept {
            constexpr auto maximum_steps = std::numeric_limits<std::size_t>::max();
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto unit = values.builtin_type(BuiltinType::Void);
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto tuple =
                values.intern_type({.value = TupleTypeValue {.elements = {integer, unit}}});
            const auto unit_constant =
                values.intern_constant({.type = unit, .value = VoidConstant {}});
            const auto number =
                values.intern_constant({.type = integer, .value = IntegerConstant::from_signed(7)});
            const auto retained = values.intern_constant(
                {.type = tuple, .value = TupleConstant {.elements = {number, unit_constant}}}
            );
            auto elements = std::vector<ExecutionValue>();
            elements.push_back(number);
            elements.push_back(ExecutionVoid {});
            const auto local = ExecutionValue(
                ExecutionAggregateValue {
                    .type = tuple,
                    .elements = std::move(elements),
                    .capacity = 0uz
                }
            );
            const auto atom = execution_atom(values, ExecutionVoid {});
            if (!expect(atom.has_value())) {
                return;
            }
            expect(atom->type == unit);
            expect(std::holds_alternative<VoidConstant>(atom->value));
            auto steps = 0uz;
            expect(execution_equal(values, local, retained, steps, maximum_steps) == true);
            expect_equal(display_execution_value(values, ExecutionVoid {}, false), "()");
            const auto display = display_execution_value(values, local, false);
            if (expect(display.has_value())) {
                expect(display->starts_with("("));
                expect(display->ends_with(")"));
                expect(display->contains("()"));
            }
            const auto shape = ExecutionTypeShapes(values).get(tuple);
            if (expect(shape.has_value())) {
                expect(shape->depth == 1uz);
                expect(shape->elements == 2uz);
            }
        };

    "Constant values: text observation and equality are independent of storage representation"_test =
        [] static noexcept {
            constexpr auto maximum_steps = std::numeric_limits<std::size_t>::max();
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto bytes = std::string("a\0我", 5uz);
            const auto fact = ConstantFact {
                .type = values.builtin_type(BuiltinType::Str),
                .value = TextConstant {.value = fixture.compilation.intern_spelling(bytes)}
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
                if (expect(text.has_value()).note("index = ", index)) {
                    expect_equal(text->view(), bytes).note("index = ", index);
                }
                auto steps = 0uz;
                expect(
                    execution_equal(values, representations[index], retained, steps, maximum_steps)
                    == true
                )
                    .note("index = ", index);
            }
            const auto nontext = ExecutionValue(
                ConstantAtom {
                    .type = values.builtin_type(BuiltinType::I32),
                    .value = IntegerConstant::from_signed(1)
                }
            );
            expect(!execution_text(values, nontext).has_value());
            auto steps = 0uz;
            expect(execution_equal(values, nontext, retained, steps, maximum_steps) == false);
        };

    "Constant values: comparison distinguishes expired text, unsupported values and limits"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            auto storage = std::optional(ExecutionText(std::string("text")));
            const auto borrowed = ExecutionValue(storage->borrow());
            const auto owned = ExecutionValue(ExecutionOwnedText("text"));
            auto steps = 0uz;
            expect(execution_equal(values, borrowed, owned, steps, 1uz) == true);
            const auto exhausted = execution_equal(values, borrowed, owned, steps, 1uz);
            if (!expect(!(exhausted.has_value()))) {
                return;
            }
            expect(exhausted.error() == ExecutionComparisonFailure::StepLimit);

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
                if (!(expect(!(result.has_value())).note("reverse = ", reverse))) {
                    return;
                }
                expect(result.error() == ExecutionComparisonFailure::ExpiredText)
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
            if (!expect(!(unsupported.has_value()))) {
                return;
            }
            expect(unsupported.error() == ExecutionComparisonFailure::Unsupported);
        };

    "Execution text: shared content and String borrows have separate lifetimes"_test =
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            auto owner = ExecutionOwnedText("a");
            const auto borrowed = owner.borrow();
            // Copying the borrow is part of the lifetime contract under test.
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
            const auto copied_borrow = borrowed;
            if (!expect(execution_text(values, borrowed) == "a")) {
                return;
            }
            auto moved_owner = std::move(owner);
            expect(execution_text(values, copied_borrow) == "a");
            moved_owner.append("b");
            expect(moved_owner.bytes() == "ab");
            expect(!(execution_text(values, borrowed).has_value()));
            expect(!(execution_text(values, copied_borrow).has_value()));

            const auto before_take = moved_owner.borrow();
            moved_owner.transfer();
            expect(moved_owner.bytes() == "ab");
            expect(!(execution_text(values, before_take).has_value()));

            auto text = std::optional(ExecutionText(std::string("shared")));
            auto shared = std::optional(*text);
            const auto view = text->borrow();
            text.reset();
            expect(execution_text(values, *shared) == "shared");
            expect(execution_text(values, view) == "shared");
            shared.reset();
            expect(!(execution_text(values, view).has_value()));
        };
});

} // namespace
