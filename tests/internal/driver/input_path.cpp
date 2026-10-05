module carven:test.internal.driver.input_path;

import :driver.input_path;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Input path: normalized relative paths preserve their module hierarchy"_test =
        [] static noexcept {
            const auto direct = derive_input_module_path("src/app/main.cv");
            const auto normalized = derive_input_module_path("src/app/./nested/../main.cv");

            if (!expect(direct.has_value())) {
                return;
            }
            if (!expect(normalized.has_value())) {
                return;
            }
            expect_equal(direct->value(), std::string_view("src.app.main"));
            expect((*normalized == *direct));
        };

    "Input path: identifier-only hierarchy remains exact"_test = [] static noexcept {
        const auto upper = derive_input_module_path("Case.cv");
        const auto lower = derive_input_module_path("case.cv");
        const auto nested = derive_input_module_path("linear_algebra/vector2.cv");
        const auto cv = derive_input_module_path("cv.cv");
        const auto double_underscore = derive_input_module_path("__carven_internal/__value.cv");

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

    "Input path: standard craft inputs follow ordinary path derivation"_test = [] static noexcept {
        const auto direct = derive_input_module_path("crafts/carven/std/utf.cv");
        const auto normalized = derive_input_module_path("crafts/vendor/../json/parser.cv");
        if (!expect(direct.has_value())) {
            return;
        }
        if (!expect(normalized.has_value())) {
            return;
        }
        expect_equal(direct->value(), std::string_view("crafts.carven.std.utf"));
        expect_equal(normalized->value(), std::string_view("crafts.json.parser"));
    };

    "Input path: installed crafts preserve package-relative module identity"_test =
        [] static noexcept {
            const auto directory = TempDirectory("installed-crafts");
            const auto installed = derive_input_module_path(
                path_to_generic_utf8(directory.path("crafts/carven/std/utf.cv"))
            );
            const auto local = derive_input_module_path("crafts/carven/std/utf.cv");
            if (!expect(installed.has_value())) {
                return;
            }
            if (!expect(local.has_value())) {
                return;
            }
            expect((*installed == *local));
            expect_equal(installed->value(), std::string_view("crafts.carven.std.utf"));
            const auto outside =
                derive_input_module_path(path_to_generic_utf8(directory.path("other/std/utf.cv")));
            const auto escaped = derive_input_module_path(
                path_to_generic_utf8(directory.path("crafts/../outside.cv"))
            );
            expect(!outside.has_value());
            expect(!escaped.has_value());
        };

    "Input path: invalid module names identify the first offending component"_test =
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

            each(
                cases,
                [](const auto& entry) static noexcept { return entry.first; },
                [&](const auto& entry) noexcept {
                    const auto& [input, error] = entry;
                    const auto result = derive_input_module_path(input);
                    if (!(expect(!result.has_value()))) {
                        return;
                    }
                    expect((result.error() == error));
                }
            );
        };

    "Input path: invalid paths are rejected before source acquisition"_test = [] static noexcept {
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

        each(
            cases,
            [](const auto& entry) static noexcept { return entry.first; },
            [&](const auto& entry) noexcept {
                const auto& [input, error] = entry;
                const auto result = derive_input_module_path(input);
                if (!(expect(!result.has_value()))) {
                    return;
                }
                expect((result.error() == error));
            }
        );

        const auto with_nul = std::string_view("nul\0name.cv", 11);
        const auto nul_result = derive_input_module_path(with_nul);
        if (!expect(!nul_result.has_value())) {
            return;
        }
        expect_equal(nul_result.error(), std::string_view("input path cannot contain NUL"));

        auto invalid_utf8 = std::string("invalid-");
        invalid_utf8.push_back(static_cast<char>(0xff));
        invalid_utf8 += ".cv";
        const auto utf8_result = derive_input_module_path(invalid_utf8);
        if (!expect(!utf8_result.has_value())) {
            return;
        }
        expect_equal(utf8_result.error(), std::string_view("input path is not valid UTF-8"));
    };

#if defined(_WIN32)
    "Input path: drive-relative paths cannot escape the working directory"_test =
        [] static noexcept {
            const auto result = derive_input_module_path("C:escape.cv");

            expect(!(derive_input_module_path("C:prefix/crafts/carven/std/utf.cv").has_value()));
            if (!expect(!result.has_value())) {
                return;
            }
            expect_equal(
                result.error(),
                std::string_view(
                    "input 'C:escape.cv' must be relative or belong to a crafts directory"
                )
            );
        };
#endif

    "Input path: keyword components preserve exact module identity"_test = [] static noexcept {
        const auto result = derive_input_module_path("crafts/import/using/match.cv");
        if (!expect(result.has_value())) {
            return;
        }
        expect_equal(result->value(), std::string_view("crafts.import.using.match"));
    };
});

} // namespace
