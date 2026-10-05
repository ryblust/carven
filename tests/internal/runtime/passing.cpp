module;
#include <carven/runtime/passing.hpp>
#include <array>
#include <memory>
#include <type_traits>
#include <vector>

module carven:test.internal.runtime.passing;

import :test.harness.framework;

namespace {

struct TrivialMoveOnly final {
    TrivialMoveOnly() = default;
    TrivialMoveOnly(const TrivialMoveOnly&) = delete;
    TrivialMoveOnly(TrivialMoveOnly&&) = default;
};

template<typename T>
concept TransferSource = requires (T& value) { carven::runtime::transfer(value); };


const TestSuite suite([] static noexcept {
    "Value access: read parameters require no nontrivial copy"_test = [] static noexcept {
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
        expect(read(owner) == 42);
        expect(*owner == 42);
    };

    "Value transfer: owned resources enter the destination"_test = [] static noexcept {
        auto owner = std::make_unique<int>(42);
        const auto destination = carven::runtime::transfer(owner);
        expect(owner == nullptr);
        if (!expect(destination != nullptr)) {
            return;
        }
        expect(*destination == 42);

        auto values = std::vector<int> {1, 2, 3};
        const auto* allocation = values.data();
        const auto moved = carven::runtime::transfer(values);
        expect(moved.data() == allocation);
        expect(moved == std::vector<int> {1, 2, 3});
    };

    "Value transfer: source capability excludes const owners"_test = [] static noexcept {
        static_assert(TransferSource<int>);
        static_assert(TransferSource<TrivialMoveOnly>);
        static_assert(!TransferSource<const int>);
        static_assert(!TransferSource<const std::unique_ptr<int>>);
        auto source = 42;
        const auto destination = carven::runtime::transfer(source);
        expect(destination == 42);
    };
});

} // namespace
