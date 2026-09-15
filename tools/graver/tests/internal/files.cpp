module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.graver.files;

import :graver.batch;
import :graver.files;
import :source.manager;
import :support.file;
import :support.path;
import std;

namespace {

class Fixture final {
public:
    Fixture() noexcept;
    ~Fixture() noexcept;
    auto path() const noexcept -> std::filesystem::path;
    auto entries() const noexcept -> std::size_t;

private:
    std::filesystem::path directory;
};

Fixture::Fixture() noexcept {
    auto error = std::error_code();
    const auto base = std::filesystem::temp_directory_path(error);
    REQUIRE_FALSE(error);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (auto i = 0; i < 128; ++i) {
        const auto candidate = base / std::format("graver-files-{}-{}", stamp, i);
        if (std::filesystem::create_directory(candidate, error)) {
            directory = candidate;
            return;
        }
    }
    REQUIRE(false);
}

Fixture::~Fixture() noexcept {
    auto ignored = std::error_code();
    std::filesystem::remove_all(directory, ignored);
}

auto Fixture::path() const noexcept -> std::filesystem::path {
    return directory / "source.cv";
}

auto Fixture::entries() const noexcept -> std::size_t {
    auto error = std::error_code();
    const auto iterator = std::filesystem::directory_iterator(directory, error);
    REQUIRE_FALSE(error);
    return static_cast<std::size_t>(std::distance(iterator, std::filesystem::directory_iterator()));
}

} // namespace

TEST_CASE("Graver files: replacement preserves permissions and removes staging files") {
    const auto fixture = Fixture();
    const auto path = fixture.path();
    REQUIRE(write_file(path, "original").has_value());
    auto error = std::error_code();
    const auto permissions =
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    std::filesystem::permissions(path, permissions, error);
    REQUIRE_FALSE(error);
    const auto original_permissions = std::filesystem::status(path, error).permissions();
    REQUIRE_FALSE(error);
    const auto result = graver::replace_file(path, "original", "formatted\n");
    REQUIRE(result.has_value());
    const auto text = read_file(path, 100);
    REQUIRE(text.has_value());
    CHECK(*text == "formatted\n");
    CHECK(std::filesystem::status(path, error).permissions() == original_permissions);
    CHECK_FALSE(error);
    CHECK(fixture.entries() == 1uz);
}

TEST_CASE("Graver files: concurrent edits survive a rejected replacement") {
    const auto fixture = Fixture();
    const auto path = fixture.path();
    REQUIRE(write_file(path, "new user edit").has_value());
    const auto result = graver::replace_file(path, "older source", "formatter output");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("source changed") != std::string::npos);
    const auto text = read_file(path, 100);
    REQUIRE(text.has_value());
    CHECK(*text == "new user edit");
    CHECK(fixture.entries() == 1uz);
}

TEST_CASE("Graver files: symlink replacement is refused without touching its target") {
    const auto fixture = Fixture();
    const auto target = fixture.path();
    const auto link = target.parent_path() / "link.cv";
    REQUIRE(write_file(target, "original").has_value());
    auto error = std::error_code();
    std::filesystem::create_symlink(target, link, error);
    if (error) {
        MESSAGE("symlink creation is not available on this platform: ", error.message());
        return;
    }
    CHECK_FALSE(graver::replace_file(link, "original", "formatted").has_value());
    const auto text = read_file(target, 100);
    REQUIRE(text.has_value());
    CHECK(*text == "original");
    CHECK(std::filesystem::is_symlink(link));
    CHECK(fixture.entries() == 2uz);
}

TEST_CASE("Graver files: symlink parent traversal preserves the actual formatting destination") {
    const auto fixture = Fixture();
    const auto decoy = fixture.path();
    const auto directory = decoy.parent_path();
    const auto actual_directory = directory / "other";
    const auto nested = actual_directory / "nested";
    const auto actual = actual_directory / "source.cv";
    const auto link = directory / "link";
    auto error = std::error_code();
    REQUIRE(std::filesystem::create_directories(nested, error));
    REQUIRE_FALSE(error);
    std::filesystem::create_directory_symlink(nested, link, error);
    if (error) {
        MESSAGE("directory symlinks are not available on this platform: ", error.message());
        return;
    }
    REQUIRE(write_file(decoy, "fn decoy(){}").has_value());
    REQUIRE(write_file(actual, "fn actual(){call();}").has_value());
    const auto traversal = path_to_generic_utf8(link / ".." / "source.cv");
    const auto direct = path_to_generic_utf8(actual);
    const auto arguments = std::to_array<std::string_view>({traversal, direct, traversal});
    const auto paths = graver::collect_inputs(arguments);
    REQUIRE(paths.has_value());
    REQUIRE(paths->size() == 1uz);
    CHECK(std::filesystem::equivalent(paths->front(), actual, error));
    REQUIRE_FALSE(error);
    auto sources = SourceManager();
    const auto id = sources.append_file(path_to_generic_utf8(paths->front()));
    REQUIRE(id.has_value());
    const auto inputs =
        std::to_array<graver::BatchInput>({{.path = paths->front(), .source_id = *id}});
    const auto batch = graver::format_batch(sources, inputs);
    REQUIRE(batch.has_value());
    CHECK(graver::check_report(*batch, directory) == "link/../source.cv\n");
    REQUIRE(graver::write_batch(*batch).has_value());
    const auto actual_text = read_file(actual, 100);
    const auto decoy_text = read_file(decoy, 100);
    REQUIRE(actual_text.has_value());
    REQUIRE(decoy_text.has_value());
    CHECK(*actual_text == "fn actual() {\n    call();\n}\n");
    CHECK(*decoy_text == "fn decoy(){}");
}

TEST_CASE("Graver files: collecting a symlink and its target cannot bypass write rejection") {
    const auto fixture = Fixture();
    const auto target = fixture.path();
    const auto link = target.parent_path() / "z-link.cv";
    constexpr auto original = "fn original(){}";
    REQUIRE(write_file(target, original).has_value());
    auto error = std::error_code();
    std::filesystem::create_symlink(target, link, error);
    if (error) {
        MESSAGE("symlinks are not available on this platform: ", error.message());
        return;
    }
    const auto target_name = path_to_generic_utf8(target);
    const auto link_name = path_to_generic_utf8(link);
    const auto arguments = std::to_array<std::string_view>({target_name, link_name});
    const auto paths = graver::collect_inputs(arguments);
    REQUIRE(paths.has_value());
    REQUIRE(paths->size() == 2uz);
    auto sources = SourceManager();
    auto inputs = std::vector<graver::BatchInput>();
    for (const auto& path : *paths) {
        const auto id = sources.append_file(path_to_generic_utf8(path));
        REQUIRE(id.has_value());
        inputs.push_back(graver::BatchInput {.path = path, .source_id = *id});
    }
    const auto batch = graver::format_batch(sources, inputs);
    REQUIRE(batch.has_value());
    CHECK_FALSE(graver::write_batch(*batch).has_value());
    const auto contents = read_file(target, 100);
    REQUIRE(contents.has_value());
    CHECK(*contents == original);
    CHECK(std::filesystem::is_symlink(link, error));
    CHECK_FALSE(error);
}
