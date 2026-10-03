module carven:test.graver.files;

import :graver.batch;
import :graver.files;
import :source.manager;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

class Fixture final {
public:
    Fixture() noexcept;
    auto path() const noexcept -> std::filesystem::path;
    auto entries() const noexcept -> std::size_t;

private:
    ct::TempDirectory directory;
};

Fixture::Fixture() noexcept
    : directory("graver-files") {}

auto Fixture::path() const noexcept -> std::filesystem::path {
    return directory.path("source.cv");
}

auto Fixture::entries() const noexcept -> std::size_t {
    auto error = std::error_code();
    const auto iterator = std::filesystem::directory_iterator(directory.path(), error);
    if (!ct::expect(!error)) {
        return 0;
    }
    return static_cast<std::size_t>(std::distance(iterator, std::filesystem::directory_iterator()));
}

auto write_fixture(const std::filesystem::path& path, std::string_view content) noexcept -> bool {
    auto output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    return output.good();
}

auto read_fixture(const std::filesystem::path& path) noexcept -> std::optional<std::string> {
    auto input = std::ifstream(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    auto text =
        std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (input.bad()) {
        return std::nullopt;
    }
    return text;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Graver files: replacement preserves permissions and removes staging files",
        [] static noexcept {
            const auto fixture = Fixture();
            const auto path = fixture.path();
            if (!ct::expect(write_fixture(path, "original"))) {
                return;
            }
            auto error = std::error_code();
            const auto permissions =
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
            std::filesystem::permissions(path, permissions, error);
            if (!ct::expect(!(error))) {
                return;
            }
            const auto original_permissions = std::filesystem::status(path, error).permissions();
            if (!ct::expect(!(error))) {
                return;
            }
            const auto result = graver::replace_file(path, "original", "formatted\n");
            if (!ct::expect(result.has_value())) {
                return;
            }
            const auto text = read_fixture(path);
            if (!ct::expect(text.has_value())) {
                return;
            }
            ct::expect_equal(*text, std::string_view("formatted\n"));
            ct::expect(std::filesystem::status(path, error).permissions() == original_permissions);
            ct::expect(!(error));
            ct::expect_equal(fixture.entries(), 1uz);
        }
    );

    ct::test("Graver files: concurrent edits survive a rejected replacement", [] static noexcept {
        const auto fixture = Fixture();
        const auto path = fixture.path();
        if (!ct::expect(write_fixture(path, "new user edit"))) {
            return;
        }
        const auto result = graver::replace_file(path, "older source", "formatter output");
        if (!ct::expect(!(result.has_value()))) {
            return;
        }
        ct::expect_not_equal(result.error().find("source changed"), std::string::npos);
        const auto text = read_fixture(path);
        if (!ct::expect(text.has_value())) {
            return;
        }
        ct::expect_equal(*text, std::string_view("new user edit"));
        ct::expect_equal(fixture.entries(), 1uz);
    });

    ct::test(
        "Graver files: symlink replacement is refused without touching its target",
        [] static noexcept {
            const auto fixture = Fixture();
            const auto target = fixture.path();
            const auto link = target.parent_path() / "link.cv";
            if (!ct::expect(write_fixture(target, "original"))) {
                return;
            }
            auto error = std::error_code();
            std::filesystem::create_symlink(target, link, error);
            if (error) {
                std::println(
                    std::cerr,
                    "symlink creation is not available on this platform: {}",
                    error.message()
                );
                return;
            }
            ct::expect(!(graver::replace_file(link, "original", "formatted").has_value()));
            const auto text = read_fixture(target);
            if (!ct::expect(text.has_value())) {
                return;
            }
            ct::expect_equal(*text, std::string_view("original"));
            ct::expect(std::filesystem::is_symlink(link));
            ct::expect_equal(fixture.entries(), 2uz);
        }
    );

    ct::test(
        "Graver files: symlink parent traversal follows native path resolution",
        [] static noexcept {
            const auto fixture = Fixture();
            const auto decoy = fixture.path();
            const auto directory = decoy.parent_path();
            const auto actual_directory = directory / "other";
            const auto nested = actual_directory / "nested";
            const auto actual = actual_directory / "source.cv";
            const auto link = directory / "link";
            auto error = std::error_code();
            if (!ct::expect(std::filesystem::create_directories(nested, error))) {
                return;
            }
            if (!ct::expect(!(error))) {
                return;
            }
            std::filesystem::create_directory_symlink(nested, link, error);
            if (error) {
                std::println(
                    std::cerr,
                    "directory symlinks are not available on this platform: {}",
                    error.message()
                );
                return;
            }
            if (!ct::expect(write_fixture(decoy, "fn decoy(){}"))) {
                return;
            }
            if (!ct::expect(write_fixture(actual, "fn actual(){call();}"))) {
                return;
            }
            const auto traversal = path_to_generic_utf8(link / ".." / "source.cv");
            const auto direct = path_to_generic_utf8(actual);
            const auto arguments = std::to_array<std::string_view>({traversal, direct, traversal});
            const auto paths = graver::collect_inputs(arguments);
            if (!ct::expect(paths.has_value())) {
                return;
            }
#if defined(_WIN32)
            // Win32 collapses link/.. before following the directory symlink.
            constexpr auto expected_count = 2uz;
            const auto& traversal_destination = decoy;
            constexpr auto expected_report = "link/../source.cv\nother/source.cv\n";
            constexpr auto expected_decoy = "fn decoy() {}\n";
#else
            constexpr auto expected_count = 1uz;
            const auto& traversal_destination = actual;
            constexpr auto expected_report = "link/../source.cv\n";
            constexpr auto expected_decoy = "fn decoy(){}";
#endif
            if (!ct::expect_equal(paths->size(), expected_count)) {
                return;
            }
            ct::expect(
                std::filesystem::equivalent(link / ".." / "source.cv", traversal_destination, error)
            );
            if (!ct::expect(!(error))) {
                return;
            }
            auto sources = SourceManager();
            auto inputs = std::vector<graver::BatchInput>();
            for (const auto& path : *paths) {
                const auto id = sources.append_file(path_to_generic_utf8(path));
                if (!ct::expect(id.has_value())) {
                    return;
                }
                inputs.push_back({.path = path, .source_id = *id});
            }
            const auto batch = graver::format_batch(sources, inputs);
            if (!ct::expect(batch.has_value())) {
                return;
            }
            ct::expect_equal(
                graver::check_report(*batch, directory),
                std::string_view(expected_report)
            );
            if (!ct::expect(graver::write_batch(*batch).has_value())) {
                return;
            }
            const auto actual_text = read_fixture(actual);
            const auto decoy_text = read_fixture(decoy);
            if (!ct::expect(actual_text.has_value())) {
                return;
            }
            if (!ct::expect(decoy_text.has_value())) {
                return;
            }
            ct::expect_equal(*actual_text, std::string_view("fn actual() {\n    call();\n}\n"));
            ct::expect_equal(*decoy_text, std::string_view(expected_decoy));
        }
    );

    ct::test(
        "Graver files: collecting a symlink and its target cannot bypass write rejection",
        [] static noexcept {
            const auto fixture = Fixture();
            const auto target = fixture.path();
            const auto link = target.parent_path() / "z-link.cv";
            constexpr auto original = "fn original(){}";
            if (!ct::expect(write_fixture(target, original))) {
                return;
            }
            auto error = std::error_code();
            std::filesystem::create_symlink(target, link, error);
            if (error) {
                std::println(
                    std::cerr,
                    "symlinks are not available on this platform: {}",
                    error.message()
                );
                return;
            }
            const auto target_name = path_to_generic_utf8(target);
            const auto link_name = path_to_generic_utf8(link);
            const auto arguments = std::to_array<std::string_view>({target_name, link_name});
            const auto paths = graver::collect_inputs(arguments);
            if (!ct::expect(paths.has_value())) {
                return;
            }
            if (!ct::expect_equal(paths->size(), 2uz)) {
                return;
            }
            auto sources = SourceManager();
            auto inputs = std::vector<graver::BatchInput>();
            for (const auto& path : *paths) {
                const auto id = sources.append_file(path_to_generic_utf8(path));
                if (!ct::expect(id.has_value())) {
                    return;
                }
                inputs.push_back(graver::BatchInput {.path = path, .source_id = *id});
            }
            const auto batch = graver::format_batch(sources, inputs);
            if (!ct::expect(batch.has_value())) {
                return;
            }
            ct::expect(!(graver::write_batch(*batch).has_value()));
            const auto contents = read_fixture(target);
            if (!ct::expect(contents.has_value())) {
                return;
            }
            ct::expect_equal(*contents, original);
            ct::expect(std::filesystem::is_symlink(link, error));
            ct::expect(!(error));
        }
    );
});

} // namespace
