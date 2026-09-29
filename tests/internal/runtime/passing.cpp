module;
#include <carven/runtime/passing.hpp>
#include <array>
#include <memory>
#include <type_traits>
#include <vector>

module carven:test.internal.runtime.passing;

import :test.harness.framework;

namespace {

namespace ct = carven::testing;

struct TrivialMoveOnly final {
    TrivialMoveOnly() = default;
    TrivialMoveOnly(const TrivialMoveOnly&) = delete;
    TrivialMoveOnly(TrivialMoveOnly&&) = default;
};

template<typename T>
concept TransferSource = requires (T& value) { carven::runtime::transfer(value); };

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Value access: read parameters require no nontrivial copy", [] static noexcept {
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
        ct::expect(read(owner) == 42);
        ct::expect(*owner == 42);
    });

    ct::test("Value transfer: owned resources enter the destination", [] static noexcept {
        auto owner = std::make_unique<int>(42);
        const auto destination = carven::runtime::transfer(owner);
        ct::expect(owner == nullptr);
        if (!ct::expect(destination != nullptr)) {
            return;
        }
        ct::expect(*destination == 42);

        auto values = std::vector<int> {1, 2, 3};
        const auto* allocation = values.data();
        const auto moved = carven::runtime::transfer(values);
        ct::expect(moved.data() == allocation);
        ct::expect(moved == std::vector<int> {1, 2, 3});
    });

    ct::test("Value transfer: source capability excludes const owners", [] static noexcept {
        static_assert(TransferSource<int>);
        static_assert(TransferSource<TrivialMoveOnly>);
        static_assert(!TransferSource<const int>);
        static_assert(!TransferSource<const std::unique_ptr<int>>);
        auto source = 42;
        const auto destination = carven::runtime::transfer(source);
        ct::expect(destination == 42);
    });
});

} // namespace
