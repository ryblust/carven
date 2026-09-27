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
    return ExecutionAggregateValue {
        .type = type,
        .elements = {integer_value(element, first), integer_value(element, second)}
    };
}

auto enum_value(TypeID type, EnumCaseID member, TypeID element, std::int64_t payload) noexcept
    -> ExecutionValue {
    return ExecutionEnumValue {
        .type = type,
        .enum_case = member,
        .payload = {integer_value(element, payload)}
    };
}

} // namespace

TEST_CASE("Execution memory: equal values retain distinct object identities") {
    auto fixture = ConstantEvaluationFixture();
    auto& values = fixture.compilation;
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
    auto& values = fixture.compilation;
    const auto integer = values.builtin_type(BuiltinType::I32);
    const auto pair = values.intern_type(
        CanonicalType {.value = ArrayTypeValue {.element = integer, .extent = 2u}}
    );
    auto memory = ExecutionMemory();

    const auto scalar = memory.create(integer_value(integer, 1));
    const auto scalar_address = scalar;
    REQUIRE(memory.assign(scalar, integer_value(integer, 2)));
    check_integer(values, memory.resolve(scalar_address), 2);

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
    auto& values = fixture.compilation;
    const auto integer = values.builtin_type(BuiltinType::I32);
    const auto enumeration = values.reserve_enum_declaration();
    const auto member = values.reserve_enum_case_declaration();
    const auto enum_type =
        values.intern_type(CanonicalType {.value = EnumTypeValue {.enumeration = enumeration}});
    auto memory = ExecutionMemory();
    const auto object = memory.create(enum_value(enum_type, member, integer, 1));
    CHECK_FALSE(memory.project(object, 0uz).has_value());
}

TEST_CASE("Execution memory: borrowed slice observations validate live backing and bounds") {
    auto fixture = ConstantEvaluationFixture();
    auto& values = fixture.compilation;
    const auto integer = values.builtin_type(BuiltinType::I32);
    const auto array =
        values.intern_type({.value = ArrayTypeValue {.element = integer, .extent = 2u}});
    const auto slice_type = values.intern_type({.value = SliceTypeValue {.element = integer}});
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
    auto fixture = ConstantEvaluationFixture();
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
    auto& values = fixture.compilation;
    const auto byte = values.builtin_type(BuiltinType::U8);
    const auto slice_type = values.intern_type({.value = SliceTypeValue {.element = byte}});
    auto memory = ExecutionMemory();
    const auto owner = memory.create(make_owned_execution_text("ab"));
    const auto* text = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
    REQUIRE(text != nullptr);
    const auto backing = memory.text_bytes(text->storage, byte);
    CHECK(memory.text_bytes(text->storage, byte) == backing);
    const auto borrowed = ExecutionValue(ExecutionText {.storage = std::weak_ptr(text->storage)});
    const auto slice =
        ExecutionSlice {.type = slice_type, .backing = backing, .offset = 1uz, .extent = 1uz};
    const auto element = memory.project(backing, 1uz);
    REQUIRE(element.has_value());
    check_integer(values, memory.resolve(*element), 98);
    const auto view = execution_compound_view(values, slice, &memory);
    REQUIRE(view.has_value());
    REQUIRE(display_execution_value(values, slice, false, &memory).has_value());
    CHECK(execution_text(values, borrowed) == "ab");
    REQUIRE(memory.assign(owner, make_owned_execution_text("cd")));
    CHECK_FALSE(execution_text(values, borrowed).has_value());
    CHECK(memory.resolve(*element) == nullptr);
    CHECK_FALSE(memory.view(slice).has_value());

    auto* replacement = std::get_if<ExecutionOwnedText>(memory.resolve(owner));
    REQUIRE(replacement != nullptr);
    const auto new_backing = memory.text_bytes(replacement->storage, byte);
    CHECK(new_backing != backing);
    const auto new_element = memory.project(new_backing, 0uz);
    REQUIRE(new_element.has_value());
    memory.release(owner);
    CHECK(memory.resolve(*new_element) == nullptr);
}

TEST_CASE("Execution memory: taking String ends old byte coordinates without copying its content") {
    auto fixture = ConstantEvaluationFixture();
    auto& values = fixture.compilation;
    const auto byte = values.builtin_type(BuiltinType::U8);
    auto memory = ExecutionMemory();
    const auto owner = memory.create(make_owned_execution_text("ab"));
    auto* value = memory.resolve(owner);
    REQUIRE(value != nullptr);
    const auto* text = std::get_if<ExecutionOwnedText>(value);
    REQUIRE(text != nullptr);
    const auto previous = memory.text_bytes(text->storage, byte);
    const auto old_element = memory.project(previous, 0uz);
    REQUIRE(old_element.has_value());
    transfer_owned_text(*value);
    const auto moved = memory.create(std::move(*value));
    memory.release(owner);
    CHECK(memory.resolve(*old_element) == nullptr);
    const auto* moved_text = std::get_if<ExecutionOwnedText>(memory.resolve(moved));
    REQUIRE(moved_text != nullptr);
    const auto backing = memory.text_bytes(moved_text->storage, byte);
    CHECK(backing != previous);
    const auto element = memory.project(backing, 1uz);
    REQUIRE(element.has_value());
    check_integer(values, memory.resolve(*element), 98);
}
