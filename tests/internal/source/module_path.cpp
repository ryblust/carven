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

    "Module path: relative source paths preserve their module hierarchy"_test = [] static noexcept {
        const auto direct = CanonicalModulePath::from_source_path("src/app/main.cv");
        const auto normalized =
            CanonicalModulePath::from_source_path("src/app/./nested/../main.cv");

        if (!expect(direct.has_value())) {
            return;
        }
        if (!expect(normalized.has_value())) {
            return;
        }
        expect_equal(direct->value(), std::string_view("src.app.main"));
        expect((*normalized == *direct));
    };

    "Module path: source hierarchy preserves identifier spelling"_test = [] static noexcept {
        const auto upper = CanonicalModulePath::from_source_path("Case.cv");
        const auto lower = CanonicalModulePath::from_source_path("case.cv");
        const auto nested = CanonicalModulePath::from_source_path("linear_algebra/vector2.cv");
        const auto cv = CanonicalModulePath::from_source_path("cv.cv");
        const auto double_underscore =
            CanonicalModulePath::from_source_path("__carven_internal/__value.cv");

        if (!expect(upper.has_value())) {
            return;
        }
        if (!expect(lower.has_value())) {
            return;
        }
        if (!expect(nested.has_value())) {
            return;
        }
        if (!expect(cv.has_value())) {
            return;
        }
        if (!expect(double_underscore.has_value())) {
            return;
        }
        expect_equal(upper->value(), std::string_view("Case"));
        expect_equal(lower->value(), std::string_view("case"));
        expect(*upper != *lower);
        expect_equal(nested->value(), std::string_view("linear_algebra.vector2"));
        expect_equal(cv->value(), std::string_view("cv"));
        expect_equal(double_underscore->value(), std::string_view("__carven_internal.__value"));
    };

    "Module path: Craft source paths follow ordinary derivation"_test = [] static noexcept {
        const auto direct = CanonicalModulePath::from_source_path("crafts/carven/std/utf.cv");
        const auto normalized =
            CanonicalModulePath::from_source_path("crafts/vendor/../json/parser.cv");
        if (!expect(direct.has_value())) {
            return;
        }
        if (!expect(normalized.has_value())) {
            return;
        }
        expect_equal(direct->value(), std::string_view("crafts.carven.std.utf"));
        expect_equal(normalized->value(), std::string_view("crafts.json.parser"));
    };

    "Module path: source paths identify invalid module components"_test = [] static noexcept {
        constexpr auto cases = std::array {
            std::pair {
                std::string_view("bad-dir/main.cv"),
                std::string_view(
                    "input 'bad-dir/main.cv' has invalid module directory component 'bad-dir'"
                ),
            },
            std::pair {
                std::string_view("math/bad-name.cv"),
                std::string_view(
                    "input 'math/bad-name.cv' has invalid module file stem 'bad-name'"
                ),
            },
            std::pair {
                std::string_view("42/value.cv"),
                std::string_view("input '42/value.cv' has invalid module directory component '42'"),
            },
            std::pair {
                std::string_view("math/42.cv"),
                std::string_view("input 'math/42.cv' has invalid module file stem '42'"),
            },
            std::pair {
                std::string_view("\xe6\x95\xb0\xe5\xad\xa6/vector.cv"),
                std::string_view(
                    "input '\xe6\x95\xb0\xe5\xad\xa6/vector.cv' has invalid module directory "
                    "component '\xe6\x95\xb0\xe5\xad\xa6'"
                ),
            },
        };

        each(
            cases,
            [](const auto& entry) static noexcept { return entry.first; },
            [&](const auto& entry) noexcept {
                const auto& [input, error] = entry;
                const auto result = CanonicalModulePath::from_source_path(input);
                if (!(expect(!result.has_value()))) {
                    return;
                }
                expect((result.error() == error));
            }
        );
    };

    "Module path: invalid source paths are rejected"_test = [] static noexcept {
        constexpr auto cases = std::array {
            std::pair {
                std::string_view(""),
                std::string_view("input path cannot be empty"),
            },
            std::pair {
                std::string_view("/absolute.cv"),
                std::string_view("input '/absolute.cv' must be relative"),
            },
            std::pair {
                std::string_view("../outside.cv"),
                std::string_view("input '../outside.cv' escapes the source root"),
            },
            std::pair {
                std::string_view("wrong.txt"),
                std::string_view("input 'wrong.txt' does not have a .cv extension"),
            },
            std::pair {
                std::string_view("..cv"),
                std::string_view("cannot derive a module path from '..cv'"),
            },
            std::pair {
                std::string_view("a/..cv"),
                std::string_view("input 'a/..cv' derives an empty module path component"),
            },
            std::pair {
                std::string_view("bad\\name.cv"),
                std::string_view("input path must use '/' as the path separator"),
            },
            std::pair {
                std::string_view("quote\"name.cv"),
                std::string_view("input path cannot contain '\"'"),
            },
            std::pair {
                std::string_view("line\nname.cv"),
                std::string_view("input path cannot contain a line break"),
            },
            std::pair {
                std::string_view("carriage\rname.cv"),
                std::string_view("input path cannot contain a line break"),
            },
        };

        each(
            cases,
            [](const auto& entry) static noexcept { return entry.first; },
            [&](const auto& entry) noexcept {
                const auto& [input, error] = entry;
                const auto result = CanonicalModulePath::from_source_path(input);
                if (!(expect(!result.has_value()))) {
                    return;
                }
                expect((result.error() == error));
            }
        );

        constexpr auto with_nul = std::string_view("nul\0name.cv", 11);
        const auto nul_result = CanonicalModulePath::from_source_path(with_nul);
        if (!expect(!nul_result.has_value())) {
            return;
        }
        expect_equal(nul_result.error(), std::string_view("input path cannot contain NUL"));

        auto invalid_utf8 = std::string("invalid-");
        invalid_utf8.push_back(static_cast<char>(0xff));
        invalid_utf8 += ".cv";
        const auto utf8_result = CanonicalModulePath::from_source_path(invalid_utf8);
        if (!expect(!utf8_result.has_value())) {
            return;
        }
        expect_equal(utf8_result.error(), std::string_view("input path is not valid UTF-8"));
    };

#if defined(_WIN32)
    "Module path: drive-relative source paths are rejected"_test = [] static noexcept {
        const auto result = CanonicalModulePath::from_source_path("C:escape.cv");

        expect(!(
            CanonicalModulePath::from_source_path("C:prefix/crafts/carven/std/utf.cv").has_value()
        ));
        if (!expect(!result.has_value())) {
            return;
        }
        expect_equal(result.error(), std::string_view("input 'C:escape.cv' must be relative"));
    };
#endif

    "Module path: source paths preserve keyword components"_test = [] static noexcept {
        const auto result = CanonicalModulePath::from_source_path("crafts/import/using/match.cv");
        if (!expect(result.has_value())) {
            return;
        }
        expect_equal(result->value(), std::string_view("crafts.import.using.match"));
    };
});

} // namespace
