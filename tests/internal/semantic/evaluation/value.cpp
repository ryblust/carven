module carven:test.internal.semantic.evaluation.value;

import :semantic.evaluation.admission;
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
    "Execution type admission: root permissions and views retain their storage boundaries"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto empty = values.builtin_type(BuiltinType::Void);
            const auto opaque = values.builtin_type(BuiltinType::StrCharsView);
            const auto empty_array =
                values.intern_type({.value = ArrayTypeValue {.element = empty, .extent = 1u}});
            const auto opaque_array =
                values.intern_type({.value = ArrayTypeValue {.element = opaque, .extent = 1u}});
            const auto pointer = values.intern_type(
                {.value = PointerTypeValue {.target = opaque, .access = PointerAccess::Read}}
            );
            const auto slice = values.intern_type({.value = SliceTypeValue {.element = opaque}});
            const auto construction = values.append_construction_type(
                {.value = ConstructionArrayTypeValue {
                     .element = values.builtin_type(BuiltinType::I32),
                     .extent = 1u,
                 }}
            );
            struct Scenario final {
                std::string_view name;
                ConstructionTypeRef type;
                bool allow_void;
                bool supported;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "void requires root permission",
                    .type = empty,
                    .allow_void = false,
                    .supported = false
                },
                Scenario {
                    .name = "void root permission",
                    .type = empty,
                    .allow_void = true,
                    .supported = true
                },
                Scenario {
                    .name = "root permission does not reach owned array element",
                    .type = empty_array,
                    .allow_void = true,
                    .supported = false
                },
                Scenario {
                    .name = "unsupported owned root",
                    .type = opaque,
                    .allow_void = false,
                    .supported = false
                },
                Scenario {
                    .name = "unsupported owned array element",
                    .type = opaque_array,
                    .allow_void = false,
                    .supported = false
                },
                Scenario {
                    .name = "pointer does not own target storage",
                    .type = pointer,
                    .allow_void = false,
                    .supported = true
                },
                Scenario {
                    .name = "slice does not own element storage",
                    .type = slice,
                    .allow_void = false,
                    .supported = true
                },
                Scenario {
                    .name = "unresolved construction is not canonical admission",
                    .type = construction,
                    .allow_void = true,
                    .supported = false
                },
            };
            each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                expect_equal(
                    supported_execution_type(values, scenario.type, scenario.allow_void),
                    scenario.supported
                );
            });
        };

    "Constant values: text observation and equality are independent of storage representation"_test =
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
                if (!(expect(text.has_value()).note("index = ", index))) {
                    return;
                }
                expect(*text == bytes).note("index = ", index);
                auto steps = 0uz;
                expect(
                    execution_equal(values, representations[index], retained, steps, 1uz) == true
                )
                    .note("index = ", index);
                expect(steps == 1uz).note("index = ", index);
            }
            const auto nontext = ExecutionValue(
                ConstantAtom {
                    .type = values.builtin_type(BuiltinType::I32),
                    .value = IntegerConstant::from_signed(1)
                }
            );
            expect(!(execution_text(values, nontext).has_value()));
            auto steps = 0uz;
            expect(execution_equal(values, nontext, retained, steps, 1uz) == false);
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
            auto owner = ExecutionOwnedText("a");
            const auto borrowed = owner.borrow();
            // Copying the borrow is part of the lifetime contract under test.
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
            const auto copied_borrow = borrowed;
            if (!expect(borrowed.bytes() == "a")) {
                return;
            }
            auto moved_owner = std::move(owner);
            expect(copied_borrow.bytes() == "a");
            moved_owner.append("b");
            expect(moved_owner.bytes() == "ab");
            expect(!(borrowed.bytes().has_value()));
            expect(!(copied_borrow.bytes().has_value()));

            const auto before_take = moved_owner.borrow();
            moved_owner.transfer();
            expect(moved_owner.bytes() == "ab");
            expect(!(before_take.bytes().has_value()));

            auto text = std::optional(ExecutionText(std::string("shared")));
            auto shared = std::optional(*text);
            const auto view = text->borrow();
            text.reset();
            expect(shared->bytes() == "shared");
            expect(view.bytes() == "shared");
            shared.reset();
            expect(!(view.bytes().has_value()));
        };
});

} // namespace
