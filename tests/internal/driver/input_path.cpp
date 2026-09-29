module carven:test.internal.driver.input_path;

import :driver.input_path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Input path: normalized relative paths preserve their module hierarchy",
        [] static noexcept {
            const auto direct = derive_input_module_path("src/app/main.cv");
            const auto normalized = derive_input_module_path("src/app/./nested/../main.cv");

            if (!ct::expect(direct.has_value())) {
                return;
            }
            if (!ct::expect(normalized.has_value())) {
                return;
            }
            ct::expect_equal(direct->value(), std::string_view("src.app.main"));
            ct::expect((*normalized == *direct));
        }
    );

    ct::test("Input path: identifier-only hierarchy remains exact", [] static noexcept {
        const auto upper = derive_input_module_path("Case.cv");
        const auto lower = derive_input_module_path("case.cv");
        const auto nested = derive_input_module_path("linear_algebra/vector2.cv");
        const auto cv = derive_input_module_path("cv.cv");
        const auto double_underscore = derive_input_module_path("__carven_internal/__value.cv");

        if (!ct::expect(upper.has_value())) {
            return;
        }
        if (!ct::expect(lower.has_value())) {
            return;
        }
        if (!ct::expect(nested.has_value())) {
            return;
        }
        if (!ct::expect(cv.has_value())) {
            return;
        }
        if (!ct::expect(double_underscore.has_value())) {
            return;
        }
        ct::expect_equal(upper->value(), std::string_view("Case"));
        ct::expect_equal(lower->value(), std::string_view("case"));
        ct::expect(*upper != *lower);
        ct::expect_equal(nested->value(), std::string_view("linear_algebra.vector2"));
        ct::expect_equal(cv->value(), std::string_view("cv"));
        ct::expect_equal(double_underscore->value(), std::string_view("__carven_internal.__value"));
    });

    ct::test(
        "Input path: standard craft inputs follow ordinary path derivation",
        [] static noexcept {
            const auto direct = derive_input_module_path("crafts/carven/std/utf.cv");
            const auto normalized = derive_input_module_path("crafts/vendor/../json/parser.cv");
            if (!ct::expect(direct.has_value())) {
                return;
            }
            if (!ct::expect(normalized.has_value())) {
                return;
            }
            ct::expect_equal(direct->value(), std::string_view("crafts.carven.std.utf"));
            ct::expect_equal(normalized->value(), std::string_view("crafts.json.parser"));
        }
    );

    ct::test(
        "Input path: installed crafts preserve package-relative module identity",
        [] static noexcept {
            const auto installed =
                derive_input_module_path("/opt/toolchain/crafts/carven/std/utf.cv");
            const auto local = derive_input_module_path("crafts/carven/std/utf.cv");
            if (!ct::expect(installed.has_value())) {
                return;
            }
            if (!ct::expect(local.has_value())) {
                return;
            }
            ct::expect((*installed == *local));
            ct::expect_equal(installed->value(), std::string_view("crafts.carven.std.utf"));
            ct::expect(!(derive_input_module_path("/opt/toolchain/other/std/utf.cv").has_value()));
            ct::expect(
                !(derive_input_module_path("/opt/toolchain/crafts/../outside.cv").has_value())
            );
        }
    );

    ct::test(
        "Input path: invalid module names identify the first offending component",
        [] static noexcept {
            const auto cases = std::array {
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
                    std::string_view(
                        "input '42/value.cv' has invalid module directory component '42'"
                    ),
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

            ct::each(
                cases,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    const auto& [input, error] = entry;
                    const auto result = derive_input_module_path(input);
                    if (!(ct::expect(!result.has_value()))) {
                        return;
                    }
                    ct::expect((result.error() == error));
                }
            );
        }
    );

    ct::test(
        "Input path: invalid paths are rejected before source acquisition",
        [] static noexcept {
            const auto cases = std::array {
                std::pair {
                    std::string_view(""),
                    std::string_view("input path cannot be empty"),
                },
                std::pair {
                    std::string_view("/absolute.cv"),
                    std::string_view(
                        "input '/absolute.cv' must be relative or belong to a crafts directory"
                    ),
                },
                std::pair {
                    std::string_view("../outside.cv"),
                    std::string_view("input '../outside.cv' escapes the working directory"),
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

            ct::each(
                cases,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    const auto& [input, error] = entry;
                    const auto result = derive_input_module_path(input);
                    if (!(ct::expect(!result.has_value()))) {
                        return;
                    }
                    ct::expect((result.error() == error));
                }
            );

            const auto with_nul = std::string_view("nul\0name.cv", 11);
            const auto nul_result = derive_input_module_path(with_nul);
            if (!ct::expect(!nul_result.has_value())) {
                return;
            }
            ct::expect_equal(nul_result.error(), std::string_view("input path cannot contain NUL"));

            auto invalid_utf8 = std::string("invalid-");
            invalid_utf8.push_back(static_cast<char>(0xff));
            invalid_utf8 += ".cv";
            const auto utf8_result = derive_input_module_path(invalid_utf8);
            if (!ct::expect(!utf8_result.has_value())) {
                return;
            }
            ct::expect_equal(
                utf8_result.error(),
                std::string_view("input path is not valid UTF-8")
            );
        }
    );

#if defined(_WIN32)
    ct::test(
        "Input path: drive-relative paths cannot escape the working directory",
        [] static noexcept {
            const auto result = derive_input_module_path("C:escape.cv");

            ct::expect(
                !(derive_input_module_path("C:prefix/crafts/carven/std/utf.cv").has_value())
            );
            if (!ct::expect(!result.has_value())) {
                return;
            }
            ct::expect_equal(
                result.error(),
                std::string_view(
                    "input 'C:escape.cv' must be relative or belong to a crafts directory"
                )
            );
        }
    );
#endif

    ct::test("Input path: keyword components preserve exact module identity", [] static noexcept {
        const auto result = derive_input_module_path("crafts/import/using/match.cv");
        if (!ct::expect(result.has_value())) {
            return;
        }
        ct::expect_equal(result->value(), std::string_view("crafts.import.using.match"));
    });
});

} // namespace
