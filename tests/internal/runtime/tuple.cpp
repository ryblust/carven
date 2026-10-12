module;
#include <carven/runtime/value/deferred.hpp>
#include <carven/runtime/value/tuple.hpp>

module carven:test.internal.runtime.tuple;

import :test.harness.framework;
import std;

namespace {

class TupleFixed final {
public:
    explicit TupleFixed(int value, std::vector<int>& trace) noexcept;
    ~TupleFixed() noexcept;
    TupleFixed(const TupleFixed&) = delete;
    TupleFixed(TupleFixed&&) = delete;
    auto operator=(const TupleFixed&) -> TupleFixed& = delete;
    auto operator=(TupleFixed&&) -> TupleFixed& = delete;
    auto stable() const noexcept -> bool;

private:
    int value;
    std::vector<int>& trace;
    const TupleFixed* address;
};

TupleFixed::TupleFixed(int input, std::vector<int>& events) noexcept
    : value(input),
      trace(events),
      address(this) {
    trace.push_back(value);
}

TupleFixed::~TupleFixed() noexcept {
    trace.push_back(-value);
}

auto TupleFixed::stable() const noexcept -> bool {
    return address == this;
}

const TestSuite suite([] static noexcept {
    "Runtime: tuple directly constructs immovable slots and closes in reverse order"_test =
        [] static noexcept {
            using namespace carven::runtime;
            using Product = Tuple<TupleFixed, Void, TupleFixed>;
            auto trace = std::vector<int>();
            {
                auto storage = DeferredResult<Product>();
                storage.initialize([&]() noexcept -> Product {
                    return {TupleFixed {1, trace}, {Void {}, {TupleFixed {2, trace}, {}}}};
                });
                expect(trace == std::vector<int> {1, 2});
                expect(tuple_get<0>(*storage).stable());
                expect(tuple_get<2>(*storage).stable());
                expect(tuple_get<1>(*storage) == Void {});
            }
            expect(trace == std::vector<int> {1, 2, -2, -1});
        };

    "Runtime: tuple projection preserves cv references and logical void positions"_test =
        [] static noexcept {
            using namespace carven::runtime;
            auto value = Tuple<int, Void, Tuple<int>> {4, {{}, {{7, {}}, {}}}};
            static_assert(std::same_as<decltype(tuple_get<0>(value)), int&>);
            static_assert(std::same_as<decltype(tuple_get<0>(std::as_const(value))), const int&>);
            static_assert(std::same_as<decltype(tuple_get<0>(std::move(value))), int&&>);
            static_assert(std::same_as<decltype(tuple_get<1>(value)), Void&>);
            tuple_get<0>(tuple_get<2>(value)) = 9;
            expect_equal(tuple_get<0>(tuple_get<2>(value)), 9);
            expect(value == Tuple<int, Void, Tuple<int>> {4, {{}, {{9, {}}, {}}}});
        };

    "Runtime: native materialization executes void once and preserves native result storage"_test =
        [] static noexcept {
            using namespace carven::runtime;
            auto calls = 0;
            const auto empty = materialize_native([&]() noexcept -> void { ++calls; });
            expect(empty == Void {});
            expect_equal(calls, 1);
            auto number = 11;
            auto& reference = materialize_native([&]() noexcept -> int& { return number; });
            reference = 12;
            expect_equal(number, 12);
            auto trace = std::vector<int>();
            {
                const auto fixed = materialize_native([&]() noexcept -> TupleFixed {
                    return TupleFixed {3, trace};
                });
                expect(fixed.stable());
                expect(trace == std::vector<int> {3});
            }
            expect(trace == std::vector<int> {3, -3});
        };
});

} // namespace
