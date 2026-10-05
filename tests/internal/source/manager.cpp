module carven:test.internal.source.manager;

import :source.location;
import :source.manager;
import :source.text;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Source manager: virtual sources own text and provide stable source-aware locations"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual("first.cv", "alpha\nbeta");
            const auto second = *sources.append_virtual("second.cv", "gamma");

            expect_equal(first.index(), 0u);
            expect_equal(second.index(), 1u);
            expect_equal(sources.view(first).origin, std::string_view("first.cv"));
            expect_equal(
                sources.slice(locate(first, Span::from_bounds(6, 10))),
                std::string_view("beta")
            );
            const auto first_location = sources.location(locate(first, Span::from_bounds(7, 7)));
            expect_equal(first_location.line, 2u);
            expect_equal(first_location.column, 2u);
        };

    "Source manager: file sources own an immutable snapshot through the same ID model"_test =
        [] static noexcept {
            const auto temporary = TempDirectory("carven-source-test");
            const auto file_path = temporary.path("source.cv");
            auto output = std::ofstream(file_path, std::ios::binary | std::ios::trunc);
            if (!expect(output.is_open())) {
                return;
            }
            output.write("first\nsecond", 12);
            output.close();
            if (!expect(output.good())) {
                return;
            }
            const auto encoded_path = path_to_generic_utf8(file_path);

            auto sources = SourceManager();
            const auto source = sources.append_file(encoded_path);
            if (!expect(source.has_value())) {
                return;
            }
            expect_equal(source->index(), 0u);
            expect_equal(sources.view(*source).origin, encoded_path);
            expect_equal(sources.view(*source).text, std::string_view("first\nsecond"));
            expect_equal(
                sources.slice(locate(*source, Span::from_bounds(6, 12))),
                std::string_view("second")
            );
            const auto second_location = sources.location(locate(*source, Span::from_bounds(8, 8)));
            expect_equal(second_location.line, 2u);
            expect_equal(second_location.column, 3u);

            auto changed = std::ofstream(file_path, std::ios::binary | std::ios::trunc);
            if (!expect(changed.is_open())) {
                return;
            }
            changed.write("changed", 7);
            changed.close();
            if (!expect(changed.good())) {
                return;
            }
            expect_equal(sources.view(*source).text, std::string_view("first\nsecond"));
        };

    "Source manager: load failures retain origin and system detail"_test = [] static noexcept {
        const auto temporary = TempDirectory("carven-source-test");
        const auto missing_path = path_to_generic_utf8(temporary.path("missing.cv"));
        auto sources = SourceManager();
        const auto source = sources.append_file(missing_path);
        if (!expect(!source.has_value())) {
            return;
        }
        expect_equal(source.error().origin, std::string_view(missing_path));
        expect(source.error().message.starts_with("cannot read source file: "));
        expect_greater(
            source.error().message.size(),
            std::string_view("cannot read source file: ").size()
        );
    };

    "Source manager: existing views remain stable across later insertions"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual("first.cv", "stable");
            const auto view = sources.view(first);

            for (auto index = 0; index < 1024; ++index) {
                if (!expect(sources
                                .append_virtual(
                                    std::format("{}.cv", index),
                                    std::format("text {}", index)
                                )
                                .has_value())) {
                    return;
                }
            }

            expect_equal(view.origin, std::string_view("first.cv"));
            expect_equal(view.text, std::string_view("stable"));
            expect_equal(sources.view(first).text, std::string_view("stable"));
        };
});

} // namespace
