module carven:test.internal.artifacts.materialize;

import :artifacts.materialize;
import :artifacts;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

auto generated(const std::string& logical_path, const std::string& content) noexcept
    -> GeneratedArtifact {
    return {
        .logical_path = logical_path,
        .role = GeneratedArtifactRole::ModuleImplementation,
        .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
        .content = content,
    };
}

auto contents(const std::filesystem::path& path) noexcept -> std::string {
    auto input = std::ifstream(path, std::ios::binary);
    require(input.is_open()).note("read artifact: ", path_to_generic_utf8(path));
    auto result =
        std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    require(!input.bad()).note("read artifact: ", path_to_generic_utf8(path));
    return result;
}

const TestSuite suite([] static noexcept {
    "Artifacts: GeneratedArtifactSet establishes one canonical order"_test = [] static noexcept {
        const auto artifacts = GeneratedArtifactSet({
            generated("nested/api.hpp", "interface"),
            generated("api.cpp", "implementation"),
            generated("api.hpp", "interface"),
        });

        if (!expect_equal(artifacts.entries().size(), 3u)) {
            return;
        }
        expect_equal(artifacts.entries()[0].logical_path, std::string_view("api.cpp"));
        expect_equal(artifacts.entries()[1].logical_path, std::string_view("api.hpp"));
        expect_equal(artifacts.entries()[2].logical_path, std::string_view("nested/api.hpp"));
    };

    "Artifacts: logical paths are normalized relative paths"_test = [] static noexcept {
        const auto valid = std::to_array<std::string_view>({
            "api.hpp",
            "carven/api/example.hpp",
            ".carven-artifacts",
        });
        each(valid, std::identity {}, [](std::string_view path) static noexcept {
            expect(validate_artifact_logical_path(path).has_value());
        });
        const auto invalid = std::to_array<std::string_view>({
            "",
            "/api.hpp",
            "api.hpp/",
            "carven//api.hpp",
            "carven/./api.hpp",
            "carven/../api.hpp",
            "carven\\api.hpp",
            "C:/api.hpp",
        });
        each(
            invalid,
            [](std::string_view path) static noexcept {
                return path.empty() ? std::string_view("<empty>") : path;
            },
            [](std::string_view path) static noexcept {
                expect(!validate_artifact_logical_path(path).has_value());
            }
        );
    };

    "Artifacts: duplicate and prefix-colliding logical paths violate the set invariant"_test =
        [] static noexcept {
            expect(expect_termination("artifact-set-duplicate-logical-path", []() static noexcept {
                const auto artifacts = GeneratedArtifactSet({
                    generated("module.cpp", "first"),
                    generated("module.cpp", "second"),
                });
                static_cast<void>(artifacts);
            }));

            expect(expect_termination("artifact-set-prefix-logical-path", []() static noexcept {
                const auto artifacts = GeneratedArtifactSet({
                    generated("carven/generated", "file"),
                    generated("carven/generated/module.hpp", "descendant"),
                });
                static_cast<void>(artifacts);
            }));
        };

    "Artifacts: ordinary writes overwrite generated files and preserve stale files"_test =
        [] static noexcept {
            const auto temporary = TempDirectory("carven-artifacts-test");
            const auto initial = GeneratedArtifactSet({
                generated("nested/api.cpp", "implementation\n"),
                generated("nested/api.hpp", "interface\n"),
                generated(".carven-artifacts", "ordinary file\n"),
            });
            if (!expect(write_artifacts(temporary.path(), initial).has_value())) {
                return;
            }

            const auto next = GeneratedArtifactSet({generated("nested/api.cpp", "replacement\n")});
            if (!expect(write_artifacts(temporary.path(), next).has_value())) {
                return;
            }

            expect_equal(
                contents(temporary.path() / "nested/api.cpp"),
                std::string_view("replacement\n")
            );
            expect_equal(
                contents(temporary.path() / "nested/api.hpp"),
                std::string_view("interface\n")
            );
            expect_equal(
                contents(temporary.path() / ".carven-artifacts"),
                std::string_view("ordinary file\n")
            );
        };

    "Artifacts: writes fail at the first filesystem error without rollback"_test =
        [] static noexcept {
            const auto temporary = TempDirectory("carven-artifacts-test");
            auto output = std::ofstream(temporary.path("z"), std::ios::binary | std::ios::trunc);
            if (!expect(output.is_open())) {
                return;
            }
            output.write("not a directory\n", 16);
            output.close();
            if (!expect(output.good())) {
                return;
            }
            const auto batch = GeneratedArtifactSet({
                generated("z/api.cpp", "blocked\n"),
                generated("a.cpp", "written first\n"),
            });

            expect(!(write_artifacts(temporary.path(), batch).has_value()));
            expect_equal(contents(temporary.path() / "a.cpp"), std::string_view("written first\n"));
            expect_equal(contents(temporary.path() / "z"), std::string_view("not a directory\n"));
        };
});

} // namespace
