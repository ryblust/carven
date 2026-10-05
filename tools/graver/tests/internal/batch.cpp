module carven:test.graver.batch;

import :graver.batch;
import :source.manager;
import :source.text;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

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
    "Graver batch: ordered results preserve inputs without writing"_test = [] static noexcept {
        const auto fixture = TempDirectory("graver-batch");
        const auto first_path = fixture.path("z.cv");
        const auto second_path = fixture.path("a.cv");
        constexpr auto original = "fn f(){let x=1;call(x);}";
        constexpr auto formatted = "fn f() {\n    let x = 1;\n    call(x);\n}\n";
        if (!expect(write_fixture(first_path, original))) {
            return;
        }
        if (!expect(write_fixture(second_path, formatted))) {
            return;
        }
        auto sources = SourceManager();
        const auto first = sources.append_file(path_to_generic_utf8(first_path));
        const auto second = sources.append_file(path_to_generic_utf8(second_path));
        if (!expect(first.has_value())) {
            return;
        }
        if (!expect(second.has_value())) {
            return;
        }
        const auto inputs = std::to_array<FormattingInput>({
            {.path = first_path, .source_id = *first},
            {.path = second_path, .source_id = *second},
        });
        const auto batch = format_batch(sources, inputs);
        if (!expect(batch.has_value())) {
            return;
        }
        const auto files = batch->files();
        if (!expect_equal(files.size(), 2uz)) {
            return;
        }
        expect_equal(files[0].path, first_path);
        expect_equal(files[0].original, original);
        expect_equal(files[0].formatted, formatted);
        expect(files[0].changed());
        expect_equal(files[1].path, second_path);
        expect_equal(files[1].original, formatted);
        expect_equal(files[1].formatted, formatted);
        expect(!(files[1].changed()));
        expect_equal(
            format_check_report(*batch, first_path.parent_path()),
            std::string_view("z.cv\n")
        );
        const auto before = read_fixture(first_path);
        if (!expect(before.has_value())) {
            return;
        }
        expect_equal(*before, original);
        auto error = std::error_code();
        const auto timestamp = std::filesystem::last_write_time(second_path, error);
        if (!expect(!(error))) {
            return;
        }
        if (!expect(write_formatted_batch(*batch).has_value())) {
            return;
        }
        const auto after = read_fixture(first_path);
        if (!expect(after.has_value())) {
            return;
        }
        expect_equal(*after, formatted);
        expect(std::filesystem::last_write_time(second_path, error) == timestamp);
        expect(!(error));
        expect_equal(sources.view(*first).text, original);
    };

    "Graver batch: all failures are collected in input order without publishing outputs"_test =
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = sources.append_virtual("first.cv", "fn bad(");
            const auto valid = sources.append_virtual("valid.cv", "fn good(){}");
            const auto last = sources.append_virtual("last.cv", "@");
            if (!expect(first.has_value())) {
                return;
            }
            if (!expect(valid.has_value())) {
                return;
            }
            if (!expect(last.has_value())) {
                return;
            }
            const auto inputs = std::to_array<FormattingInput>({
                {.path = "last.cv", .source_id = *last},
                {.path = "valid.cv", .source_id = *valid},
                {.path = "first.cv", .source_id = *first},
            });
            const auto batch = format_batch(sources, inputs);
            if (!expect(!(batch.has_value()))) {
                return;
            }
            auto locations = std::vector<std::uint32_t>();
            for (const auto& diagnostic : batch.error()) {
                if (!expect(diagnostic.attachment.primary.has_value())) {
                    return;
                }
                const auto id = diagnostic.attachment.primary->span.source_id.index();
                if (locations.empty() || locations.back() != id) {
                    locations.push_back(id);
                }
            }
            const auto expected = std::vector<std::uint32_t> {last->index(), first->index()};
            expect_range_equal(locations, expected);
            expect_equal(sources.view(*valid).text, std::string_view("fn good(){}"));
        };

    "Graver batch: empty batches and stdin have explicit report behavior"_test =
        [] static noexcept {
            auto error = std::error_code();
            const auto directory = std::filesystem::current_path(error);
            if (!expect(!(error))) {
                return;
            }
            auto sources = SourceManager();
            {
                const auto empty = format_batch(sources, {});
                if (!expect(empty.has_value())) {
                    return;
                }
                expect(empty->files().empty());
                expect(format_check_report(*empty, directory).empty());
                expect(write_formatted_batch(*empty).has_value());
            }
            const auto id = sources.append_virtual("stdin", "fn f(){}");
            if (!expect(id.has_value())) {
                return;
            }
            const auto inputs = std::to_array<FormattingInput>({{.path = {}, .source_id = *id}});
            const auto batch = format_batch(sources, inputs);
            if (!expect(batch.has_value())) {
                return;
            }
            expect_equal(format_check_report(*batch, directory), std::string_view("stdin\n"));
            expect(!(write_formatted_batch(*batch).has_value()));
        };

    "Graver batch: every destination is checked before the first replacement"_test =
        [] static noexcept {
            const auto fixture = TempDirectory("graver-batch");
            const auto path = fixture.path("first.cv");
            constexpr auto original = "fn first(){}";
            if (!expect(write_fixture(path, original))) {
                return;
            }
            auto sources = SourceManager();
            const auto first = sources.append_file(path_to_generic_utf8(path));
            const auto second = sources.append_virtual("stdin", "fn second(){}");
            if (!expect(first.has_value())) {
                return;
            }
            if (!expect(second.has_value())) {
                return;
            }
            const auto inputs = std::to_array<FormattingInput>({
                {.path = path, .source_id = *first},
                {.path = {}, .source_id = *second},
            });
            const auto batch = format_batch(sources, inputs);
            if (!expect(batch.has_value())) {
                return;
            }
            const auto written = write_formatted_batch(*batch);
            if (!expect(!(written.has_value()))) {
                return;
            }
            expect_not_equal(written.error().find("stdin"), std::string::npos);
            const auto contents = read_fixture(path);
            if (!expect(contents.has_value())) {
                return;
            }
            expect_equal(*contents, original);
        };
});

} // namespace
