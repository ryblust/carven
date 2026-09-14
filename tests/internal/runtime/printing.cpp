module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/print.hpp>

module carven:test.internal.runtime.printing;

import std;

TEST_CASE("Runtime printing: text arguments retain separators Unicode and NUL") {
    const auto stream =
        std::unique_ptr<std::FILE, decltype(&std::fclose)>(std::tmpfile(), &std::fclose);
    REQUIRE(stream != nullptr);
    carven::runtime::detail::print_values<true>(
        stream.get(),
        std::string_view("42"),
        std::string_view("true"),
        std::string_view("我"),
        std::string_view("\0", 1),
        std::string_view("raw")
    );
    std::rewind(stream.get());
    auto bytes = std::array<char, 64uz>();
    const auto size = std::fread(bytes.data(), 1uz, bytes.size(), stream.get());
    CHECK(std::string_view(bytes.data(), size) == std::string_view("42 true 我 \0 raw\n", 18uz));
}
