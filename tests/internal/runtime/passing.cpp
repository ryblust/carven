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

struct ReadBase {
    int value;

    ~ReadBase() {}
};

struct ReadDerived final : ReadBase {};

struct ReadConverted final {
    int value;

    operator ReadBase() const noexcept { return ReadBase {value}; }
};

struct ReadFresh final {
    int value;

    operator ReadBase() && noexcept { return ReadBase {value}; }

    operator ReadBase() const& = delete;
};

struct ReadSnapshot final {
    int value;

    ReadSnapshot(int initial) noexcept
        : value(initial) {}

    ReadSnapshot(const ReadSnapshot&) = default;

    ReadSnapshot(ReadSnapshot&& source) noexcept
        : value(source.value + 1) {}
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

    "Read argument storage snapshots references and owns fresh backing"_test = [] static noexcept {
        using carven::runtime::ReadArgumentStorage;
        auto source = std::array<int, 1024> {};
        source[0] = 7;
        ReadArgumentStorage<decltype((source))> snapshot = source;
        source[0] = 9;
        expect(snapshot[0] == 7);

        auto borrowed = std::vector<int> {11};
        ReadArgumentStorage<decltype((borrowed))> alias = borrowed;
        borrowed[0] = 13;
        expect(alias[0] == 13);
        expect(std::addressof(alias) == std::addressof(borrowed));

        ReadArgumentStorage<std::vector<int>> owned = std::vector<int> {17};
        expect(owned[0] == 17);
    };

    "Read conversions borrow standard projections and own conversion results"_test =
        [] static noexcept {
            using carven::runtime::ReadArg;
            using carven::runtime::read_conversion;
            auto source = ReadDerived {ReadBase {7}};
            decltype(auto) projected =
                read_conversion<ReadArg<ReadBase>, const ReadDerived&>(source);
            source.value = 9;
            expect(projected.value == 9);
            expect(std::addressof(projected) == static_cast<ReadBase*>(std::addressof(source)));

            auto converted_source = ReadConverted {11};
            decltype(auto) converted =
                read_conversion<ReadArg<ReadBase>, const ReadConverted&>(converted_source);
            converted_source.value = 13;
            expect(converted.value == 11);

            auto fresh = ReadFresh {17};
            auto delivered = read_conversion<ReadArg<ReadBase>, ReadFresh>(fresh);
            expect(delivered.value == 17);

            auto snapshot_source = ReadSnapshot {19};
            auto snapshot = read_conversion<ReadArg<ReadSnapshot>, ReadSnapshot>(snapshot_source);
            expect(snapshot.value == 19);
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
