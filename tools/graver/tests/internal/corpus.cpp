module carven:test.graver.corpus;

import :diagnostics.report;
import :graver.format;
import :source.manager;
import :support.path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Graver corpus: repository programs format into valid stable source",
        [] static noexcept {
            auto paths = std::vector<std::filesystem::path>();
            for (const auto* root : {"tests/language", "tests/interop", "examples", "crafts"}) {
                auto error = std::error_code();
                auto iterator = std::filesystem::recursive_directory_iterator(root, error);
                if (!ct::expect(!(error))) {
                    return;
                }
                const auto end = std::filesystem::recursive_directory_iterator();
                while (iterator != end) {
                    if (iterator->path().extension() == ".cv" && iterator->is_regular_file(error)) {
                        paths.push_back(iterator->path());
                    }
                    if (!ct::expect(!(error))) {
                        return;
                    }
                    iterator.increment(error);
                    if (!ct::expect(!(error))) {
                        return;
                    }
                }
            }
            std::ranges::sort(paths);
            auto formatted_count = 0uz;
            ct::each(
                paths,
                [](const auto& path) static noexcept { return path_to_generic_utf8(path); },
                [&](const auto& path) noexcept {
                    const auto name = path_to_generic_utf8(path);
                    auto sources = SourceManager();
                    const auto id = sources.append_file(name);
                    if (!ct::expect(id.has_value())) {
                        return;
                    }
                    const auto result = graver::format(sources, *id);
                    if (!result) {
                        ct::expect(result.has_value())
                            .note(render_diagnostics(result.error(), sources));
                        return;
                    }
                    const auto formatted_id = sources.append_virtual("formatted.cv", *result);
                    if (!ct::expect(formatted_id.has_value())) {
                        return;
                    }
                    const auto repeated = graver::format(sources, *formatted_id);
                    if (!repeated) {
                        ct::expect(repeated.has_value())
                            .note(render_diagnostics(repeated.error(), sources));
                        return;
                    }
                    ++formatted_count;
                    ct::expect_equal(*result, *repeated);
                }
            );
            std::println("Graver corpus: {} formatted .cv files", formatted_count);
            ct::expect(formatted_count > 0uz);
        }
    );
});

} // namespace
