module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cstdlib>
#endif

module carven:test.internal.support.process;

import :support.process;
import :test.harness.framework;
import std;

namespace {

constexpr auto variable = "CARVEN_TEST_PROCESS_TEXT";

#if defined(_WIN32)
auto set_value(const wchar_t* value) noexcept -> bool {
    return SetEnvironmentVariableW(L"CARVEN_TEST_PROCESS_TEXT", value) != 0;
}
#else
auto set_value(const char* value) noexcept -> bool {
    return value == nullptr ? unsetenv(variable) == 0 : setenv(variable, value, 1) == 0;
}
#endif

const TestSuite suite([] static noexcept {
    "Support process: environment values own UTF-8 and distinguish empty from absent"_test =
        [] static noexcept {
#if defined(_WIN32)
            require(set_value(L"工具 🌍"));
#else
            require(set_value("工具 🌍"));
#endif
            const auto unicode = process::environment(variable);
#if defined(_WIN32)
            require(set_value(L""));
#else
            require(set_value(""));
#endif
            const auto empty = process::environment(variable);
            require(set_value(nullptr));
            const auto absent = process::environment(variable);

            if (expect(unicode.has_value())) {
                expect_equal(*unicode, std::optional<std::string>("工具 🌍"));
            }
            if (expect(empty.has_value())) {
                expect_equal(*empty, std::optional<std::string>(""));
            }
            if (expect(absent.has_value())) {
                expect(!absent->has_value());
            }

#if defined(_WIN32)
            require(set_value(L"\xd800"));
#else
            require(set_value("\xff"));
#endif
            const auto invalid = process::environment(variable);
            require(set_value(nullptr));
            expect(!invalid.has_value());
        };
});

} // namespace
