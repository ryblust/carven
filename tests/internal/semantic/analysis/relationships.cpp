module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.relationships;

import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Semantic relationships: unknown element projection preserves set identity") {
    const auto program = analyze_test_program("fn source() {}");
    for (const auto [id, body] : program.bodies().entries()) {
        static_cast<void>(id);
        const auto origin = body.region().origin;
        const auto backing = OwnershipPlace {0uz, {}};
        const auto source = OwnershipRelationships {
            .callable_loans =
                {{{0uz}, backing, std::nullopt, origin, false},
                 {{1uz}, backing, std::nullopt, origin, false}},
            .captures = {{{0uz}, backing, origin}, {{1uz}, backing, origin}},
            .text_loans = {{{0uz}, backing, origin}, {{1uz}, backing, origin}}
        };
        const auto result = project_relationships(source, {std::nullopt});
        REQUIRE(result.callable_loans.size() == 1uz);
        REQUIRE(result.captures.size() == 1uz);
        REQUIRE(result.text_loans.size() == 1uz);
        CHECK(result.text_loans.front().holder.empty());
        CHECK(result.callable_loans.front().holder.empty());
        CHECK(result.captures.front().holder.empty());
        auto merged = result;
        merge_relationships(merged, result);
        CHECK(merged == result);
    }
}

TEST_CASE("Semantic relationships: equivalent facts retain a stable diagnostic origin") {
    const auto program = analyze_test_program("fn first() {} fn second() {}");
    auto origins = std::vector<ProgramOriginID>();
    for (const auto [id, body] : program.bodies().entries()) {
        static_cast<void>(id);
        origins.push_back(body.region().origin);
    }
    REQUIRE(origins.size() == 2uz);
    std::ranges::sort(origins);
    const auto backing = OwnershipPlace {0uz, {}};
    auto first = OwnershipRelationships {
        .callable_loans =
            {{{}, backing, std::nullopt, origins[1], true},
             {{}, backing, std::nullopt, origins[0], false}},
        .captures = {{{}, backing, origins[1]}, {{}, backing, origins[0]}},
        .text_loans = {{{}, backing, origins[1]}, {{}, backing, origins[0]}}
    };
    for (auto& loan : first.callable_loans) {
        loan.direct_only = false;
    }
    auto second = first;
    std::ranges::reverse(second.callable_loans);
    std::ranges::reverse(second.captures);
    std::ranges::reverse(second.text_loans);
    normalize_relationships(first);
    normalize_relationships(second);
    CHECK(first == second);
    REQUIRE(first.callable_loans.size() == 1uz);
    REQUIRE(first.captures.size() == 1uz);
    REQUIRE(first.text_loans.size() == 1uz);
    CHECK(first.text_loans.front().origin == origins.front());
    CHECK(first.callable_loans.front().origin == origins.front());
    CHECK(second.callable_loans.front().origin == origins.front());
    CHECK(first.captures.front().origin == origins.front());
    CHECK(second.captures.front().origin == origins.front());
}
