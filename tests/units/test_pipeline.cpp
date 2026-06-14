#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include "doctest.h"

import carven.driver.pipeline;
import std;

TEST_CASE("Pipeline: transpile") {
    SUBCASE("transpile simple function") {
        const auto result = transpile("fn main() { }", {
            .language_standard = 23,
            .import_std = false,
        });
        CHECK(result.errors.empty());
        CHECK(!result.output.empty());
    }

    SUBCASE("transpile includes preamble when import_std is set") {
        const auto result = transpile("fn main() { }", {
            .language_standard = 23,
            .import_std = true,
        });
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile auto-detects import std") {
        const auto result = transpile("import std;\nfn main() { }", {
            .language_standard = 23,
            .import_std = false,
        });
        CHECK(result.errors.empty());
        CHECK(result.output.contains("#include"));
    }

    SUBCASE("transpile returns errors on invalid source") {
        const auto result = transpile("@ invalid @", {
            .language_standard = 23,
            .import_std = false,
        });
        CHECK(!result.errors.empty());
    }

    SUBCASE("transpile with c++26 standard") {
        const auto result = transpile("fn main() { }", {
            .language_standard = 26,
            .import_std = true,
        });
        CHECK(result.errors.empty());
    }

}
