#include "test_helpers.h"

import carven.common.source;
import std;

TEST_CASE("Span") {
    SUBCASE("construction and empty check") {
        const auto s = Span{};
        CHECK_EQ(s.start, 0u);
        CHECK_EQ(s.end, 0u);
        const auto r = Span { .start = 5, .end = 10 };
        CHECK_EQ(r.start, 5u);
        CHECK_EQ(r.end, 10u);
        CHECK( Span { .start = 3, .end = 3 }.empty());
        CHECK( Span { .start = 5, .end = 2 }.empty());
        CHECK(!Span { .start = 0, .end = 5 }.empty());
    }
}

TEST_CASE("slice") {
    constexpr auto text = "hello world\nfoo bar";

    SUBCASE("extract substring by span") {
        CHECK_EQ(slice(text, Span { .start = 0,  .end =  5 }), "hello");
        CHECK_EQ(slice(text, Span { .start = 6,  .end = 11 }), "world");
        CHECK_EQ(slice(text, Span { .start = 12, .end = 15 }),   "foo");
    }

    SUBCASE("whole source") {
        CHECK_EQ(slice(text, Span { .start = 0, .end = 19 }), "hello world\nfoo bar");
    }

    SUBCASE("empty span returns empty") {
        CHECK_EQ(slice(text, Span { .start = 3, .end = 3 }), "");
    }

    SUBCASE("start beyond size returns empty") {
        CHECK_EQ(slice(text, Span { .start = 999, .end = 1000 }), "");
    }

    SUBCASE("end beyond size returns empty") {
        CHECK_EQ(slice(text, Span { .start = 0, .end = 999 }), "");
    }

    SUBCASE("inverted span returns empty") {
        CHECK_EQ(slice(text, Span { .start = 10, .end = 2 }), "");
    }
}

TEST_CASE("LineOffsets") {
    SUBCASE("line 1 and 2 positions") {
        const auto lo = LineOffsets("line1\nline2\nline3\n");
        CHECK_EQ(lo.location(0).line,   1u);
        CHECK_EQ(lo.location(0).column, 1u);
        CHECK_EQ(lo.location(3).line,   1u);
        CHECK_EQ(lo.location(3).column, 4u);
        CHECK_EQ(lo.location(6).line,   2u);
        CHECK_EQ(lo.location(9).line,   2u);
        CHECK_EQ(lo.location(9).column, 4u);
    }

    SUBCASE("line 3 and beyond") {
        const auto lo = LineOffsets("line1\nline2\nline3\n");
        CHECK_EQ(lo.location(12).line, 3u);
        CHECK_EQ(lo.location(18).line, 4u);
    }

    SUBCASE("single line no newline") {
        const auto lo = LineOffsets("hello");
        CHECK_EQ(lo.location(0).line,   1u);
        CHECK_EQ(lo.location(0).column, 1u);
        CHECK_EQ(lo.location(4).line,   1u);
        CHECK_EQ(lo.location(4).column, 5u);
    }

    SUBCASE("offset far beyond source length") {
        const auto lo = LineOffsets("hello\nworld");
        const auto loc = lo.location(9999);
        CHECK_EQ(loc.line, 2u);
        CHECK_EQ(loc.column, 9994u);
    }
}

TEST_CASE("SourceFile metadata") {
    const auto file = SourceFile("let x = 1;", "test.cv");
    CHECK_EQ(file.filepath(), "test.cv");
    CHECK_EQ(file.text(), "let x = 1;");
}

TEST_CASE("SourceFile empty content") {
    const auto source = SourceFile("", "empty.cv");

    SUBCASE("text is empty") {
        CHECK_EQ(source.text(), "");
    }
}

TEST_CASE("SourceFile move constructor") {
    auto original = SourceFile("moved content", "moved.cv");
    const auto moved = std::move(original);

    SUBCASE("moved-to has correct data") {
        CHECK_EQ(moved.text(), "moved content");
        CHECK_EQ(moved.filepath(), "moved.cv");
    }

    SUBCASE("moved-from is empty") {
        CHECK_EQ(original.text(), "");
    }
}

TEST_CASE("SourceFile move assignment") {
    auto a = SourceFile("first", "a.cv");
    auto b = SourceFile("second", "b.cv");

    a = std::move(b);

    SUBCASE("assigned-to has correct data") {
        CHECK_EQ(a.text(), "second");
        CHECK_EQ(a.filepath(), "b.cv");
    }

    SUBCASE("assigned-from is empty") {
        CHECK_EQ(b.text(), "");
    }
}

TEST_CASE("SourceFile from_file mmap path") {
    auto source = SourceFile::from_file("tests/fixtures/helloworld.cv");
    REQUIRE(source.has_value());

    SUBCASE("text is non-empty") {
        CHECK(!source->text().empty());
    }

    SUBCASE("filepath is preserved") {
        CHECK_EQ(source->filepath(), "tests/fixtures/helloworld.cv");
    }

    SUBCASE("slice works") {
        const auto text = source->text();
        const auto nl = static_cast<std::uint32_t>(text.find('\n'));
        const auto first_line = slice(text, Span { .start = 0, .end = nl });
        CHECK(!first_line.empty());
    }

    SUBCASE("move from_file result") {
        const auto moved = std::move(*source);
        CHECK(!moved.text().empty());
        CHECK_EQ(moved.filepath(), "tests/fixtures/helloworld.cv");
    }
}

TEST_CASE("SourceFile self-move assignment") {
    auto source = SourceFile("self move test", "self.cv");
    auto& ref = source;
    source = std::move(ref);
    CHECK_EQ(source.text(), "self move test");
}

TEST_CASE("SourceFile from_file nonexistent") {
    const auto source = SourceFile::from_file("nonexistent_file_12345.cv");
    CHECK(!source.has_value());
}

TEST_CASE("SourceFile from_file empty file") {
    const auto dir  = std::filesystem::temp_directory_path();
    const auto path = dir / "carven_test_empty.cv";

    {
        std::ofstream ofs(path, std::ios::trunc);
        ofs.close();
    }

    const auto source = SourceFile::from_file(path.string());
    REQUIRE(source.has_value());
    CHECK_EQ(source->text(), "");

    std::filesystem::remove(path);
}
