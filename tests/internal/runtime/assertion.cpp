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

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Runtime: assertions terminate even with NDEBUG defined", [] static noexcept {
        ct::expect(expect_termination("assertion-ndebug", []() static noexcept {
            carven::runtime::assertion_failed(
                {"source.cv", 7, 3},
                "assert",
                "false",
                "failure",
                ""
            );
        }));
    });
});

} // namespace
