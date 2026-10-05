module carven:test.internal.support.id_table;

import :support.id_table;
import :support.typed_id;
import :test.harness.framework;
import std;

namespace {

struct TestIDTag final {};

using TestID = TypedID<TestIDTag>;
using TestTable = IDTable<std::string, TestID>;
using TestReservedTable = ReservedTable<std::string, TestID>;

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
static_assert(!std::copy_constructible<TestReservedTable>);
static_assert(std::movable<TestReservedTable>);
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


const TestSuite suite([] static noexcept {
    "Support IDTable: typed IDs, views and rewind"_test = [] static noexcept {
        auto table = TestTable();
        const auto first = table.add("first");
        const auto checkpoint = table.checkpoint();
        const auto second = table.add("second");

        expect_equal(first.index(), 0u);
        expect_equal(second.index(), 1u);
        expect(table.contains(second));
        expect_equal(table.get(first), std::string_view("first"));
        expect_equal(table.values().size(), 2u);

        table.rewind(checkpoint);
        expect(!table.contains(second));
        expect_equal(table.values().size(), 1u);
    };

    "Support ReservedTable: reservations preserve identity when sealed"_test = [] static noexcept {
        auto reservations = TestReservedTable();
        const auto first = reservations.reserve();
        const auto second = reservations.reserve();

        reservations.define(second, "second");
        reservations.define(first, "first");
        expect_equal(reservations.get_defined(first), std::string_view("first"));
        expect_equal(std::as_const(reservations).get_defined(second), std::string_view("second"));

        const auto table = std::move(reservations).seal();
        expect_equal(table.get(first), std::string_view("first"));
        expect_equal(table.get(second), std::string_view("second"));
    };
});

} // namespace
