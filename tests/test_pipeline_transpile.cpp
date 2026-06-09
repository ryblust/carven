#include "test_helpers.h"

import carven.common.source;
import carven.driver.pipeline;
import std;

TEST_CASE("Pipeline: transpile") {
    SUBCASE("transpile simple function") {
        const auto result = transpile("fn main() { }", 23, false);
        CHECK(result.errors.empty());
        CHECK(!result.output.empty());
    }

    SUBCASE("transpile includes preamble when import_std is set") {
        const auto result = transpile("fn main() { }", 23, true);
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile auto-detects import std") {
        const auto result = transpile("import std;\nfn main() { }", 23, false);
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile returns errors on invalid source") {
        const auto result = transpile("@ invalid @", 23, false);
        CHECK(!result.errors.empty());
    }

    SUBCASE("transpile with c++26 standard") {
        const auto result = transpile("fn main() { }", 26, true);
        CHECK(result.errors.empty());
    }

    SUBCASE("write output matches transpile result") {
        static constexpr auto source = std::string_view { "fn main() { }" };
        const auto output_path = std::filesystem::temp_directory_path() / "carven_transpile_output.cpp";
        const auto expected = transpile(source, 23, false);
        const auto driver = Driver{};

        REQUIRE(expected.errors.empty());
        CHECK_EQ(write_transpiled_source(driver, source, "inline.cv", output_path.generic_string()), 0);

        auto output = SourceFile::from_file(output_path.generic_string());
        REQUIRE(output.has_value());
        CHECK_EQ(output->text(), expected.output);

        std::filesystem::remove(output_path);
    }
}
