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

static_assert(!std::default_initializable<TestID>);
static_assert(!std::copy_constructible<TestTable>);
static_assert(std::movable<TestTable>);

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
