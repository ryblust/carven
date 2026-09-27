module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.identifier;

import :source.identifier;
import std;

TEST_CASE("Source identifier: classification is independent of token kinds") {
    const auto keyword = classify_identifier("match");
    REQUIRE(std::holds_alternative<KeywordIdentifier>(keyword));
    CHECK_EQ(std::get<KeywordIdentifier>(keyword).keyword, SourceKeyword::Match);
    static constexpr auto ordinary_ids = std::to_array<std::string_view>({
        "name",
        "_",
        "__value",
        "__carven_module",
        "value_42",
        "module_42",
        "cv",
        "craft",
    });
    for (const auto& spelling : ordinary_ids) {
        CAPTURE(spelling);
        CHECK(std::holds_alternative<OrdinaryIdentifier>(classify_identifier(spelling)));
    }

    static constexpr auto keyword_ids = std::to_array<std::string_view>({
        "import",
        "match",
        "private",
        "while",
    });
    for (const auto& spelling : keyword_ids) {
        CAPTURE(spelling);
        CHECK(std::holds_alternative<KeywordIdentifier>(classify_identifier(spelling)));
    }

    static constexpr auto invalid_ids = std::to_array<std::string_view>({
        "",
        "42name",
        "42module",
        "hyphen-name",
        "dot.name",
        "变量",
    });
    for (const auto& spelling : invalid_ids) {
        CAPTURE(spelling);
        CHECK(std::holds_alternative<InvalidIdentifier>(classify_identifier(spelling)));
    }
}
