module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.names;

import :backend.generation.names;

TEST_CASE("Target names: callable-local suffix state is isolated") {
    auto first = TargetNameAllocator {};
    auto second = TargetNameAllocator {};
    first.reserve("operand");
    second.reserve("operand");

    CHECK_EQ(first.fresh(TargetTemporaryNameKind::Operand).spelling(), "operand_2");
    CHECK_EQ(second.fresh(TargetTemporaryNameKind::Operand).spelling(), "operand_2");
}

TEST_CASE("Target names: local bindings and temporaries share a collision domain") {
    auto names = TargetNameAllocator {};
    constexpr auto scope = TargetScopeID {.ordinal = 0};

    const auto binding = names.local_symbol("operand", 0, scope);
    const auto temporary = names.fresh(TargetTemporaryNameKind::Operand);

    CHECK_EQ(binding.spelling(), "operand");
    CHECK_EQ(temporary.spelling(), "operand_2");
}

TEST_CASE("Target names: input reservations protect the callable scope") {
    auto names = TargetNameAllocator {};
    constexpr auto scope = TargetScopeID {.ordinal = 0};
    names.reserve("value", scope);

    CHECK_EQ(names.local_symbol("value", 0, scope).spelling(), "value_2");
}
