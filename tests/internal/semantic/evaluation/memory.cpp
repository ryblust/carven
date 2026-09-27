module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.evaluation.memory;

import :semantic.evaluation.display;
import :semantic.evaluation.memory;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.type;
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
    REQUIRE(value != nullptr);
    const auto atom = execution_atom(values, *value);
    REQUIRE(atom.has_value());
    CHECK(std::get<IntegerConstant>(atom->value) == IntegerConstant::from_signed(expected));
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

TEST_CASE("Execution memory: equal values retain distinct object identities") {
    const auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    const auto integer = values.builtin_type(BuiltinType::I32);
    auto memory = ExecutionMemory();
    const auto first = memory.create(integer_value(integer, 7));
    const auto second = memory.create(integer_value(integer, 7));

    CHECK(first != second);
    auto* first_value = memory.resolve(first);
    auto* second_value = memory.resolve(second);
    REQUIRE(first_value != nullptr);
    REQUIRE(second_value != nullptr);
    auto steps = 0uz;
    CHECK(execution_equal(values, *first_value, *second_value, steps, 4uz) == true);
}

TEST_CASE("Execution memory: ordinary root and field assignment preserve addresses") {
    auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    const auto integer = values.builtin_type(BuiltinType::I32);
    const auto pair = fixture.compilation.intern_type(
        CanonicalType {.value = ArrayTypeValue {.element = integer, .extent = 2u}}
    );
    auto memory = ExecutionMemory();

    const auto scalar = memory.create(integer_value(integer, 1));
    REQUIRE(memory.assign(scalar, integer_value(integer, 2)));
    check_integer(values, memory.resolve(scalar), 2);

    const auto aggregate = memory.create(pair_value(pair, integer, 3, 4));
    const auto first_field = memory.project(aggregate, 0uz);
    const auto second_field = memory.project(aggregate, 1uz);
    REQUIRE(first_field.has_value());
    REQUIRE(second_field.has_value());
    REQUIRE(memory.assign(aggregate, pair_value(pair, integer, 5, 6)));
    check_integer(values, memory.resolve(*first_field), 5);
    check_integer(values, memory.resolve(*second_field), 6);
    REQUIRE(memory.assign(*first_field, integer_value(integer, 8)));
    check_integer(values, memory.resolve(*first_field), 8);
    check_integer(values, memory.resolve(*second_field), 6);
}

TEST_CASE("Execution memory: enum payload values do not expose storage projections") {
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
    CHECK_FALSE(memory.project(object, 0uz).has_value());
}

TEST_CASE("Execution memory: borrowed slice observations validate live backing and bounds") {
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
        ExecutionSlice {.type = slice_type, .backing = backing, .offset = 1uz, .extent = 1uz}
    );
    const auto borrowed = execution_compound_view(values, slice, &memory);
    REQUIRE(borrowed.has_value());
    CHECK(borrowed->size() == 1uz);
    const auto selected = memory.project(backing, 1uz);
    REQUIRE(selected.has_value());
    check_integer(values, memory.resolve(*selected), 4);
    REQUIRE(display_execution_value(values, slice, false, &memory).has_value());
    CHECK(display_execution_value(values, slice, false, &memory)->contains("4"));
    const auto invalid = ExecutionValue(
        ExecutionSlice {.type = slice_type, .backing = backing, .offset = 2uz, .extent = 1uz}
    );
    CHECK_FALSE(execution_compound_view(values, invalid, &memory).has_value());
    memory.release(backing);
    CHECK_FALSE(display_execution_value(values, slice, false, &memory).has_value());
}

TEST_CASE("Execution memory: release and another session cannot reuse an address") {
    const auto fixture = ConstantEvaluationFixture();
    const auto integer = fixture.compilation.builtin_type(BuiltinType::I32);
    auto first_memory = ExecutionMemory();
    const auto old_object = first_memory.create(integer_value(integer, 1));
    first_memory.release(old_object);
    CHECK(first_memory.resolve(old_object) == nullptr);

    const auto new_object = first_memory.create(integer_value(integer, 1));
    CHECK(new_object != old_object);
    CHECK(first_memory.resolve(old_object) == nullptr);
    check_integer(fixture.compilation, first_memory.resolve(new_object), 1);

    auto second_memory = ExecutionMemory();
    CHECK(second_memory.resolve(new_object) == nullptr);
    const auto other_object = second_memory.create(integer_value(integer, 1));
    CHECK(other_object != new_object);
    CHECK(first_memory.resolve(other_object) == nullptr);
}

TEST_CASE("Execution memory: text byte views borrow their owner and preserve selected addresses") {
    auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    const auto byte = values.builtin_type(BuiltinType::U8);
    const auto slice_type =
        fixture.compilation.intern_type({.value = SliceTypeValue {.element = byte}});
    auto memory = ExecutionMemory();
    const auto owner = memory.create(ExecutionOwnedText("ab"));
    const auto* text = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
    REQUIRE(text != nullptr);
    const auto backing = memory.text_bytes(text->borrow(), byte);
    REQUIRE(backing.has_value());
    CHECK(memory.text_bytes(text->borrow(), byte) == backing);
    const auto borrowed = ExecutionValue(text->borrow());
    const auto slice =
        ExecutionSlice {.type = slice_type, .backing = *backing, .offset = 1uz, .extent = 1uz};
    const auto element = memory.project(*backing, 1uz);
    REQUIRE(element.has_value());
    check_integer(values, memory.resolve(*element), 98);
    const auto view = execution_compound_view(values, slice, &memory);
    REQUIRE(view.has_value());
    REQUIRE(display_execution_value(values, slice, false, &memory).has_value());
    CHECK(execution_text(values, borrowed) == "ab");
    REQUIRE(memory.assign(owner, ExecutionOwnedText("cd")));
    CHECK_FALSE(execution_text(values, borrowed).has_value());
    CHECK(memory.resolve(*element) == nullptr);
    CHECK_FALSE(memory.view(slice).has_value());

    auto* replacement = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
    REQUIRE(replacement != nullptr);
    const auto new_backing = memory.text_bytes(replacement->borrow(), byte);
    CHECK(new_backing != backing);
    REQUIRE(new_backing.has_value());
    const auto new_element = memory.project(*new_backing, 0uz);
    REQUIRE(new_element.has_value());
    memory.release(owner);
    CHECK(memory.resolve(*new_element) == nullptr);
}

TEST_CASE("Execution memory: taking String invalidates byte coordinates and preserves content") {
    const auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    const auto byte = values.builtin_type(BuiltinType::U8);
    auto memory = ExecutionMemory();
    const auto owner = memory.create(ExecutionOwnedText("ab"));
    auto* value = memory.resolve(owner);
    REQUIRE(value != nullptr);
    const auto* text = std::get_if<ExecutionOwnedText>(value);
    REQUIRE(text != nullptr);
    const auto previous = memory.text_bytes(text->borrow(), byte);
    REQUIRE(previous.has_value());
    const auto old_element = memory.project(*previous, 0uz);
    REQUIRE(old_element.has_value());
    transfer_owned_text(*value);
    const auto moved = memory.create(std::move(*value));
    memory.release(owner);
    CHECK(memory.resolve(*old_element) == nullptr);
    const auto* moved_text = std::get_if<ExecutionOwnedText>(memory.resolve(moved));
    REQUIRE(moved_text != nullptr);
    const auto backing = memory.text_bytes(moved_text->borrow(), byte);
    REQUIRE(backing.has_value());
    CHECK(backing != previous);
    const auto element = memory.project(*backing, 1uz);
    REQUIRE(element.has_value());
    check_integer(values, memory.resolve(*element), 98);
}

TEST_CASE("Execution memory: dead domains and expired text cannot acquire new storage") {
    const auto fixture = ConstantEvaluationFixture();
    const auto integer = fixture.compilation.builtin_type(BuiltinType::I32);
    const auto stale = [&]() noexcept {
        auto memory = ExecutionMemory();
        return memory.create(integer_value(integer, 1));
    }();
    auto memory = ExecutionMemory();
    const auto current = memory.create(integer_value(integer, 2));
    CHECK(current.owner != stale.owner);
    CHECK(memory.resolve(stale) == nullptr);

    const auto borrowed = []() static noexcept {
        const auto owner = ExecutionOwnedText("old");
        return owner.borrow();
    }();
    CHECK_FALSE(memory.text_bytes(borrowed, fixture.compilation.builtin_type(BuiltinType::U8)));
}

TEST_CASE("Execution memory: shared text has stable byte coordinates in each domain") {
    const auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    const auto byte = values.builtin_type(BuiltinType::U8);
    auto first = ExecutionMemory();
    auto second = ExecutionMemory();
    auto text = std::optional(ExecutionText(std::string("ab")));
    const auto borrowed = text->borrow();
    const auto first_backing = first.text_bytes(*text, byte);
    const auto second_backing = second.text_bytes(*text, byte);
    REQUIRE(first_backing.has_value());
    REQUIRE(second_backing.has_value());
    CHECK(first_backing != second_backing);
    CHECK(first.text_bytes(borrowed, byte) == first_backing);
    CHECK(second.text_bytes(borrowed, byte) == second_backing);
    const auto first_byte = first.project(*first_backing, 0uz);
    const auto second_byte = second.project(*second_backing, 0uz);
    REQUIRE(first_byte.has_value());
    REQUIRE(second_byte.has_value());
    check_integer(values, first.resolve(*first_byte), 97);
    check_integer(values, second.resolve(*second_byte), 97);
    CHECK(first.resolve(*second_byte) == nullptr);
    CHECK(second.resolve(*first_byte) == nullptr);
    text.reset();
    CHECK_FALSE(borrowed.bytes().has_value());
    CHECK(first.resolve(*first_byte) == nullptr);
    CHECK(second.resolve(*second_byte) == nullptr);
    CHECK_FALSE(first.text_bytes(borrowed, byte).has_value());
    CHECK_FALSE(second.text_bytes(borrowed, byte).has_value());
}
