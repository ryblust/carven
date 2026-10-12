module;
#ifndef NDEBUG
#define NDEBUG
#endif
#include <carven/runtime/diagnostics/report.hpp>

module carven:test.internal.runtime.assertion;

import :test.harness.death;
import :test.harness.framework;
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
