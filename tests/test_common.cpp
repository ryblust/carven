#include "doctest.h"

import carven.common.source;
import std;

TEST_CASE("Span") {
    SUBCASE("default is zeroed") {
        const auto s = Span{};
        CHECK_EQ(s.start, 0u);
        CHECK_EQ(s.end, 0u);
    }

    SUBCASE("normal range") {
        const auto s = Span { .start = 5, .end = 10 };
        CHECK_EQ(s.start, 5u);
        CHECK_EQ(s.end, 10u);
    }
}

TEST_CASE("is_empty") {
    SUBCASE("empty span (start == end)") {
        CHECK(is_empty(Span { .start = 3, .end = 3 }));
    }

    SUBCASE("invalid span (start > end)") {
        CHECK(is_empty(Span { .start = 5, .end = 2 }));
    }

    SUBCASE("non-empty span") {
        CHECK(!is_empty(Span { .start = 0, .end = 5 }));
    }
}

TEST_CASE("text_at") {
    static constexpr auto source = std::string_view("hello world\nfoo bar");

    SUBCASE("extract substring by span") {
        CHECK_EQ(text_at(source, Span { .start = 0, .end = 5 }), "hello");
        CHECK_EQ(text_at(source, Span { .start = 6, .end = 11 }), "world");
        CHECK_EQ(text_at(source, Span { .start = 12, .end = 15 }), "foo");
    }

    SUBCASE("whole source") {
        CHECK_EQ(text_at(source, Span { .start = 0, .end = 19 }), "hello world\nfoo bar");
    }

    SUBCASE("empty span returns empty") {
        CHECK_EQ(text_at(source, Span { .start = 3, .end = 3 }), "");
    }

    SUBCASE("start beyond size returns empty") {
        CHECK_EQ(text_at(source, Span { .start = 999, .end = 1000 }), "");
    }

    SUBCASE("end beyond size returns empty") {
        CHECK_EQ(text_at(source, Span { .start = 0, .end = 999 }), "");
    }

    SUBCASE("inverted span returns empty") {
        CHECK_EQ(text_at(source, Span { .start = 10, .end = 2 }), "");
    }
}

TEST_CASE("location_at") {
    static constexpr auto source = std::string_view("line1\nline2\nline3\n");

    SUBCASE("first line first char") {
        const auto loc = location_at(source, 0);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("first line mid char") {
        const auto loc = location_at(source, 3);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 4u);
    }

    SUBCASE("start of second line") {
        const auto loc = location_at(source, 6);
        CHECK_EQ(loc.line, 2u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("mid of second line") {
        const auto loc = location_at(source, 9);
        CHECK_EQ(loc.line, 2u);
        CHECK_EQ(loc.column, 4u);
    }

    SUBCASE("third line") {
        const auto loc = location_at(source, 12);
        CHECK_EQ(loc.line, 3u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("offset at end") {
        const auto loc = location_at(source, 18);
        CHECK_EQ(loc.line, 4u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("offset beyond end") {
        const auto loc = location_at(source, 999);
        CHECK_EQ(loc.line, 4u);
        CHECK_EQ(loc.column, 982u);
    }
}

TEST_CASE("SourceLocation formatting") {
    const auto loc = SourceLocation { .line = 5, .column = 12 };
    CHECK_EQ(std::format("{}", loc), "5:12");
}

TEST_CASE("SourceFile") {
    const auto sf = SourceFile { .filename = "test.cv", .content = "let x = 1;" };
    CHECK_EQ(sf.filename, "test.cv");
    CHECK_EQ(sf.content, "let x = 1;");
}
