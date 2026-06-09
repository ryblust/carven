#include "test_helpers.h"

import carven.common.source;
import carven.driver.pipeline;
import std;

static auto golden_sources() noexcept -> std::vector<std::filesystem::path> {
    auto paths = std::vector<std::filesystem::path>();
    auto error = std::error_code();
    auto iter = std::filesystem::directory_iterator("tests/golden", error);
    const auto end = std::filesystem::directory_iterator();

    while (!error && iter != end) {
        const auto path = iter->path();
        if (path.extension() == ".cv") {
            paths.push_back(path);
        }
        iter.increment(error);
    }

    std::ranges::sort(paths, {}, [](const std::filesystem::path& path) {
        return path.generic_string();
    });
    return paths;
}

TEST_CASE("Golden: transpile full-file fixtures") {
    const auto sources = golden_sources();
    REQUIRE(!sources.empty());

    for (const auto& source_path : sources) {
        const auto expected_path = std::filesystem::path(source_path).replace_extension(".cpp");
        const auto name = source_path.generic_string();
        CAPTURE(name);

        auto source = SourceFile::from_file(source_path.generic_string());
        auto expected = SourceFile::from_file(expected_path.generic_string());

        REQUIRE(source.has_value());
        REQUIRE(expected.has_value());

        const auto result = transpile(source->text(), 23, false);
        REQUIRE(result.errors.empty());
        CHECK_EQ(result.output, expected->text());
    }
}
