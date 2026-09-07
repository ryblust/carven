module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.manager;

import :source.location;
import :source.manager;
import :source.text;
import :support.file;
import :support.path;
import std;

namespace {

class TemporaryFile final {
public:
    explicit TemporaryFile(std::string_view content) noexcept {
        auto error = std::error_code();
        const auto root = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }

        static auto sequence = std::atomic<std::uint64_t>();
        file_path = root
            / std::format(
                        "carven-source-test-{}-{}.cv",
                        std::chrono::steady_clock::now().time_since_epoch().count(),
                        sequence.fetch_add(1, std::memory_order_relaxed)
            );
        encoded_path = file_path.generic_string();

        auto output = std::ofstream(file_path, std::ios::binary | std::ios::trunc);
        ready_state = output.is_open() && output.write(content.data(), content.size()).good();
    }

    TemporaryFile(const TemporaryFile&) = delete;
    auto operator=(const TemporaryFile&) -> TemporaryFile& = delete;

    ~TemporaryFile() noexcept {
        auto error = std::error_code();
        if (!file_path.empty()) {
            std::filesystem::remove(file_path, error);
        }
    }

    auto ready() const noexcept -> bool { return ready_state; }

    auto path() const noexcept -> std::string_view { return encoded_path; }

private:
    std::filesystem::path file_path;
    std::string encoded_path;
    bool ready_state = false;
};

} // namespace

TEST_CASE("Source manager: virtual sources own text and provide stable source-aware locations") {
    auto sources = SourceManager();
    const auto first = *sources.append_virtual("first.cv", "alpha\nbeta");
    const auto second = *sources.append_virtual("second.cv", "gamma");

    CHECK_EQ(first.index(), 0u);
    CHECK_EQ(second.index(), 1u);
    CHECK_EQ(sources.view(first).origin, "first.cv");
    CHECK_EQ(sources.slice(locate(first, Span::from_bounds(6, 10))), "beta");
    const auto first_location = sources.location(locate(first, Span::from_bounds(7, 7)));
    CHECK_EQ(first_location.line, 2u);
    CHECK_EQ(first_location.column, 2u);
}

TEST_CASE("Source manager: file sources own an immutable snapshot through the same ID model") {
    const auto temporary = TemporaryFile("first\nsecond");
    REQUIRE(temporary.ready());

    auto sources = SourceManager();
    const auto source = sources.append_file(temporary.path());
    REQUIRE(source.has_value());
    CHECK_EQ(source->index(), 0u);
    CHECK_EQ(sources.view(*source).origin, temporary.path());
    CHECK_EQ(sources.view(*source).text, "first\nsecond");
    CHECK_EQ(sources.slice(locate(*source, Span::from_bounds(6, 12))), "second");
    const auto second_location = sources.location(locate(*source, Span::from_bounds(8, 8)));
    CHECK_EQ(second_location.line, 2u);
    CHECK_EQ(second_location.column, 3u);

    REQUIRE(write_file(path_from_utf8(temporary.path()), "changed").has_value());
    CHECK_EQ(sources.view(*source).text, "first\nsecond");
}

TEST_CASE("Source manager: load failures retain origin and system detail") {
    auto sources = SourceManager();
    const auto source = sources.append_file("carven-source-test-missing.cv");
    REQUIRE(!source.has_value());
    CHECK_EQ(source.error().origin, "carven-source-test-missing.cv");
    CHECK(source.error().message.starts_with("cannot read source file: "));
    CHECK_GT(source.error().message.size(), std::string_view("cannot read source file: ").size());
}

TEST_CASE("Source manager: existing views remain stable across later insertions") {
    auto sources = SourceManager();
    const auto first = *sources.append_virtual("first.cv", "stable");
    const auto view = sources.view(first);

    for (auto index = 0; index < 1024; ++index) {
        REQUIRE(sources.append_virtual(std::format("{}.cv", index), std::format("text {}", index)));
    }

    CHECK_EQ(view.origin, "first.cv");
    CHECK_EQ(view.text, "stable");
    CHECK_EQ(sources.view(first).text, "stable");
}
