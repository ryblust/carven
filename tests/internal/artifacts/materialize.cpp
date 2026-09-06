module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.artifacts.materialize;

import :artifacts;
import :artifacts.materialize;
import :support.file;
import :test.internal.harness.death;
import std;

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() noexcept {
        static auto sequence = std::atomic<std::uint64_t>(0);
        directory_path = std::filesystem::temp_directory_path()
            / std::format("carven-artifacts-test-{}-{}",
                          std::chrono::steady_clock::now().time_since_epoch().count(),
                          sequence.fetch_add(1, std::memory_order_relaxed));
        auto error = std::error_code();
        std::filesystem::create_directories(directory_path, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;

    ~TemporaryDirectory() noexcept {
        auto ignored = std::error_code();
        std::filesystem::remove_all(directory_path, ignored);
    }

    auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;
    auto operator=(TemporaryDirectory&&) -> TemporaryDirectory& = delete;

    auto path() const noexcept -> const std::filesystem::path& { return directory_path; }

private:
    std::filesystem::path directory_path;
};

auto generated(const std::string& logical_path, const std::string& content) noexcept -> GeneratedArtifact {
    return {
        .logical_path = logical_path,
        .role = GeneratedArtifactRole::ModuleImplementation,
        .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
        .content = content,
    };
}

auto contents(const std::filesystem::path& path) noexcept -> std::string {
    const auto result = read_file(path, 1024 * 1024);
    return result.has_value() ? *result : std::string {};
}

} // namespace

TEST_CASE("Artifacts: GeneratedArtifactSet establishes one canonical order") {
    const auto artifacts = GeneratedArtifactSet({
        generated("nested/api.hpp", "interface"),
        generated("api.cpp", "implementation"),
        generated("api.hpp", "interface"),
    });

    REQUIRE_EQ(artifacts.artifacts().size(), 3u);
    CHECK_EQ(artifacts.artifacts()[0].logical_path, "api.cpp");
    CHECK_EQ(artifacts.artifacts()[1].logical_path, "api.hpp");
    CHECK_EQ(artifacts.artifacts()[2].logical_path, "nested/api.hpp");
}

TEST_CASE("Artifacts: logical paths are normalized relative paths") {
    for (const auto* const valid : {"api.hpp", "carven/api/example.hpp", ".carven-artifacts"}) {
        CAPTURE(valid);
        CHECK(validate_artifact_logical_path(valid).has_value());
    }
    for (const auto* const invalid : {
             "",
             "/api.hpp",
             "api.hpp/",
             "carven//api.hpp",
             "carven/./api.hpp",
             "carven/../api.hpp",
             "carven\\api.hpp",
             "C:/api.hpp",
         }) {
        CAPTURE(invalid);
        CHECK_FALSE(validate_artifact_logical_path(invalid).has_value());
    }
}

TEST_CASE("Artifacts: duplicate and prefix-colliding logical paths violate the set invariant") {
    CHECK(expect_termination("artifact-set-duplicate-logical-path", []() static noexcept {
        const auto artifacts = GeneratedArtifactSet({
            generated("module.cpp", "first"),
            generated("module.cpp", "second"),
        });
        static_cast<void>(artifacts);
    }));

    CHECK(expect_termination("artifact-set-prefix-logical-path", []() static noexcept {
        const auto artifacts = GeneratedArtifactSet({
            generated("carven/generated", "file"),
            generated("carven/generated/module.hpp", "descendant"),
        });
        static_cast<void>(artifacts);
    }));
}

TEST_CASE("Artifacts: ordinary writes overwrite generated files and preserve stale files") {
    const auto temporary = TemporaryDirectory();
    const auto initial = GeneratedArtifactSet({
        generated("nested/api.cpp", "implementation\n"),
        generated("nested/api.hpp", "interface\n"),
        generated(".carven-artifacts", "ordinary file\n"),
    });
    REQUIRE(write_artifacts(temporary.path(), initial).has_value());

    const auto next = GeneratedArtifactSet({generated("nested/api.cpp", "replacement\n")});
    REQUIRE(write_artifacts(temporary.path(), next).has_value());

    CHECK_EQ(contents(temporary.path() / "nested/api.cpp"), "replacement\n");
    CHECK_EQ(contents(temporary.path() / "nested/api.hpp"), "interface\n");
    CHECK_EQ(contents(temporary.path() / ".carven-artifacts"), "ordinary file\n");
}

TEST_CASE("Artifacts: writes fail at the first filesystem error without rollback") {
    const auto temporary = TemporaryDirectory();
    REQUIRE(write_file(temporary.path() / "z", "not a directory\n").has_value());
    const auto batch = GeneratedArtifactSet({
        generated("z/api.cpp", "blocked\n"),
        generated("a.cpp", "written first\n"),
    });

    CHECK_FALSE(write_artifacts(temporary.path(), batch).has_value());
    CHECK_EQ(contents(temporary.path() / "a.cpp"), "written first\n");
    CHECK_EQ(contents(temporary.path() / "z"), "not a directory\n");
}
