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

TEST_CASE("Runtime printing: structural display escapes text and bounds sequences and UTF8") {
    auto writer = carven::runtime::DisplayWriter();
    writer.quoted(std::string_view("a\0\"\\\n", 5));
    CHECK(writer.result() == "\"a\\0\\\"\\\\\\n\"");

    auto sequence = carven::runtime::DisplayWriter();
    const auto values = std::array<int, 65>();
    sequence
        .sequence(values, [](auto& output, int value) static noexcept { output.scalar(value); }, 0);
    CHECK(sequence.result().starts_with("[\n    0,\n"));
    CHECK(sequence.result().ends_with("\n    ...,\n]"));

    auto empty = carven::runtime::DisplayWriter();
    empty.sequence(
        std::array<int, 0> {},
        [](auto& output, int value) static noexcept { output.scalar(value); },
        0
    );
    CHECK(empty.result() == "[]");

    auto bounded = carven::runtime::DisplayWriter();
    bounded.text(std::string(16383, 'a'));
    bounded.text("我");
    CHECK(bounded.result().size() == 16386);
    CHECK(bounded.result().ends_with("a..."));
}
