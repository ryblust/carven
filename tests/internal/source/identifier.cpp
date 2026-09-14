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
    CHECK(std::holds_alternative<OrdinaryIdentifier>(classify_identifier("module_42")));
    CHECK(std::holds_alternative<OrdinaryIdentifier>(classify_identifier("craft")));
    CHECK(std::holds_alternative<InvalidIdentifier>(classify_identifier("hyphen-name")));
    CHECK(std::holds_alternative<InvalidIdentifier>(classify_identifier("42module")));
}
