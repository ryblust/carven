module carven:test.graver.batch;

import :graver.batch;
import :source.manager;
import :source.text;
import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

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
    ct::test("Graver batch: ordered results preserve inputs without writing", [] static noexcept {
        const auto fixture = ct::TempDirectory("graver-batch");
        const auto first_path = fixture.path("z.cv");
        const auto second_path = fixture.path("a.cv");
        constexpr auto original = "fn f(){let x=1;call(x);}";
        constexpr auto formatted = "fn f() {\n    let x = 1;\n    call(x);\n}\n";
        if (!ct::expect(write_fixture(first_path, original))) {
            return;
        }
        if (!ct::expect(write_fixture(second_path, formatted))) {
            return;
        }
        auto sources = SourceManager();
        const auto first = sources.append_file(path_to_generic_utf8(first_path));
        const auto second = sources.append_file(path_to_generic_utf8(second_path));
        if (!ct::expect(first.has_value())) {
            return;
        }
        if (!ct::expect(second.has_value())) {
            return;
        }
        const auto inputs = std::to_array<graver::BatchInput>({
            {.path = first_path, .source_id = *first},
            {.path = second_path, .source_id = *second},
        });
        const auto batch = graver::format_batch(sources, inputs);
        if (!ct::expect(batch.has_value())) {
            return;
        }
        const auto files = batch->files();
        if (!ct::expect_equal(files.size(), 2uz)) {
            return;
        }
        ct::expect_equal(files[0].path, first_path);
        ct::expect_equal(files[0].original, original);
        ct::expect_equal(files[0].formatted, formatted);
        ct::expect(files[0].changed());
        ct::expect_equal(files[1].path, second_path);
        ct::expect_equal(files[1].original, formatted);
        ct::expect_equal(files[1].formatted, formatted);
        ct::expect(!(files[1].changed()));
        ct::expect_equal(
            graver::check_report(*batch, first_path.parent_path()),
            std::string_view("z.cv\n")
        );
        const auto before = read_fixture(first_path);
        if (!ct::expect(before.has_value())) {
            return;
        }
        ct::expect_equal(*before, original);
        auto error = std::error_code();
        const auto timestamp = std::filesystem::last_write_time(second_path, error);
        if (!ct::expect(!(error))) {
            return;
        }
        if (!ct::expect(graver::write_batch(*batch).has_value())) {
            return;
        }
        const auto after = read_fixture(first_path);
        if (!ct::expect(after.has_value())) {
            return;
        }
        ct::expect_equal(*after, formatted);
        ct::expect(std::filesystem::last_write_time(second_path, error) == timestamp);
        ct::expect(!(error));
        ct::expect_equal(sources.view(*first).text, original);
    });

    ct::test(
        "Graver batch: all failures are collected in input order without publishing outputs",
        [] static noexcept {
            auto sources = SourceManager();
            const auto first = sources.append_virtual("first.cv", "fn bad(");
            const auto valid = sources.append_virtual("valid.cv", "fn good(){}");
            const auto last = sources.append_virtual("last.cv", "@");
            if (!ct::expect(first.has_value())) {
                return;
            }
            if (!ct::expect(valid.has_value())) {
                return;
            }
            if (!ct::expect(last.has_value())) {
                return;
            }
            const auto inputs = std::to_array<graver::BatchInput>({
                {.path = "last.cv", .source_id = *last},
                {.path = "valid.cv", .source_id = *valid},
                {.path = "first.cv", .source_id = *first},
            });
            const auto batch = graver::format_batch(sources, inputs);
            if (!ct::expect(!(batch.has_value()))) {
                return;
            }
            auto locations = std::vector<std::uint32_t>();
            for (const auto& diagnostic : batch.error()) {
                if (!ct::expect(diagnostic.attachment.primary.has_value())) {
                    return;
                }
                const auto id = diagnostic.attachment.primary->span.source_id.index();
                if (locations.empty() || locations.back() != id) {
                    locations.push_back(id);
                }
            }
            const auto expected = std::vector<std::uint32_t> {last->index(), first->index()};
            ct::expect_range_equal(locations, expected);
            ct::expect_equal(sources.view(*valid).text, std::string_view("fn good(){}"));
        }
    );

    ct::test(
        "Graver batch: empty batches and stdin have explicit report behavior",
        [] static noexcept {
            auto error = std::error_code();
            const auto directory = std::filesystem::current_path(error);
            if (!ct::expect(!(error))) {
                return;
            }
            auto sources = SourceManager();
            {
                const auto empty = graver::format_batch(sources, {});
                if (!ct::expect(empty.has_value())) {
                    return;
                }
                ct::expect(empty->files().empty());
                ct::expect(graver::check_report(*empty, directory).empty());
                ct::expect(graver::write_batch(*empty).has_value());
            }
            const auto id = sources.append_virtual("stdin", "fn f(){}");
            if (!ct::expect(id.has_value())) {
                return;
            }
            const auto inputs = std::to_array<graver::BatchInput>({{.path = {}, .source_id = *id}});
            const auto batch = graver::format_batch(sources, inputs);
            if (!ct::expect(batch.has_value())) {
                return;
            }
            ct::expect_equal(graver::check_report(*batch, directory), std::string_view("stdin\n"));
            ct::expect(!(graver::write_batch(*batch).has_value()));
        }
    );

    ct::test(
        "Graver batch: every destination is checked before the first replacement",
        [] static noexcept {
            const auto fixture = ct::TempDirectory("graver-batch");
            const auto path = fixture.path("first.cv");
            constexpr auto original = "fn first(){}";
            if (!ct::expect(write_fixture(path, original))) {
                return;
            }
            auto sources = SourceManager();
            const auto first = sources.append_file(path_to_generic_utf8(path));
            const auto second = sources.append_virtual("stdin", "fn second(){}");
            if (!ct::expect(first.has_value())) {
                return;
            }
            if (!ct::expect(second.has_value())) {
                return;
            }
            const auto inputs = std::to_array<graver::BatchInput>({
                {.path = path, .source_id = *first},
                {.path = {}, .source_id = *second},
            });
            const auto batch = graver::format_batch(sources, inputs);
            if (!ct::expect(batch.has_value())) {
                return;
            }
            const auto written = graver::write_batch(*batch);
            if (!ct::expect(!(written.has_value()))) {
                return;
            }
            ct::expect_not_equal(written.error().find("stdin"), std::string::npos);
            const auto contents = read_fixture(path);
            if (!ct::expect(contents.has_value())) {
                return;
            }
            ct::expect_equal(*contents, original);
        }
    );
});

} // namespace
