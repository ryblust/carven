module;
#ifndef NDEBUG
#define NDEBUG
#endif
#include <carven/runtime/report.hpp>

module carven:test.internal.runtime.assertion;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Runtime: assertions terminate even with NDEBUG defined"_test = [] static noexcept {
        expect(expect_termination("assertion-ndebug", []() static noexcept {
            carven::runtime::assertion_failed(
                {"source.cv", 7, 3},
                "assert",
                "false",
                "failure",
                ""
            );
        }));
    };
});

} // namespace
