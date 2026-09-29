module carven:test.internal.source.manager;

import :source.location;
import :source.manager;
import :source.text;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Source manager: virtual sources own text and provide stable source-aware locations",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual("first.cv", "alpha\nbeta");
            const auto second = *sources.append_virtual("second.cv", "gamma");

            ct::expect_equal(first.index(), 0u);
            ct::expect_equal(second.index(), 1u);
            ct::expect_equal(sources.view(first).origin, std::string_view("first.cv"));
            ct::expect_equal(
                sources.slice(locate(first, Span::from_bounds(6, 10))),
                std::string_view("beta")
            );
            const auto first_location = sources.location(locate(first, Span::from_bounds(7, 7)));
            ct::expect_equal(first_location.line, 2u);
            ct::expect_equal(first_location.column, 2u);
        }
    );

    ct::test(
        "Source manager: file sources own an immutable snapshot through the same ID model",
        [] static noexcept {
            const auto temporary = ct::TempDirectory("carven-source-test");
            const auto file_path = temporary.path("source.cv");
            auto output = std::ofstream(file_path, std::ios::binary | std::ios::trunc);
            if (!ct::expect(output.is_open())) {
                return;
            }
            output.write("first\nsecond", 12);
            output.close();
            if (!ct::expect(output.good())) {
                return;
            }
            const auto encoded_path = path_to_generic_utf8(file_path);

            auto sources = SourceManager();
            const auto source = sources.append_file(encoded_path);
            if (!ct::expect(source.has_value())) {
                return;
            }
            ct::expect_equal(source->index(), 0u);
            ct::expect_equal(sources.view(*source).origin, encoded_path);
            ct::expect_equal(sources.view(*source).text, std::string_view("first\nsecond"));
            ct::expect_equal(
                sources.slice(locate(*source, Span::from_bounds(6, 12))),
                std::string_view("second")
            );
            const auto second_location = sources.location(locate(*source, Span::from_bounds(8, 8)));
            ct::expect_equal(second_location.line, 2u);
            ct::expect_equal(second_location.column, 3u);

            auto changed = std::ofstream(file_path, std::ios::binary | std::ios::trunc);
            if (!ct::expect(changed.is_open())) {
                return;
            }
            changed.write("changed", 7);
            changed.close();
            if (!ct::expect(changed.good())) {
                return;
            }
            ct::expect_equal(sources.view(*source).text, std::string_view("first\nsecond"));
        }
    );

    ct::test("Source manager: load failures retain origin and system detail", [] static noexcept {
        const auto temporary = ct::TempDirectory("carven-source-test");
        const auto missing_path = path_to_generic_utf8(temporary.path("missing.cv"));
        auto sources = SourceManager();
        const auto source = sources.append_file(missing_path);
        if (!ct::expect(!source.has_value())) {
            return;
        }
        ct::expect_equal(source.error().origin, std::string_view(missing_path));
        ct::expect(source.error().message.starts_with("cannot read source file: "));
        ct::expect_greater(
            source.error().message.size(),
            std::string_view("cannot read source file: ").size()
        );
    });

    ct::test(
        "Source manager: existing views remain stable across later insertions",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = *sources.append_virtual("first.cv", "stable");
            const auto view = sources.view(first);

            for (auto index = 0; index < 1024; ++index) {
                if (!ct::expect(sources
                                    .append_virtual(
                                        std::format("{}.cv", index),
                                        std::format("text {}", index)
                                    )
                                    .has_value())) {
                    return;
                }
            }

            ct::expect_equal(view.origin, std::string_view("first.cv"));
            ct::expect_equal(view.text, std::string_view("stable"));
            ct::expect_equal(sources.view(first).text, std::string_view("stable"));
        }
    );
});

} // namespace
