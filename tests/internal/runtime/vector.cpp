module;
#include <carven/runtime/vector.hpp>

module carven:test.internal.runtime.vector;

import :test.harness.framework;
import std;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

const TestSuite suite([] static noexcept {
    "Vector: bool views expose ordinary contiguous elements"_test = [] static noexcept {
        auto values = carven::runtime::Vector<bool>();
        values.push(true);
        values.push(false);
        const auto view = carven::runtime::as_slice(values);
        expect(view.data() == &values.at(0, site));
        expect(view.data() + 1 == &values.at(1, site));
        expect(view[0]);
        expect(!view[1]);
        auto copy = values;
        copy.at(0, site) = false;
        expect(values.at(0, site));
        expect(carven::runtime::as_slice(copy).data() != view.data());
    };

    "Vector: reservation and extraction preserve storage and order"_test = [] static noexcept {
        auto values = carven::runtime::Vector<std::string>::with_capacity(8);
        values.push("first");
        values.push("second");
        const auto* backing = values.begin();
        const auto capacity = values.capacity();
        values.push(values.at(0, site));
        expect(values.begin() == backing);
        expect_equal(values.remove(1, site), std::string("second"));
        expect_equal(values.at(1, site), std::string("first"));
        values.reserve(1);
        expect(values.begin() == backing);
        expect_equal(values.capacity(), capacity);
        values.clear();
        expect(values.begin() == backing);
        expect(values.empty());
        expect_equal(values.capacity(), capacity);
        values.push("last");
        auto moved = std::move(values);
        expect(moved.begin() == backing);
        expect_equal(moved.at(0, site), std::string("last"));
    };

    "Vector: retained backing stays immutable across owning copies and assignment"_test =
        [] static noexcept {
            static constexpr auto elements = std::array {3, 5};
            constexpr auto retained =
                carven::runtime::Vector<int>::from_static(carven::runtime::as_slice(elements), 8);
            auto copy = retained;
            copy.at(0, site) = 7;
            auto assigned = carven::runtime::Vector<int>::with_capacity(4);
            assigned.push(11);
            assigned = retained;
            assigned.push(13);
            expect(retained.begin() == elements.data());
            expect_equal(retained.capacity(), std::size_t(8));
            expect_equal(retained.at(0, site), 3);
            expect_equal(copy.at(0, site), 7);
            expect_equal(assigned.at(0, site), 3);
            expect_equal(assigned.at(2, site), 13);
        };
});

} // namespace
