module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.names;

import :backend.generation.names;
import std;

TEST_CASE("Target names: callable-local suffix state is isolated") {
    const auto enclosing = std::flat_set<std::string> {"operand"};
    auto first = TargetNameAllocator(enclosing);
    auto second = TargetNameAllocator(enclosing);

    CHECK_EQ(first.fresh(TargetTemporaryNameKind::Operand).spelling(), "operand_2");
    CHECK_EQ(second.fresh(TargetTemporaryNameKind::Operand).spelling(), "operand_2");
    CHECK_EQ(first.fresh(TargetTemporaryNameKind::Operand).spelling(), "operand_3");
    CHECK_EQ(enclosing.size(), 1uz);
}

TEST_CASE("Target names: local bindings and temporaries share a collision domain") {
    const auto enclosing = std::flat_set<std::string> {"operand"};
    auto names = TargetNameAllocator(enclosing);
    constexpr auto scope = TargetScopeID {.ordinal = 0};

    const auto binding = names.local_symbol("operand", 0, scope);
    const auto temporary = names.fresh(TargetTemporaryNameKind::Operand);

    CHECK_EQ(binding.spelling(), "operand_2");
    CHECK_EQ(temporary.spelling(), "operand_3");
}

TEST_CASE("Target names: input reservations protect the callable scope") {
    auto names = TargetNameAllocator {};
    constexpr auto scope = TargetScopeID {.ordinal = 0};
    names.reserve("value", scope);

    CHECK_EQ(names.local_symbol("value", 0, scope).spelling(), "value_2");
}

TEST_CASE("Target names: public encoding separates safe and escaped spellings") {
    const auto cases = std::to_array<std::string_view>(
        {"pricing",
         "match",
         "export",
         "export_cv",
         "class",
         "_Upper",
         "a__b",
         "cv_escaped_",
         "cv_escaped_6578706f7274"}
    );
    auto names = std::flat_set<std::string>();
    for (const auto spelling : cases) {
        const auto name = TargetNameAllocator::public_identifier(spelling);
        CHECK(names.insert(std::string(name.spelling())).second);
        CHECK_EQ(name, TargetNameAllocator::public_identifier(spelling));
    }
    CHECK_EQ(TargetNameAllocator::public_identifier("pricing").spelling(), "pricing");
    CHECK_EQ(
        TargetNameAllocator::public_identifier("export").spelling(),
        "cv_escaped_6578706f7274"
    );
}
