#include "test_helpers.h"

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.handler;
import std;

TEST_CASE("Pipeline: transpile") {
    SUBCASE("transpile simple function") {
        const auto driver = Driver{};
        const auto result = transpile(driver, "fn main() { }");
        CHECK(result.errors.empty());
        CHECK(!result.output.empty());
    }

    SUBCASE("transpile includes preamble when import_std is set") {
        auto driver = Driver{};
        driver.import_std = true;
        const auto result = transpile(driver, "fn main() { }");
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile auto-detects import std") {
        const auto driver = Driver{};
        const auto result = transpile(driver, "import std;\nfn main() { }");
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile returns errors on invalid source") {
        const auto driver = Driver{};
        const auto result = transpile(driver, "@ invalid @");
        CHECK(!result.errors.empty());
    }

    SUBCASE("transpile with c++26 standard") {
        auto driver = Driver{};
        driver.language_standard = 26;
        driver.import_std = true;
        const auto result = transpile(driver, "fn main() { }");
        CHECK(result.errors.empty());
    }

    SUBCASE("transpile helloworld") {
        const auto driver = Driver{};
        auto source = SourceFile::from_file("tests/fixtures/helloworld.cv");
        REQUIRE(source.has_value());
        const auto text = source->text();
        const auto result = transpile(driver, text);
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
