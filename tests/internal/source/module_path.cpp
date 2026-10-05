module carven:test.internal.source.module_path;

import :source.module_path;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Module path: factory enforces component structure"_test = [] static noexcept {
        expect(CanonicalModulePath::from_value("app.main").has_value());
        expect(CanonicalModulePath::from_value("App._entry2").has_value());
        expect(!CanonicalModulePath::from_value("").has_value());
        expect(!CanonicalModulePath::from_value("app..main").has_value());
        expect(!CanonicalModulePath::from_value("app.2main").has_value());
        expect(CanonicalModulePath::from_value("app.for").has_value());
        expect(!CanonicalModulePath::from_value("app.模块").has_value());
        expect(!CanonicalModulePath::from_value("crafts").has_value());
        expect(!CanonicalModulePath::from_value("crafts.json").has_value());
        expect(CanonicalModulePath::from_value("crafts.json.parser").has_value());
    };

    "Module path: domain projection is derived from reserved path structure"_test =
        [] static noexcept {
            const auto local = CanonicalModulePath::from_value("src.app");
            const auto craft_parser = CanonicalModulePath::from_value("crafts.json.parser");
            const auto craft_value = CanonicalModulePath::from_value("crafts.json.model.value");
            if (!expect(local.has_value())) {
                return;
            }
            if (!expect(craft_parser.has_value())) {
                return;
            }
            if (!expect(craft_value.has_value())) {
                return;
            }

            expect(!local->module_domain_prefix().craft_name().has_value());
            const auto domain_prefix = craft_parser->module_domain_prefix();
            const auto craft_name = domain_prefix.craft_name();
            if (!expect(craft_name.has_value())) {
                return;
            }
            expect_equal(*craft_name, std::string_view("json"));
            expect_equal(craft_parser->domain_relative_components().size(), 1u);
            expect_equal(
                craft_parser->domain_relative_components().front(),
                std::string_view("parser")
            );
            expect(same_module_domain(*craft_parser, *craft_value));
            expect(!same_module_domain(*local, *craft_parser));
        };
});

} // namespace
