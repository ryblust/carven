module carven:graver.batch.impl;

import :diagnostics.diagnostic;
import :graver.batch;
import :graver.files;
import :graver.format;
import :source.manager;
import :support.path;
import std;

namespace graver {

auto FormattedFile::changed() const noexcept -> bool {
    return original != formatted;
}

FormattedBatch::FormattedBatch(std::vector<FormattedFile> files) noexcept
    : outputs(std::move(files)) {}

auto FormattedBatch::files() const noexcept -> std::span<const FormattedFile> {
    return outputs;
}

auto format_batch(const SourceManager& sources, std::span<const BatchInput> inputs) noexcept
    -> std::expected<FormattedBatch, Diagnostics> {
    auto outputs = std::vector<FormattedFile>();
    outputs.reserve(inputs.size());
    auto diagnostics = Diagnostics();
    auto failed = false;
    for (const auto& input : inputs) {
        auto result = format(sources, input.source_id);
        if (!result) {
            failed = true;
            diagnostics.append_range(std::move(result.error()) | std::views::as_rvalue);
            continue;
        }
        outputs.push_back(
            FormattedFile {
                .path = input.path,
                .original = sources.view(input.source_id).text,
                .formatted = std::move(*result),
            }
        );
    }
    if (failed) {
        return std::unexpected(std::move(diagnostics));
    }
    return FormattedBatch(std::move(outputs));
}

auto check_report(const FormattedBatch& batch, const std::filesystem::path& directory) noexcept
    -> std::string {
    auto report = std::string();
    for (const auto& file : batch.files()) {
        if (!file.changed()) {
            continue;
        }
        const auto relative = file.path.lexically_relative(directory);
        report += file.path.empty() ? "stdin"
                                    : path_to_generic_utf8(relative.empty() ? file.path : relative);
        report += '\n';
    }
    return report;
}

auto write_batch(const FormattedBatch& batch) noexcept -> std::expected<void, std::string> {
    for (const auto& file : batch.files()) {
        if (file.path.empty()) {
            return std::unexpected("write requires regular files, not stdin");
        }
        auto error = std::error_code();
        const auto status = std::filesystem::symlink_status(file.path, error);
        if (error || !std::filesystem::is_regular_file(status)) {
            return std::unexpected(
                std::format(
                    "{}: write requires regular files, not symlinks",
                    path_to_generic_utf8(file.path)
                )
            );
        }
    }
    for (const auto& file : batch.files()) {
        if (!file.changed()) {
            continue;
        }
        const auto replaced = replace_file(file.path, file.original, file.formatted);
        if (!replaced) {
            return std::unexpected(
                std::format("{}: {}", path_to_generic_utf8(file.path), replaced.error())
            );
        }
    }
    return {};
}

}
