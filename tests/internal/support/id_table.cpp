module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.support.id_table;

import :support.id_table;
import :support.typed_id;
import std;

namespace {

struct TestIDTag final {};
using TestID = TypedID<TestIDTag>;
using TestTable = IDTable<std::string, TestID>;

template<typename Table>
concept HasRvalueGet =
    requires (Table&& table, TestID test_id) { static_cast<Table&&>(table).get(test_id); };

template<typename Table>
concept HasRvalueTryGet =
    requires (Table&& table, TestID test_id) { static_cast<Table&&>(table).try_get(test_id); };

template<typename Table>
concept HasRvalueValues = requires (Table&& table) { static_cast<Table&&>(table).values(); };

static_assert(!std::default_initializable<TestID>);
static_assert(!std::copy_constructible<TestTable>);
static_assert(std::movable<TestTable>);
static_assert(
    std::same_as<decltype(std::declval<TestTable&>().get(std::declval<TestID>())), std::string&>
);
static_assert(std::same_as<
              decltype(std::declval<const TestTable&>().get(std::declval<TestID>())),
              const std::string&>);
static_assert(
    std::same_as<decltype(std::declval<TestTable&>().try_get(std::declval<TestID>())), std::string*>
);
static_assert(std::same_as<
              decltype(std::declval<const TestTable&>().try_get(std::declval<TestID>())),
              const std::string*>);
static_assert(std::same_as<decltype(std::declval<TestTable&>().values()), std::span<std::string>>);
static_assert(
    std::same_as<decltype(std::declval<const TestTable&>().values()), std::span<const std::string>>
);
static_assert(!HasRvalueGet<TestTable>);
static_assert(!HasRvalueGet<const TestTable>);
static_assert(!HasRvalueTryGet<TestTable>);
static_assert(!HasRvalueTryGet<const TestTable>);
static_assert(!HasRvalueValues<TestTable>);
static_assert(!HasRvalueValues<const TestTable>);

} // namespace

TEST_CASE("Support IDTable: typed IDs, views and rewind") {
    auto table = TestTable();
    const auto first = table.add("first");
    const auto checkpoint = table.checkpoint();
    const auto second = table.add("second");

    CHECK_EQ(first.index(), 0u);
    CHECK_EQ(second.index(), 1u);
    CHECK(table.contains(second));
    CHECK_EQ(table.get(first), "first");
    CHECK_EQ(table.values().size(), 2u);

    table.rewind(checkpoint);
    CHECK(!table.contains(second));
    CHECK_EQ(table.values().size(), 1u);
}
