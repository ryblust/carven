module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.artifacts.materialize;

import :artifacts;
import :artifacts.materialize;
import :support.file;
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

auto generated(std::string logical_path, std::string content) noexcept -> GeneratedArtifact {
    return {.logical_path = std::move(logical_path), .content = std::move(content)};
}

auto contents(const std::filesystem::path& path) noexcept -> std::string {
    const auto result = read_file(path, 1024 * 1024);
    return result.has_value() ? *result : std::string {};
}

} // namespace

TEST_CASE("Artifacts: ArtifactSet establishes one canonical order") {
    const auto artifacts = ArtifactSet({
        generated("nested/api.hpp", "interface"),
        generated("api.cpp", "implementation"),
        generated("api.hpp", "interface"),
    });

    REQUIRE_EQ(artifacts.artifacts().size(), 3u);
    CHECK_EQ(artifacts.artifacts()[0].logical_path, "api.cpp");
    CHECK_EQ(artifacts.artifacts()[1].logical_path, "api.hpp");
    CHECK_EQ(artifacts.artifacts()[2].logical_path, "nested/api.hpp");
}

TEST_CASE("Artifacts: ordinary writes overwrite generated files and preserve stale files") {
    const auto temporary = TemporaryDirectory();
    const auto initial = ArtifactSet({
        generated("nested/api.cpp", "implementation\n"),
        generated("nested/api.hpp", "interface\n"),
        generated(".carven-artifacts", "ordinary file\n"),
    });
    REQUIRE(write_artifacts(temporary.path(), initial).has_value());

    const auto next = ArtifactSet({generated("nested/api.cpp", "replacement\n")});
    REQUIRE(write_artifacts(temporary.path(), next).has_value());

    CHECK_EQ(contents(temporary.path() / "nested/api.cpp"), "replacement\n");
    CHECK_EQ(contents(temporary.path() / "nested/api.hpp"), "interface\n");
    CHECK_EQ(contents(temporary.path() / ".carven-artifacts"), "ordinary file\n");
}

TEST_CASE("Artifacts: writes fail at the first filesystem error without rollback") {
    const auto temporary = TemporaryDirectory();
    REQUIRE(write_file(temporary.path() / "z", "not a directory\n").has_value());
    const auto batch = ArtifactSet({
        generated("z/api.cpp", "blocked\n"),
        generated("a.cpp", "written first\n"),
    });

    CHECK_FALSE(write_artifacts(temporary.path(), batch).has_value());
    CHECK_EQ(contents(temporary.path() / "a.cpp"), "written first\n");
    CHECK_EQ(contents(temporary.path() / "z"), "not a directory\n");
}
