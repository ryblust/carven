module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.graver.batch;

import :graver.batch;
import :source.manager;
import :source.text;
import :support.file;
import :support.path;
import std;

namespace {

class Fixture final {
public:
    Fixture() noexcept;
    ~Fixture() noexcept;
    auto path(std::string_view name) const noexcept -> std::filesystem::path;

private:
    std::filesystem::path directory;
};

Fixture::Fixture() noexcept {
    auto error = std::error_code();
    const auto base = std::filesystem::temp_directory_path(error);
    REQUIRE_FALSE(error);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (auto attempt = 0; attempt < 128; ++attempt) {
        const auto candidate = base / std::format("graver-batch-{}-{}", stamp, attempt);
        if (std::filesystem::create_directory(candidate, error)) {
            directory = candidate;
            return;
        }
    }
    REQUIRE(false);
}

Fixture::~Fixture() noexcept {
    auto ignored = std::error_code();
    std::filesystem::remove_all(directory, ignored);
}

auto Fixture::path(std::string_view name) const noexcept -> std::filesystem::path {
    return directory / name;
}

} // namespace

TEST_CASE("Graver batch: ordered results preserve inputs without writing") {
    const auto fixture = Fixture();
    const auto first_path = fixture.path("z.cv");
    const auto second_path = fixture.path("a.cv");
    constexpr auto original = "fn f(){let x=1;call(x);}";
    constexpr auto formatted = "fn f() {\n    let x = 1;\n    call(x);\n}\n";
    REQUIRE(write_file(first_path, original).has_value());
    REQUIRE(write_file(second_path, formatted).has_value());
    auto sources = SourceManager();
    const auto first = sources.append_file(path_to_generic_utf8(first_path));
    const auto second = sources.append_file(path_to_generic_utf8(second_path));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto inputs = std::to_array<graver::BatchInput>({
        {.path = first_path, .source_id = *first},
        {.path = second_path, .source_id = *second},
    });
    const auto batch = graver::format_batch(sources, inputs);
    REQUIRE(batch.has_value());
    const auto files = batch->files();
    REQUIRE(files.size() == 2uz);
    CHECK(files[0].path == first_path);
    CHECK(files[0].original == original);
    CHECK(files[0].formatted == formatted);
    CHECK(files[0].changed());
    CHECK(files[1].path == second_path);
    CHECK_FALSE(files[1].changed());
    CHECK(graver::check_report(*batch, first_path.parent_path()) == "z.cv\n");
    const auto before = read_file(first_path, 100);
    REQUIRE(before.has_value());
    CHECK(*before == original);
    auto error = std::error_code();
    const auto timestamp = std::filesystem::last_write_time(second_path, error);
    REQUIRE_FALSE(error);
    REQUIRE(graver::write_batch(*batch).has_value());
    const auto after = read_file(first_path, 100);
    REQUIRE(after.has_value());
    CHECK(*after == formatted);
    CHECK(std::filesystem::last_write_time(second_path, error) == timestamp);
    CHECK_FALSE(error);
    CHECK(sources.view(*first).text == original);
}

TEST_CASE("Graver batch: all failures are collected in input order without publishing outputs") {
    auto sources = SourceManager();
    const auto first = sources.append_virtual("first.cv", "fn bad(");
    const auto valid = sources.append_virtual("valid.cv", "fn good(){}");
    const auto last = sources.append_virtual("last.cv", "@");
    REQUIRE(first.has_value());
    REQUIRE(valid.has_value());
    REQUIRE(last.has_value());
    const auto inputs = std::to_array<graver::BatchInput>({
        {.path = "last.cv", .source_id = *last},
        {.path = "valid.cv", .source_id = *valid},
        {.path = "first.cv", .source_id = *first},
    });
    const auto batch = graver::format_batch(sources, inputs);
    REQUIRE_FALSE(batch.has_value());
    auto locations = std::vector<SourceID>();
    for (const auto& diagnostic : batch.error()) {
        REQUIRE(diagnostic.attachment.primary.has_value());
        const auto id = diagnostic.attachment.primary->span.source_id;
        if (locations.empty() || locations.back() != id) {
            locations.push_back(id);
        }
    }
    const auto expected = std::vector<SourceID> {*last, *first};
    CHECK(locations == expected);
    CHECK(sources.view(*valid).text == "fn good(){}");
}

TEST_CASE("Graver batch: empty batches and stdin have explicit report behavior") {
    auto error = std::error_code();
    const auto directory = std::filesystem::current_path(error);
    REQUIRE_FALSE(error);
    auto sources = SourceManager();
    {
        const auto empty = graver::format_batch(sources, {});
        REQUIRE(empty.has_value());
        CHECK(empty->files().empty());
        CHECK(graver::check_report(*empty, directory).empty());
        CHECK(graver::write_batch(*empty).has_value());
    }
    const auto id = sources.append_virtual("stdin", "fn f(){}");
    REQUIRE(id.has_value());
    const auto inputs = std::to_array<graver::BatchInput>({{.path = {}, .source_id = *id}});
    const auto batch = graver::format_batch(sources, inputs);
    REQUIRE(batch.has_value());
    CHECK(graver::check_report(*batch, directory) == "stdin\n");
    CHECK_FALSE(graver::write_batch(*batch).has_value());
}

TEST_CASE("Graver batch: every destination is checked before the first replacement") {
    const auto fixture = Fixture();
    const auto path = fixture.path("first.cv");
    constexpr auto original = "fn first(){}";
    REQUIRE(write_file(path, original).has_value());
    auto sources = SourceManager();
    const auto first = sources.append_file(path_to_generic_utf8(path));
    const auto second = sources.append_virtual("stdin", "fn second(){}");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto inputs = std::to_array<graver::BatchInput>({
        {.path = path, .source_id = *first},
        {.path = {}, .source_id = *second},
    });
    const auto batch = graver::format_batch(sources, inputs);
    REQUIRE(batch.has_value());
    const auto written = graver::write_batch(*batch);
    REQUIRE_FALSE(written.has_value());
    CHECK(written.error().find("stdin") != std::string::npos);
    const auto contents = read_file(path, 100);
    REQUIRE(contents.has_value());
    CHECK(*contents == original);
}
