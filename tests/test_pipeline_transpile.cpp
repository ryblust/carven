#include "test_helpers.h"

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.handler;
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

    SUBCASE("transpile helloworld") {
        auto source = SourceFile::from_file("tests/fixtures/helloworld.cv");
        REQUIRE(source.has_value());
        const auto text = source->text();
        const auto result = transpile(text, 23, false);
        CHECK(result.errors.empty());
        CHECK(result.output.contains("auto main() noexcept -> int"));
        CHECK(result.output.contains("std::println(\"Hello World\")"));
    }
}

TEST_CASE("Handler: run") {
    SUBCASE("nonexistent file returns 1") {
        auto driver = Driver{};
        driver.input_files.push_back("nonexistent_file_for_test_123.cv");
        const auto result = run(driver);
        CHECK_EQ(result, 1);
    }

    SUBCASE("helloworld compiles and runs") {
        auto driver = Driver{};
        driver.input_files.push_back("tests/fixtures/helloworld.cv");
        const auto result = run(driver);
        CHECK_EQ(result, 0);
    }
}
