module carven:graver.batch;

import :diagnostics.diagnostic;
import :source.manager;
import :source.text;
import std;

namespace graver {

struct BatchInput final {
    std::filesystem::path path;
    SourceID source_id;
};

struct FormattedFile final {
    std::filesystem::path path;
    std::string_view original;
    std::string formatted;

    auto changed() const noexcept -> bool;
};

// Owns successful outputs; original text borrows the caller's SourceManager.
// Keep that manager alive, unmoved, and unchanged through reporting and writing.
class FormattedBatch final {
public:
    FormattedBatch(const FormattedBatch&) = delete;
    FormattedBatch(FormattedBatch&&) = default;
    auto operator=(const FormattedBatch&) -> FormattedBatch& = delete;
    auto operator=(FormattedBatch&&) -> FormattedBatch& = delete;

    // Moving or destroying this batch ends outstanding file views.
    auto files() const noexcept -> std::span<const FormattedFile>;

private:
    explicit FormattedBatch(std::vector<FormattedFile> files) noexcept;

    friend auto format_batch(
        const SourceManager& sources,
        std::span<const BatchInput> inputs
    ) noexcept -> std::expected<FormattedBatch, Diagnostics>;

    std::vector<FormattedFile> outputs;
};

// Inputs name valid sources. Preserve their order; path collection owns sorting
// and deduplication. No output or filesystem mutation occurs during formatting.
// Any failure returns all diagnostics in input order and publishes no batch.
auto format_batch(const SourceManager& sources, std::span<const BatchInput> inputs) noexcept
    -> std::expected<FormattedBatch, Diagnostics>;

// Pure report construction; directory is the caller's absolute working directory.
auto check_report(const FormattedBatch& batch, const std::filesystem::path& directory) noexcept
    -> std::string;

// Requires a successfully formatted batch. Reject all non-file destinations
// before writing, then replace changed files in order. Replacement is per-file,
// not a transaction: a later I/O failure can leave earlier files updated.
auto write_batch(const FormattedBatch& batch) noexcept -> std::expected<void, std::string>;

}
