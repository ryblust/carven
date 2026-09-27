module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.graver.corpus;

import :diagnostics.report;
import :graver.format;
import :source.manager;
import :support.path;
import std;

TEST_CASE("Graver corpus: repository programs format into valid stable source") {
    auto paths = std::vector<std::filesystem::path>();
    for (const auto* root : {"tests/language", "tests/interop", "examples", "crafts"}) {
        auto error = std::error_code();
        auto iterator = std::filesystem::recursive_directory_iterator(root, error);
        REQUIRE_FALSE(error);
        const auto end = std::filesystem::recursive_directory_iterator();
        while (iterator != end) {
            if (iterator->path().extension() == ".cv" && iterator->is_regular_file(error)) {
                paths.push_back(iterator->path());
            }
            REQUIRE_FALSE(error);
            iterator.increment(error);
            REQUIRE_FALSE(error);
        }
    }
    std::ranges::sort(paths);
    auto formatted_count = 0uz;
    for (const auto& path : paths) {
        const auto name = path_to_generic_utf8(path);
        INFO(name);
        auto sources = SourceManager();
        const auto id = sources.append_file(name);
        REQUIRE(id.has_value());
        const auto result = graver::format(sources, *id);
        if (!result) {
            INFO(render_diagnostics(result.error(), sources));
            CHECK(result.has_value());
            continue;
        }
        const auto formatted_id = sources.append_virtual("formatted.cv", *result);
        REQUIRE(formatted_id.has_value());
        const auto repeated = graver::format(sources, *formatted_id);
        REQUIRE(repeated.has_value());
        ++formatted_count;
        const auto mismatch = std::ranges::mismatch(*result, *repeated);
        const auto offset = static_cast<std::size_t>(mismatch.in1 - result->begin());
        INFO(
            "first difference at ",
            offset,
            ": ",
            result->substr(offset, 150),
            " -> ",
            repeated->substr(offset, 150)
        );
        CHECK(*result == *repeated);
    }
    std::println("Graver corpus: {} formatted .cv files", formatted_count);
    CHECK(formatted_count > 0uz);
}
