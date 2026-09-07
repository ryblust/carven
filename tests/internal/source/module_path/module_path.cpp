module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.module_path;

import :source.module_path;
import std;

TEST_CASE("Module path: factory enforces component structure") {
    CHECK(CanonicalModulePath::from_value("app.main").has_value());
    CHECK(CanonicalModulePath::from_value("App._entry2").has_value());
    CHECK(!CanonicalModulePath::from_value("").has_value());
    CHECK(!CanonicalModulePath::from_value("app..main").has_value());
    CHECK(!CanonicalModulePath::from_value("app.2main").has_value());
    CHECK(CanonicalModulePath::from_value("app.for").has_value());
    CHECK(!CanonicalModulePath::from_value("app.模块").has_value());
    CHECK(!CanonicalModulePath::from_value("crafts").has_value());
    CHECK(!CanonicalModulePath::from_value("crafts.json").has_value());
    CHECK(CanonicalModulePath::from_value("crafts.json.parser").has_value());
}

TEST_CASE("Module path: domain projection is derived from reserved path structure") {
    const auto local = CanonicalModulePath::from_value("src.app");
    const auto craft_parser = CanonicalModulePath::from_value("crafts.json.parser");
    const auto craft_value = CanonicalModulePath::from_value("crafts.json.model.value");
    REQUIRE(local.has_value());
    REQUIRE(craft_parser.has_value());
    REQUIRE(craft_value.has_value());

    CHECK(!local->module_domain_prefix().craft_name().has_value());
    const auto craft_name = craft_parser->module_domain_prefix().craft_name();
    REQUIRE(craft_name.has_value());
    CHECK_EQ(*craft_name, "json");
    CHECK_EQ(craft_parser->domain_relative_components().size(), 1u);
    CHECK_EQ(craft_parser->domain_relative_components().front(), "parser");
    CHECK(same_module_domain(*craft_parser, *craft_value));
    CHECK(!same_module_domain(*local, *craft_parser));
}
