module carven:test.graver.corpus;

import :diagnostics.report;
import :graver.format;
import :source.manager;
import :support.path;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Graver corpus: repository programs format into valid stable source"_test = [] static noexcept {
        auto paths = std::vector<std::filesystem::path>();
        for (const auto* root : {"tests/language", "tests/interop", "examples", "crafts"}) {
            auto error = std::error_code();
            auto iterator = std::filesystem::recursive_directory_iterator(root, error);
            if (!expect(!(error))) {
                return;
            }
            const auto end = std::filesystem::recursive_directory_iterator();
            while (iterator != end) {
                if (iterator->path().extension() == ".cv" && iterator->is_regular_file(error)) {
                    paths.push_back(iterator->path());
                }
                if (!expect(!(error))) {
                    return;
                }
                iterator.increment(error);
                if (!expect(!(error))) {
                    return;
                }
            }
        }
        std::ranges::sort(paths);
        auto formatted_count = 0uz;
        each(
            paths,
            [](const auto& path) static noexcept { return path_to_generic_utf8(path); },
            [&](const auto& path) noexcept {
                const auto name = path_to_generic_utf8(path);
                auto sources = SourceManager();
                const auto id = sources.append_file(name);
                if (!expect(id.has_value())) {
                    return;
                }
                const auto result = format_source(sources, *id);
                if (!result) {
                    expect(result.has_value()).note(render_diagnostics(result.error(), sources));
                    return;
                }
                const auto formatted_id = sources.append_virtual("formatted.cv", *result);
                if (!expect(formatted_id.has_value())) {
                    return;
                }
                const auto repeated = format_source(sources, *formatted_id);
                if (!repeated) {
                    expect(repeated.has_value())
                        .note(render_diagnostics(repeated.error(), sources));
                    return;
                }
                ++formatted_count;
                expect_equal(*result, *repeated);
            }
        );
        std::println("Graver corpus: {} formatted .cv files", formatted_count);
        expect(formatted_count > 0uz);
    };
});

} // namespace
