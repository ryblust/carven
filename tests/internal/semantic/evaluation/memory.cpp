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

auto integer_value(TypeID type, std::int64_t value) noexcept -> ExecutionValue {
    return ConstantAtom {.type = type, .value = IntegerConstant::from_signed(value)};
}

auto check_integer(
    const ConstantValueReader& values,
    const ExecutionValue* value,
    std::int64_t expected
) noexcept -> void {
    if (!expect(value != nullptr)) {
        return;
    }
    const auto atom = execution_atom(values, *value);
    if (!expect(atom.has_value())) {
        return;
    }
    expect(std::get<IntegerConstant>(atom->value) == IntegerConstant::from_signed(expected));
}

auto pair_value(TypeID type, TypeID element, std::int64_t first, std::int64_t second) noexcept
    -> ExecutionValue {
    auto elements = std::vector<ExecutionValue>();
    elements.push_back(integer_value(element, first));
    elements.push_back(integer_value(element, second));
    return ExecutionAggregateValue {.type = type, .elements = std::move(elements), .capacity = 0uz};
}

auto enum_value(TypeID type, EnumCaseID member, TypeID element, std::int64_t payload) noexcept
    -> ExecutionValue {
    auto elements = std::vector<ExecutionValue>();
    elements.push_back(integer_value(element, payload));
    return ExecutionEnumValue {.type = type, .enum_case = member, .payload = std::move(elements)};
}

const TestSuite suite([] static noexcept {
    "Execution memory: equal values retain distinct object identities"_test = [] static noexcept {
        const auto fixture = ConstantEvaluationFixture();
        const auto& values = fixture.compilation;
        const auto integer = values.builtin_type(BuiltinType::I32);
        auto memory = ExecutionMemory();
        const auto first = memory.create(integer_value(integer, 7));
        const auto second = memory.create(integer_value(integer, 7));

        expect(first != second);
        const auto* first_value = memory.resolve(first);
        const auto* second_value = memory.resolve(second);
        if (!expect(first_value != nullptr)) {
            return;
        }
        if (!expect(second_value != nullptr)) {
            return;
        }
        auto steps = 0uz;
        expect(execution_equal(values, *first_value, *second_value, steps, 4uz) == true);
    };

    "Execution memory: ordinary root and field assignment preserve addresses"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto pair = fixture.compilation.intern_type(
                CanonicalType {.value = ArrayTypeValue {.element = integer, .extent = 2u}}
            );
            auto memory = ExecutionMemory();

            const auto scalar = memory.create(integer_value(integer, 1));
            if (!expect(memory.assign(scalar, integer_value(integer, 2)))) {
                return;
            }
            check_integer(values, memory.resolve(scalar), 2);

            const auto aggregate = memory.create(pair_value(pair, integer, 3, 4));
            const auto first_field = memory.project(aggregate, 0uz);
            const auto second_field = memory.project(aggregate, 1uz);
            if (!expect(first_field.has_value())) {
                return;
            }
            if (!expect(second_field.has_value())) {
                return;
            }
            if (!expect(memory.assign(aggregate, pair_value(pair, integer, 5, 6)))) {
                return;
            }
            check_integer(values, memory.resolve(*first_field), 5);
            check_integer(values, memory.resolve(*second_field), 6);
            if (!expect(memory.assign(*first_field, integer_value(integer, 8)))) {
                return;
            }
            check_integer(values, memory.resolve(*first_field), 8);
            check_integer(values, memory.resolve(*second_field), 6);
        };

    "Execution memory: product bindings retain slots with independent lifetimes"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto pair =
                values.intern_type({.value = TupleTypeValue {.elements = {integer, integer}}});
            const auto pointer_type = values.intern_type(
                {.value = PointerTypeValue {.target = integer, .access = PointerAccess::Read}}
            );
            auto memory = ExecutionMemory();
            const auto parent = memory.create(pair_value(pair, integer, 3, 4));
            const auto first_slot = memory.project(parent, 0uz);
            const auto second_slot = memory.project(parent, 1uz);
            if (!expect(first_slot.has_value()) || !expect(second_slot.has_value())) {
                return;
            }
            const auto first = memory.adopt_projection(*first_slot);
            const auto second = memory.adopt_projection(*second_slot);
            const auto pointer = ExecutionPointer {.type = pointer_type, .target = first};
            check_integer(values, memory.resolve(*pointer.target), 3);
            expect(memory.resolve(first) == memory.resolve(*first_slot));
            if (!expect(memory.assign(first, integer_value(integer, 8)))) {
                return;
            }
            check_integer(values, memory.resolve(*pointer.target), 8);
            check_integer(values, memory.resolve(*first_slot), 8);
            check_integer(values, memory.resolve(second), 4);

            memory.release(first);
            expect(memory.resolve(*pointer.target) == nullptr);
            expect(!memory.assign(first, integer_value(integer, 9)));
            expect(memory.resolve(parent) != nullptr);
            check_integer(values, memory.resolve(*first_slot), 8);
            check_integer(values, memory.resolve(second), 4);
            const auto replacement = memory.adopt_projection(*first_slot);
            expect(replacement.object != first.object);
            expect(memory.resolve(*pointer.target) == nullptr);
            check_integer(values, memory.resolve(replacement), 8);

            memory.release(parent);
            expect(memory.resolve(replacement) == nullptr);
            expect(memory.resolve(second) == nullptr);
            expect(memory.resolve(*first_slot) == nullptr);
            expect(memory.resolve(*second_slot) == nullptr);
        };

    "Execution memory: nested product bindings update original storage and expire with it"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto integer = values.builtin_type(BuiltinType::I32);
            const auto pair =
                values.intern_type({.value = TupleTypeValue {.elements = {integer, integer}}});
            const auto product =
                values.intern_type({.value = TupleTypeValue {.elements = {pair, integer}}});
            auto elements = std::vector<ExecutionValue>();
            elements.push_back(pair_value(pair, integer, 1, 2));
            elements.push_back(integer_value(integer, 3));
            auto memory = ExecutionMemory();
            const auto parent = memory.create(
                ExecutionAggregateValue {
                    .type = product,
                    .elements = std::move(elements),
                    .capacity = 0uz
                }
            );
            const auto pair_slot = memory.project(parent, 0uz);
            const auto outer_sibling = memory.project(parent, 1uz);
            if (!expect(pair_slot.has_value()) || !expect(outer_sibling.has_value())) {
                return;
            }
            const auto original = memory.project(*pair_slot, 1uz);
            const auto nested = memory.adopt_projection(*pair_slot);
            const auto nested_slot = memory.project(nested, 1uz);
            const auto nested_sibling = memory.project(nested, 0uz);
            if (!expect(original.has_value())
                || !expect(nested_slot.has_value())
                || !expect(nested_sibling.has_value())) {
                return;
            }
            const auto leaf = memory.adopt_projection(*nested_slot);
            if (!expect(memory.assign(leaf, integer_value(integer, 7)))) {
                return;
            }
            check_integer(values, memory.resolve(*original), 7);
            check_integer(values, memory.resolve(*nested_slot), 7);
            check_integer(values, memory.resolve(*nested_sibling), 1);
            check_integer(values, memory.resolve(*outer_sibling), 3);

            memory.release(parent);
            expect(memory.resolve(nested) == nullptr);
            expect(memory.resolve(leaf) == nullptr);
            expect(memory.resolve(*nested_slot) == nullptr);
            expect(memory.resolve(*nested_sibling) == nullptr);
            expect(memory.resolve(*original) == nullptr);
            expect(!memory.assign(leaf, integer_value(integer, 8)));
        };

    "Execution memory: enum payload projections preserve mutation and lifetime"_test =
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
            const auto selected = memory.project(object, 0uz);
            require(selected.has_value());
            check_integer(values, memory.resolve(*selected), 1);
            expect(memory.assign(*selected, integer_value(integer, 7)));
            check_integer(values, memory.resolve(*selected), 7);
            expect(!memory.project(object, 1uz));
            memory.release(object);
            expect(memory.resolve(*selected) == nullptr);
        };

    "Execution memory: borrowed slice observations validate live backing and bounds"_test =
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
            if (!expect(borrowed.has_value())) {
                return;
            }
            expect(borrowed->size() == 1uz);
            const auto selected = memory.project(backing, 1uz);
            if (!expect(selected.has_value())) {
                return;
            }
            check_integer(values, memory.resolve(*selected), 4);
            if (!expect(display_execution_value(values, slice, false, &memory).has_value())) {
                return;
            }
            expect(display_execution_value(values, slice, false, &memory)->contains("4"));
            const auto invalid = ExecutionValue(
                ExecutionSlice {
                    .type = slice_type,
                    .backing = backing,
                    .offset = 2uz,
                    .extent = 1uz
                }
            );
            expect(!(execution_compound_view(values, invalid, &memory).has_value()));
            memory.release(backing);
            expect(!(display_execution_value(values, slice, false, &memory).has_value()));
        };

    "Execution memory: release and another session cannot reuse an address"_test =
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto integer = fixture.compilation.builtin_type(BuiltinType::I32);
            auto first_memory = ExecutionMemory();
            const auto old_object = first_memory.create(integer_value(integer, 1));
            first_memory.release(old_object);
            expect(first_memory.resolve(old_object) == nullptr);

            const auto new_object = first_memory.create(integer_value(integer, 1));
            expect(new_object != old_object);
            expect(first_memory.resolve(old_object) == nullptr);
            check_integer(fixture.compilation, first_memory.resolve(new_object), 1);

            auto second_memory = ExecutionMemory();
            expect(second_memory.resolve(new_object) == nullptr);
            const auto other_object = second_memory.create(integer_value(integer, 1));
            expect(other_object != new_object);
            expect(first_memory.resolve(other_object) == nullptr);
        };

    "Execution memory: UTF text observes the borrowed byte range until release"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto byte = values.builtin_type(BuiltinType::U8);
            const auto array =
                values.intern_type({.value = ArrayTypeValue {.element = byte, .extent = 4u}});
            const auto slice = values.intern_type({.value = SliceTypeValue {.element = byte}});
            auto elements = std::vector<ExecutionValue>();
            for (const auto character : std::string_view("xaby")) {
                elements.push_back(integer_value(byte, character));
            }
            auto memory = ExecutionMemory();
            const auto owner = memory.create(
                ExecutionAggregateValue {
                    .type = array,
                    .elements = std::move(elements),
                    .capacity = 0uz
                }
            );
            const auto text = ExecutionText(
                ExecutionSlice {.type = slice, .backing = owner, .offset = 1uz, .extent = 2uz}
            );
            const auto borrowed = ExecutionValue(text.borrow());
            expect(execution_text(values, borrowed, &memory) == "ab");
            const auto selected = memory.project(owner, 2uz);
            if (!expect(selected.has_value())) {
                return;
            }
            expect(memory.assign(*selected, integer_value(byte, 'c')));
            expect(execution_text(values, borrowed, &memory) == "ac");
            expect(display_execution_value(values, borrowed, false, &memory) == "ac");
            auto steps = 0uz;
            expect(
                execution_equal(
                    values,
                    borrowed,
                    ExecutionText(std::string("ac")),
                    steps,
                    1uz,
                    &memory
                )
                == true
            );
            memory.release(owner);
            expect(!execution_text(values, borrowed, &memory).has_value());
            expect(!display_execution_value(values, borrowed, false, &memory).has_value());
            steps = 0uz;
            const auto equal = execution_equal(
                values,
                borrowed,
                ExecutionText(std::string("ac")),
                steps,
                1uz,
                &memory
            );
            if (expect(!equal.has_value())) {
                expect(equal.error() == ExecutionComparisonFailure::ExpiredText);
            }
        };

    "Execution memory: text byte views borrow their owner and preserve selected addresses"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto byte = values.builtin_type(BuiltinType::U8);
            const auto slice_type =
                fixture.compilation.intern_type({.value = SliceTypeValue {.element = byte}});
            auto memory = ExecutionMemory();
            const auto owner = memory.create(ExecutionOwnedText("ab"));
            const auto* text = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
            if (!expect(text != nullptr)) {
                return;
            }
            const auto backing = memory.text_bytes(text->borrow(), byte);
            if (!expect(backing.has_value())) {
                return;
            }
            expect(memory.text_bytes(text->borrow(), byte) == backing);
            const auto borrowed = ExecutionValue(text->borrow());
            const auto slice = ExecutionSlice {
                .type = slice_type,
                .backing = *backing,
                .offset = 1uz,
                .extent = 1uz
            };
            const auto element = memory.project(*backing, 1uz);
            if (!expect(element.has_value())) {
                return;
            }
            check_integer(values, memory.resolve(*element), 98);
            const auto view = execution_compound_view(values, slice, &memory);
            if (!expect(view.has_value())) {
                return;
            }
            if (!expect(display_execution_value(values, slice, false, &memory).has_value())) {
                return;
            }
            expect(execution_text(values, borrowed) == "ab");
            if (!expect(memory.assign(owner, ExecutionOwnedText("cd")))) {
                return;
            }
            expect(!(execution_text(values, borrowed).has_value()));
            expect(memory.resolve(*element) == nullptr);
            expect(!(memory.view(slice).has_value()));

            const auto* replacement = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
            if (!expect(replacement != nullptr)) {
                return;
            }
            const auto new_backing = memory.text_bytes(replacement->borrow(), byte);
            expect(new_backing != backing);
            if (!expect(new_backing.has_value())) {
                return;
            }
            const auto new_element = memory.project(*new_backing, 0uz);
            if (!expect(new_element.has_value())) {
                return;
            }
            memory.release(owner);
            expect(memory.resolve(*new_element) == nullptr);
        };

    "Execution memory: taking String invalidates byte coordinates and preserves content"_test =
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;
            const auto byte = values.builtin_type(BuiltinType::U8);
            auto memory = ExecutionMemory();
            const auto owner = memory.create(ExecutionOwnedText("ab"));
            auto* value = memory.resolve(owner);
            if (!expect(value != nullptr)) {
                return;
            }
            const auto* text = std::get_if<ExecutionOwnedText>(value);
            if (!expect(text != nullptr)) {
                return;
            }
            const auto previous = memory.text_bytes(text->borrow(), byte);
            if (!expect(previous.has_value())) {
                return;
            }
            const auto old_element = memory.project(*previous, 0uz);
            if (!expect(old_element.has_value())) {
                return;
            }
            transfer_owned_text(*value);
            const auto moved = memory.create(std::move(*value));
            memory.release(owner);
            expect(memory.resolve(*old_element) == nullptr);
            const auto* moved_text = std::get_if<ExecutionOwnedText>(memory.resolve(moved));
            if (!expect(moved_text != nullptr)) {
                return;
            }
            const auto backing = memory.text_bytes(moved_text->borrow(), byte);
            if (!expect(backing.has_value())) {
                return;
            }
            expect(backing != previous);
            const auto element = memory.project(*backing, 1uz);
            if (!expect(element.has_value())) {
                return;
            }
            check_integer(values, memory.resolve(*element), 98);
        };

    "Execution memory: dead domains and expired text cannot acquire new storage"_test =
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto integer = fixture.compilation.builtin_type(BuiltinType::I32);
            const auto stale = [&]() noexcept {
                auto memory = ExecutionMemory();
                return memory.create(integer_value(integer, 1));
            }();
            auto memory = ExecutionMemory();
            const auto current = memory.create(integer_value(integer, 2));
            expect(current.owner != stale.owner);
            expect(memory.resolve(stale) == nullptr);

            const auto borrowed = []() static noexcept {
                const auto owner = ExecutionOwnedText("old");
                return owner.borrow();
            }();
            expect(
                !(memory.text_bytes(borrowed, fixture.compilation.builtin_type(BuiltinType::U8)))
            );
        };

    "Execution memory: shared text has stable byte coordinates in each domain"_test =
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
            if (!expect(first_backing.has_value())) {
                return;
            }
            if (!expect(second_backing.has_value())) {
                return;
            }
            expect(first_backing != second_backing);
            expect(first.text_bytes(borrowed, byte) == first_backing);
            expect(second.text_bytes(borrowed, byte) == second_backing);
            const auto first_byte = first.project(*first_backing, 0uz);
            const auto second_byte = second.project(*second_backing, 0uz);
            if (!expect(first_byte.has_value())) {
                return;
            }
            if (!expect(second_byte.has_value())) {
                return;
            }
            check_integer(values, first.resolve(*first_byte), 97);
            check_integer(values, second.resolve(*second_byte), 97);
            expect(first.resolve(*second_byte) == nullptr);
            expect(second.resolve(*first_byte) == nullptr);
            text.reset();
            expect(!(execution_text(values, borrowed).has_value()));
            expect(first.resolve(*first_byte) == nullptr);
            expect(second.resolve(*second_byte) == nullptr);
            expect(!(first.text_bytes(borrowed, byte).has_value()));
            expect(!(second.text_bytes(borrowed, byte).has_value()));
        };
});

} // namespace
