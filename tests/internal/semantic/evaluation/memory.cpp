module carven:test.internal.semantic.evaluation.memory;

import :semantic.evaluation.display;
import :semantic.evaluation.memory;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.semantic.evaluation.fixture;
import std;

static_assert(!std::is_copy_constructible_v<ExecutionMemory>);
static_assert(!std::is_move_constructible_v<ExecutionMemory>);
static_assert(!std::is_copy_assignable_v<ExecutionMemory>);
static_assert(!std::is_move_assignable_v<ExecutionMemory>);

namespace {

namespace ct = carven::testing;

auto integer_value(TypeID type, std::int64_t value) noexcept -> ExecutionValue {
    return ConstantAtom {.type = type, .value = IntegerConstant::from_signed(value)};
}

auto check_integer(
    const ConstantValueReader& values,
    const ExecutionValue* value,
    std::int64_t expected
) noexcept -> void {
    if (!ct::expect(value != nullptr)) {
        return;
    }
    const auto atom = execution_atom(values, *value);
    if (!ct::expect(atom.has_value())) {
        return;
    }
    ct::expect(std::get<IntegerConstant>(atom->value) == IntegerConstant::from_signed(expected));
}

auto pair_value(TypeID type, TypeID element, std::int64_t first, std::int64_t second) noexcept
    -> ExecutionValue {
    auto elements = std::vector<ExecutionValue>();
    elements.push_back(integer_value(element, first));
    elements.push_back(integer_value(element, second));
    return ExecutionAggregateValue {.type = type, .elements = std::move(elements)};
}

auto enum_value(TypeID type, EnumCaseID member, TypeID element, std::int64_t payload) noexcept
    -> ExecutionValue {
    auto elements = std::vector<ExecutionValue>();
    elements.push_back(integer_value(element, payload));
    return ExecutionEnumValue {.type = type, .enum_case = member, .payload = std::move(elements)};
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Execution memory: equal values retain distinct object identities",
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            auto memory = ExecutionMemory();
            const auto first = memory.create(integer_value(integer, 7));
            const auto second = memory.create(integer_value(integer, 7));

            ct::expect(first != second);
            auto* first_value = memory.resolve(first);
            auto* second_value = memory.resolve(second);
            if (!ct::expect(first_value != nullptr)) {
                return;
            }
            if (!ct::expect(second_value != nullptr)) {
                return;
            }
            auto steps = 0uz;
            ct::expect(execution_equal(values, *first_value, *second_value, steps, 4uz) == true);
        }
    );

    ct::test(
        "Execution memory: ordinary root and field assignment preserve addresses",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto pair = fixture.compilation.intern_type(
                CanonicalType {.value = ArrayTypeValue {.element = integer, .extent = 2u}}
            );
            auto memory = ExecutionMemory();

            const auto scalar = memory.create(integer_value(integer, 1));
            if (!ct::expect(memory.assign(scalar, integer_value(integer, 2)))) {
                return;
            }
            check_integer(values, memory.resolve(scalar), 2);

            const auto aggregate = memory.create(pair_value(pair, integer, 3, 4));
            const auto first_field = memory.project(aggregate, 0uz);
            const auto second_field = memory.project(aggregate, 1uz);
            if (!ct::expect(first_field.has_value())) {
                return;
            }
            if (!ct::expect(second_field.has_value())) {
                return;
            }
            if (!ct::expect(memory.assign(aggregate, pair_value(pair, integer, 5, 6)))) {
                return;
            }
            check_integer(values, memory.resolve(*first_field), 5);
            check_integer(values, memory.resolve(*second_field), 6);
            if (!ct::expect(memory.assign(*first_field, integer_value(integer, 8)))) {
                return;
            }
            check_integer(values, memory.resolve(*first_field), 8);
            check_integer(values, memory.resolve(*second_field), 6);
        }
    );

    ct::test(
        "Execution memory: enum payload values do not expose storage projections",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto enumeration = fixture.compilation.reserve_enum_declaration();
            const auto member = fixture.compilation.reserve_enum_case_declaration();
            const auto enum_type = fixture.compilation.intern_type(
                CanonicalType {.value = EnumTypeValue {.enumeration = enumeration}}
            );
            auto memory = ExecutionMemory();
            const auto object = memory.create(enum_value(enum_type, member, integer, 1));
            ct::expect(!(memory.project(object, 0uz).has_value()));
        }
    );

    ct::test(
        "Execution memory: borrowed slice observations validate live backing and bounds",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto array = fixture.compilation.intern_type(
                {.value = ArrayTypeValue {.element = integer, .extent = 2u}}
            );
            const auto slice_type =
                fixture.compilation.intern_type({.value = SliceTypeValue {.element = integer}});
            auto memory = ExecutionMemory();
            const auto backing = memory.create(pair_value(array, integer, 3, 4));
            const auto slice = ExecutionValue(
                ExecutionSlice {
                    .type = slice_type,
                    .backing = backing,
                    .offset = 1uz,
                    .extent = 1uz
                }
            );
            const auto borrowed = execution_compound_view(values, slice, &memory);
            if (!ct::expect(borrowed.has_value())) {
                return;
            }
            ct::expect(borrowed->size() == 1uz);
            const auto selected = memory.project(backing, 1uz);
            if (!ct::expect(selected.has_value())) {
                return;
            }
            check_integer(values, memory.resolve(*selected), 4);
            if (!ct::expect(display_execution_value(values, slice, false, &memory).has_value())) {
                return;
            }
            ct::expect(display_execution_value(values, slice, false, &memory)->contains("4"));
            const auto invalid = ExecutionValue(
                ExecutionSlice {
                    .type = slice_type,
                    .backing = backing,
                    .offset = 2uz,
                    .extent = 1uz
                }
            );
            ct::expect(!(execution_compound_view(values, invalid, &memory).has_value()));
            memory.release(backing);
            ct::expect(!(display_execution_value(values, slice, false, &memory).has_value()));
        }
    );

    ct::test(
        "Execution memory: release and another session cannot reuse an address",
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto integer = fixture.compilation.builtin_type(BuiltinType::I32);
            auto first_memory = ExecutionMemory();
            const auto old_object = first_memory.create(integer_value(integer, 1));
            first_memory.release(old_object);
            ct::expect(first_memory.resolve(old_object) == nullptr);

            const auto new_object = first_memory.create(integer_value(integer, 1));
            ct::expect(new_object != old_object);
            ct::expect(first_memory.resolve(old_object) == nullptr);
            check_integer(fixture.compilation, first_memory.resolve(new_object), 1);

            auto second_memory = ExecutionMemory();
            ct::expect(second_memory.resolve(new_object) == nullptr);
            const auto other_object = second_memory.create(integer_value(integer, 1));
            ct::expect(other_object != new_object);
            ct::expect(first_memory.resolve(other_object) == nullptr);
        }
    );

    ct::test(
        "Execution memory: text byte views borrow their owner and preserve selected addresses",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto byte = values.builtin_type(BuiltinType::U8);
            const auto slice_type =
                fixture.compilation.intern_type({.value = SliceTypeValue {.element = byte}});
            auto memory = ExecutionMemory();
            const auto owner = memory.create(ExecutionOwnedText("ab"));
            const auto* text = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
            if (!ct::expect(text != nullptr)) {
                return;
            }
            const auto backing = memory.text_bytes(text->borrow(), byte);
            if (!ct::expect(backing.has_value())) {
                return;
            }
            ct::expect(memory.text_bytes(text->borrow(), byte) == backing);
            const auto borrowed = ExecutionValue(text->borrow());
            const auto slice = ExecutionSlice {
                .type = slice_type,
                .backing = *backing,
                .offset = 1uz,
                .extent = 1uz
            };
            const auto element = memory.project(*backing, 1uz);
            if (!ct::expect(element.has_value())) {
                return;
            }
            check_integer(values, memory.resolve(*element), 98);
            const auto view = execution_compound_view(values, slice, &memory);
            if (!ct::expect(view.has_value())) {
                return;
            }
            if (!ct::expect(display_execution_value(values, slice, false, &memory).has_value())) {
                return;
            }
            ct::expect(execution_text(values, borrowed) == "ab");
            if (!ct::expect(memory.assign(owner, ExecutionOwnedText("cd")))) {
                return;
            }
            ct::expect(!(execution_text(values, borrowed).has_value()));
            ct::expect(memory.resolve(*element) == nullptr);
            ct::expect(!(memory.view(slice).has_value()));

            auto* replacement = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
            if (!ct::expect(replacement != nullptr)) {
                return;
            }
            const auto new_backing = memory.text_bytes(replacement->borrow(), byte);
            ct::expect(new_backing != backing);
            if (!ct::expect(new_backing.has_value())) {
                return;
            }
            const auto new_element = memory.project(*new_backing, 0uz);
            if (!ct::expect(new_element.has_value())) {
                return;
            }
            memory.release(owner);
            ct::expect(memory.resolve(*new_element) == nullptr);
        }
    );

    ct::test(
        "Execution memory: taking String invalidates byte coordinates and preserves content",
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto byte = values.builtin_type(BuiltinType::U8);
            auto memory = ExecutionMemory();
            const auto owner = memory.create(ExecutionOwnedText("ab"));
            auto* value = memory.resolve(owner);
            if (!ct::expect(value != nullptr)) {
                return;
            }
            const auto* text = std::get_if<ExecutionOwnedText>(value);
            if (!ct::expect(text != nullptr)) {
                return;
            }
            const auto previous = memory.text_bytes(text->borrow(), byte);
            if (!ct::expect(previous.has_value())) {
                return;
            }
            const auto old_element = memory.project(*previous, 0uz);
            if (!ct::expect(old_element.has_value())) {
                return;
            }
            transfer_owned_text(*value);
            const auto moved = memory.create(std::move(*value));
            memory.release(owner);
            ct::expect(memory.resolve(*old_element) == nullptr);
            const auto* moved_text = std::get_if<ExecutionOwnedText>(memory.resolve(moved));
            if (!ct::expect(moved_text != nullptr)) {
                return;
            }
            const auto backing = memory.text_bytes(moved_text->borrow(), byte);
            if (!ct::expect(backing.has_value())) {
                return;
            }
            ct::expect(backing != previous);
            const auto element = memory.project(*backing, 1uz);
            if (!ct::expect(element.has_value())) {
                return;
            }
            check_integer(values, memory.resolve(*element), 98);
        }
    );

    ct::test(
        "Execution memory: dead domains and expired text cannot acquire new storage",
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto integer = fixture.compilation.builtin_type(BuiltinType::I32);
            const auto stale = [&]() noexcept {
                auto memory = ExecutionMemory();
                return memory.create(integer_value(integer, 1));
            }();
            auto memory = ExecutionMemory();
            const auto current = memory.create(integer_value(integer, 2));
            ct::expect(current.owner != stale.owner);
            ct::expect(memory.resolve(stale) == nullptr);

            const auto borrowed = []() static noexcept {
                const auto owner = ExecutionOwnedText("old");
                return owner.borrow();
            }();
            ct::expect(
                !(memory.text_bytes(borrowed, fixture.compilation.builtin_type(BuiltinType::U8)))
            );
        }
    );

    ct::test(
        "Execution memory: shared text has stable byte coordinates in each domain",
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto byte = values.builtin_type(BuiltinType::U8);
            auto first = ExecutionMemory();
            auto second = ExecutionMemory();
            auto text = std::optional(ExecutionText(std::string("ab")));
            const auto borrowed = text->borrow();
            const auto first_backing = first.text_bytes(*text, byte);
            const auto second_backing = second.text_bytes(*text, byte);
            if (!ct::expect(first_backing.has_value())) {
                return;
            }
            if (!ct::expect(second_backing.has_value())) {
                return;
            }
            ct::expect(first_backing != second_backing);
            ct::expect(first.text_bytes(borrowed, byte) == first_backing);
            ct::expect(second.text_bytes(borrowed, byte) == second_backing);
            const auto first_byte = first.project(*first_backing, 0uz);
            const auto second_byte = second.project(*second_backing, 0uz);
            if (!ct::expect(first_byte.has_value())) {
                return;
            }
            if (!ct::expect(second_byte.has_value())) {
                return;
            }
            check_integer(values, first.resolve(*first_byte), 97);
            check_integer(values, second.resolve(*second_byte), 97);
            ct::expect(first.resolve(*second_byte) == nullptr);
            ct::expect(second.resolve(*first_byte) == nullptr);
            text.reset();
            ct::expect(!(borrowed.bytes().has_value()));
            ct::expect(first.resolve(*first_byte) == nullptr);
            ct::expect(second.resolve(*second_byte) == nullptr);
            ct::expect(!(first.text_bytes(borrowed, byte).has_value()));
            ct::expect(!(second.text_bytes(borrowed, byte).has_value()));
        }
    );
});

} // namespace
