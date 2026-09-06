module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/passing.hpp>
#include <array>
#include <memory>
#include <type_traits>
#include <vector>

module carven:test.internal.runtime.passing;

namespace {

struct TrivialMoveOnly final {
    TrivialMoveOnly() = default;
    TrivialMoveOnly(const TrivialMoveOnly&) = delete;
    TrivialMoveOnly(TrivialMoveOnly&&) = default;
};

template<typename T>
concept TransferSource = requires (T& value) { carven::runtime::transfer(value); };

} // namespace

TEST_CASE("Value access: read parameters require no nontrivial copy") {
    using carven::runtime::ReadArg;
    static_assert(std::is_same_v<ReadArg<int>, const int>);
    static_assert(std::is_same_v<ReadArg<std::vector<int>>, const std::vector<int>&>);
    static_assert(std::is_trivially_copyable_v<TrivialMoveOnly>);
    static_assert(std::is_same_v<ReadArg<TrivialMoveOnly>, const TrivialMoveOnly&>);
    static_assert(std::is_same_v<ReadArg<std::array<int, 1024>>, const std::array<int, 1024>>);
    const auto owner = std::make_unique<int>(42);
    const auto read = [](ReadArg<std::unique_ptr<int>> value) static noexcept {
        return *value;
    };
    CHECK(read(owner) == 42);
    CHECK(*owner == 42);
}

TEST_CASE("Value transfer: owned resources enter the destination") {
    auto owner = std::make_unique<int>(42);
    const auto destination = carven::runtime::transfer(owner);
    CHECK(owner == nullptr);
    REQUIRE(destination != nullptr);
    CHECK(*destination == 42);

    auto values = std::vector<int> {1, 2, 3};
    const auto* allocation = values.data();
    const auto moved = carven::runtime::transfer(values);
    CHECK(moved.data() == allocation);
    CHECK(moved == std::vector<int> {1, 2, 3});
}

TEST_CASE("Value transfer: source capability excludes const owners") {
    static_assert(TransferSource<int>);
    static_assert(TransferSource<TrivialMoveOnly>);
    static_assert(!TransferSource<const int>);
    static_assert(!TransferSource<const std::unique_ptr<int>>);
    auto source = 42;
    const auto destination = carven::runtime::transfer(source);
    CHECK(destination == 42);
}
