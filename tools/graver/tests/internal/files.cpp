module carven:test.graver.files;

import :graver.batch;
import :graver.files;
import :source.manager;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

class Fixture final {
public:
    Fixture() noexcept;
    auto path() const noexcept -> std::filesystem::path;
    auto entries() const noexcept -> std::size_t;

private:
    TempDirectory directory;
};

Fixture::Fixture() noexcept
    : directory("graver-files") {}

auto Fixture::path() const noexcept -> std::filesystem::path {
    return directory.path("source.cv");
}

auto Fixture::entries() const noexcept -> std::size_t {
    auto error = std::error_code();
    const auto iterator = std::filesystem::directory_iterator(directory.path(), error);
    if (!expect(!error)) {
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

const TestSuite suite([] static noexcept {
    "Graver files: replacement preserves permissions and removes staging files"_test =
        [] static noexcept {
            const auto fixture = Fixture();
            const auto path = fixture.path();
            if (!expect(write_fixture(path, "original"))) {
                return;
            }
            auto error = std::error_code();
            const auto permissions =
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
            std::filesystem::permissions(path, permissions, error);
            if (!expect(!(error))) {
                return;
            }
            const auto original_permissions = std::filesystem::status(path, error).permissions();
            if (!expect(!(error))) {
                return;
            }
            const auto result = replace_formatted_file(path, "original", "formatted\n");
            if (!expect(result.has_value())) {
                return;
            }
            const auto text = read_fixture(path);
            if (!expect(text.has_value())) {
                return;
            }
            expect_equal(*text, std::string_view("formatted\n"));
            expect(std::filesystem::status(path, error).permissions() == original_permissions);
            expect(!(error));
            expect_equal(fixture.entries(), 1uz);
        };

    "Graver files: concurrent edits survive a rejected replacement"_test = [] static noexcept {
        const auto fixture = Fixture();
        const auto path = fixture.path();
        if (!expect(write_fixture(path, "new user edit"))) {
            return;
        }
        const auto result = replace_formatted_file(path, "older source", "formatter output");
        if (!expect(!(result.has_value()))) {
            return;
        }
        expect_not_equal(result.error().find("source changed"), std::string::npos);
        const auto text = read_fixture(path);
        if (!expect(text.has_value())) {
            return;
        }
        expect_equal(*text, std::string_view("new user edit"));
        expect_equal(fixture.entries(), 1uz);
    };

    "Graver files: symlink replacement is refused without touching its target"_test =
        [] static noexcept {
            const auto fixture = Fixture();
            const auto target = fixture.path();
            const auto link = target.parent_path() / "link.cv";
            if (!expect(write_fixture(target, "original"))) {
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
            expect(!(replace_formatted_file(link, "original", "formatted").has_value()));
            const auto text = read_fixture(target);
            if (!expect(text.has_value())) {
                return;
            }
            expect_equal(*text, std::string_view("original"));
            expect(std::filesystem::is_symlink(link));
            expect_equal(fixture.entries(), 2uz);
        };

    "Graver files: symlink parent traversal follows native path resolution"_test =
        [] static noexcept {
            const auto fixture = Fixture();
            const auto decoy = fixture.path();
            const auto directory = decoy.parent_path();
            const auto actual_directory = directory / "other";
            const auto nested = actual_directory / "nested";
            const auto actual = actual_directory / "source.cv";
            const auto link = directory / "link";
            auto error = std::error_code();
            if (!expect(std::filesystem::create_directories(nested, error))) {
                return;
            }
            if (!expect(!(error))) {
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
            if (!expect(write_fixture(decoy, "fn decoy(){}"))) {
                return;
            }
            if (!expect(write_fixture(actual, "fn actual(){call();}"))) {
                return;
            }
            const auto traversal = path_to_generic_utf8(link / ".." / "source.cv");
            const auto direct = path_to_generic_utf8(actual);
            const auto arguments = std::to_array<std::string_view>({traversal, direct, traversal});
            const auto paths = collect_format_paths(arguments);
            if (!expect(paths.has_value())) {
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
            if (!expect_equal(paths->size(), expected_count)) {
                return;
            }
            expect(
                std::filesystem::equivalent(link / ".." / "source.cv", traversal_destination, error)
            );
            if (!expect(!(error))) {
                return;
            }
            auto sources = SourceManager();
            auto inputs = std::vector<FormattingInput>();
            for (const auto& path : *paths) {
                const auto id = sources.append_file(path_to_generic_utf8(path));
                if (!expect(id.has_value())) {
                    return;
                }
                inputs.push_back({.path = path, .source_id = *id});
            }
            const auto batch = format_batch(sources, inputs);
            if (!expect(batch.has_value())) {
                return;
            }
            expect_equal(format_check_report(*batch, directory), std::string_view(expected_report));
            if (!expect(write_formatted_batch(*batch).has_value())) {
                return;
            }
            const auto actual_text = read_fixture(actual);
            const auto decoy_text = read_fixture(decoy);
            if (!expect(actual_text.has_value())) {
                return;
            }
            if (!expect(decoy_text.has_value())) {
                return;
            }
            expect_equal(*actual_text, std::string_view("fn actual() {\n    call();\n}\n"));
            expect_equal(*decoy_text, std::string_view(expected_decoy));
        };

    "Graver files: collecting a symlink and its target cannot bypass write rejection"_test =
        [] static noexcept {
            const auto fixture = Fixture();
            const auto target = fixture.path();
            const auto link = target.parent_path() / "z-link.cv";
            constexpr auto original = "fn original(){}";
            if (!expect(write_fixture(target, original))) {
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
            const auto paths = collect_format_paths(arguments);
            if (!expect(paths.has_value())) {
                return;
            }
            if (!expect_equal(paths->size(), 2uz)) {
                return;
            }
            auto sources = SourceManager();
            auto inputs = std::vector<FormattingInput>();
            for (const auto& path : *paths) {
                const auto id = sources.append_file(path_to_generic_utf8(path));
                if (!expect(id.has_value())) {
                    return;
                }
                inputs.push_back(FormattingInput {.path = path, .source_id = *id});
            }
            const auto batch = format_batch(sources, inputs);
            if (!expect(batch.has_value())) {
                return;
            }
            expect(!(write_formatted_batch(*batch).has_value()));
            const auto contents = read_fixture(target);
            if (!expect(contents.has_value())) {
                return;
            }
            expect_equal(*contents, original);
            expect(std::filesystem::is_symlink(link, error));
            expect(!(error));
        };
});

} // namespace
