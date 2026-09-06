module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.type;

import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

using semantic_analysis_test::analyze_program;
using semantic_analysis_test::function_callables;
using semantic_analysis_test::signature;

TEST_CASE("Construction types: children must already exist in the owning store") {
    const auto program = analyze_program("");
    const auto foreign = analyze_program("");
    auto types = ConstructionTypeStore(program.identity());
    auto references = MutableProgramTable<int, TypeTermID>(program.identity());
    const auto self = references.add(0);
    static_cast<void>(references.add(0));
    const auto future = references.add(0);
    auto foreign_references = MutableProgramTable<int, TypeTermID>(foreign.identity());
    const auto foreign_child = foreign_references.add(0);
    auto failures = MutableProgramTable<int, FailureTermID>(program.identity());
    const auto failure = failures.add(0);
    auto canonical = CanonicalTypeStoreBuilder(program.identity());
    const auto integer = canonical.intern_builtin(BuiltinType::I32);

    CHECK(expect_termination("type-array-child-must-exist", [&] noexcept {
        static_cast<void>(
            types.append({.value = ConstructionArrayTypeValue {.element = self, .extent = 1u}})
        );
    }));
    CHECK(expect_termination("type-parameter-child-must-exist", [&] noexcept {
        static_cast<void>(types.append(
            {.value = ConstructionCallableViewTypeValue {
                 .parameters = {{.access = AccessMode::Read, .type = future}},
                 .result = integer,
                 .failures = failure
             }}
        ));
    }));
    CHECK(expect_termination("type-result-child-must-share-owner", [&] noexcept {
        static_cast<void>(types.append(
            {.value = ConstructionCallableViewTypeValue {
                 .parameters = {},
                 .result = foreign_child,
                 .failures = failure
             }}
        ));
    }));
}

TEST_CASE("Construction types: nested callable shapes retain canonical contracts") {
    const auto program = analyze_program(
        "struct Failure {}\n"
        "fn first(values: [fn(&i32) -> i32 throw Failure; 2]) {}\n"
        "fn second(values: [fn(&i32) -> i32 throw Failure; 2]) {}\n"
    );
    const auto callables = function_callables(program);
    REQUIRE_EQ(callables.size(), 2uz);
    const auto first = signature(program, callables[0]);
    const auto second = signature(program, callables[1]);
    REQUIRE_EQ(first.parameters.size(), 1uz);
    REQUIRE_EQ(second.parameters.size(), 1uz);
    CHECK_EQ(first.parameters.front().type, second.parameters.front().type);
    const auto& array_type = program.types().type(first.parameters.front().type);
    const auto* array = std::get_if<ArrayTypeValue>(&array_type.value);
    REQUIRE(array != nullptr);
    CHECK_EQ(array->extent, 2u);
    const auto* view =
        std::get_if<CallableViewTypeValue>(&program.types().type(array->element).value);
    REQUIRE(view != nullptr);
    const auto& contract = program.callable_signatures().signature(view->signature);
    REQUIRE_EQ(contract.parameters.size(), 1uz);
    CHECK_EQ(contract.parameters.front().access, AccessMode::Write);
    CHECK_EQ(contract.parameters.front().type, contract.result);
    CHECK_EQ(
        program.types().type(contract.result).value,
        CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::I32}}
    );
    CHECK_EQ(program.failure_sets().failure_set(contract.failures).members.size(), 1uz);
}
