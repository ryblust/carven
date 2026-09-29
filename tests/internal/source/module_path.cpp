module carven:test.internal.source.module_path;

import :source.module_path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Module path: factory enforces component structure", [] static noexcept {
        ct::expect(CanonicalModulePath::from_value("app.main").has_value());
        ct::expect(CanonicalModulePath::from_value("App._entry2").has_value());
        ct::expect(!CanonicalModulePath::from_value("").has_value());
        ct::expect(!CanonicalModulePath::from_value("app..main").has_value());
        ct::expect(!CanonicalModulePath::from_value("app.2main").has_value());
        ct::expect(CanonicalModulePath::from_value("app.for").has_value());
        ct::expect(!CanonicalModulePath::from_value("app.模块").has_value());
        ct::expect(!CanonicalModulePath::from_value("crafts").has_value());
        ct::expect(!CanonicalModulePath::from_value("crafts.json").has_value());
        ct::expect(CanonicalModulePath::from_value("crafts.json.parser").has_value());
    });

    ct::test(
        "Module path: domain projection is derived from reserved path structure",
        [] static noexcept {
            const auto local = CanonicalModulePath::from_value("src.app");
            const auto craft_parser = CanonicalModulePath::from_value("crafts.json.parser");
            const auto craft_value = CanonicalModulePath::from_value("crafts.json.model.value");
            if (!ct::expect(local.has_value())) {
                return;
            }
            if (!ct::expect(craft_parser.has_value())) {
                return;
            }
            if (!ct::expect(craft_value.has_value())) {
                return;
            }

            ct::expect(!local->module_domain_prefix().craft_name().has_value());
            const auto domain_prefix = craft_parser->module_domain_prefix();
            const auto craft_name = domain_prefix.craft_name();
            if (!ct::expect(craft_name.has_value())) {
                return;
            }
            ct::expect_equal(*craft_name, std::string_view("json"));
            ct::expect_equal(craft_parser->domain_relative_components().size(), 1u);
            ct::expect_equal(
                craft_parser->domain_relative_components().front(),
                std::string_view("parser")
            );
            ct::expect(same_module_domain(*craft_parser, *craft_value));
            ct::expect(!same_module_domain(*local, *craft_parser));
        }
    );
});

} // namespace
