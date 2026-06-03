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

TEST_CASE("Span::empty") {
    SUBCASE("empty span (start == end)") {
        CHECK(Span { .start = 3, .end = 3 }.empty());
    }

    SUBCASE("invalid span (start > end)") {
        CHECK(Span { .start = 5, .end = 2 }.empty());
    }

    SUBCASE("non-empty span") {
        CHECK(!Span { .start = 0, .end = 5 }.empty());
    }
}

TEST_CASE("SourceFile::slice") {
    const auto source = SourceFile("hello world\nfoo bar", "test.cv");

    SUBCASE("extract substring by span") {
        CHECK_EQ(source.slice(Span { .start = 0, .end = 5 }), "hello");
        CHECK_EQ(source.slice(Span { .start = 6, .end = 11 }), "world");
        CHECK_EQ(source.slice(Span { .start = 12, .end = 15 }), "foo");
    }

    SUBCASE("whole source") {
        CHECK_EQ(source.slice(Span { .start = 0, .end = 19 }), "hello world\nfoo bar");
    }

    SUBCASE("empty span returns empty") {
        CHECK_EQ(source.slice(Span { .start = 3, .end = 3 }), "");
    }

    SUBCASE("start beyond size returns empty") {
        CHECK_EQ(source.slice(Span { .start = 999, .end = 1000 }), "");
    }

    SUBCASE("end beyond size returns empty") {
        CHECK_EQ(source.slice(Span { .start = 0, .end = 999 }), "");
    }

    SUBCASE("inverted span returns empty") {
        CHECK_EQ(source.slice(Span { .start = 10, .end = 2 }), "");
    }
}

TEST_CASE("SourceFile::location") {
    const auto source = SourceFile("line1\nline2\nline3\n", "test.cv");

    SUBCASE("first line first char") {
        const auto loc = source.location(0);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("first line mid char") {
        const auto loc = source.location(3);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 4u);
    }

    SUBCASE("start of second line") {
        const auto loc = source.location(6);
        CHECK_EQ(loc.line, 2u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("mid of second line") {
        const auto loc = source.location(9);
        CHECK_EQ(loc.line, 2u);
        CHECK_EQ(loc.column, 4u);
    }

    SUBCASE("third line") {
        const auto loc = source.location(12);
        CHECK_EQ(loc.line, 3u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("offset at end") {
        const auto loc = source.location(18);
        CHECK_EQ(loc.line, 4u);
        CHECK_EQ(loc.column, 1u);
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

    SUBCASE("slice returns empty") {
        CHECK_EQ(source.slice(Span { .start = 0, .end = 0 }), "");
    }
}

TEST_CASE("SourceFile single line no newline") {
    const auto source = SourceFile("hello", "test.cv");

    SUBCASE("text") {
        CHECK_EQ(source.text(), "hello");
    }

    SUBCASE("slice") {
        CHECK_EQ(source.slice(Span { .start = 0, .end = 5 }), "hello");
        CHECK_EQ(source.slice(Span { .start = 1, .end = 4 }), "ell");
    }

    SUBCASE("location") {
        const auto loc = source.location(0);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 1u);

        const auto loc2 = source.location(4);
        CHECK_EQ(loc2.line, 1u);
        CHECK_EQ(loc2.column, 5u);
    }
}

TEST_CASE("SourceFile move constructor") {
    auto original = SourceFile("moved content", "moved.cv");
    const auto moved = std::move(original);

    SUBCASE("moved-to has correct data") {
        CHECK_EQ(moved.text(), "moved content");
        CHECK_EQ(moved.filepath(), "moved.cv");
    }

    SUBCASE("moved-to location works") {
        const auto loc = moved.location(0);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 1u);
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
    auto source = SourceFile::from_file("tests/helloworld.cv");
    REQUIRE(source.has_value());

    SUBCASE("text is non-empty") {
        CHECK(!source->text().empty());
    }

    SUBCASE("filepath is preserved") {
        CHECK_EQ(source->filepath(), "tests/helloworld.cv");
    }

    SUBCASE("location on first line") {
        const auto loc = source->location(0);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 1u);
    }

    SUBCASE("slice works") {
        const auto nl = static_cast<std::uint32_t>(source->text().find('\n'));
        const auto first_line = source->slice(Span { .start = 0, .end = nl });
        CHECK(!first_line.empty());
    }

    SUBCASE("move from_file result") {
        const auto moved = std::move(*source);
        CHECK(!moved.text().empty());
        CHECK_EQ(moved.filepath(), "tests/helloworld.cv");
    }
}

TEST_CASE("SourceFile self-move assignment") {
    auto source = SourceFile("self move test", "self.cv");
    auto& ref = source;
    source = std::move(ref);
    CHECK_EQ(source.text(), "self move test");
}

TEST_CASE("SourceFile move assignment with location") {
    auto a = SourceFile("location test", "a.cv");
    auto b = SourceFile("other", "b.cv");

    a = std::move(b);

    SUBCASE("assigned-to location works") {
        const auto loc = a.location(0);
        CHECK_EQ(loc.line, 1u);
        CHECK_EQ(loc.column, 1u);
    }
}

TEST_CASE("SourceFile from_file nonexistent") {
    auto source = SourceFile::from_file("nonexistent_file_12345.cv");
    CHECK(!source.has_value());
}

TEST_CASE("SourceFile from_file empty file") {
    const auto dir  = std::filesystem::temp_directory_path();
    const auto path = dir / "carven_test_empty.cv";

    {
        std::ofstream ofs(path, std::ios::trunc);
        ofs.close();
    }

    auto source = SourceFile::from_file(path.string());
    REQUIRE(source.has_value());
    CHECK_EQ(source->text(), "");

    std::filesystem::remove(path);
}

TEST_CASE("SourceFile::location beyond source") {
    const auto source = SourceFile("hello\nworld", "test.cv");

    SUBCASE("offset far beyond source length") {
        const auto loc = source.location(9999);
        CHECK_EQ(loc.line, 2u);
        CHECK_EQ(loc.column, 9994u);
    }
}
