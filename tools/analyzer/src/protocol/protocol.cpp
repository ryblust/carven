module carven:analyzer.protocol.impl;

import :analyzer.protocol;
import :analyzer.session;
import :support.visit;
import :workspace.analysis;
import std;

namespace {
class RequestReader final {
public:
    explicit RequestReader(std::string_view bytes) noexcept;
    auto byte() noexcept -> std::uint8_t;
    auto u32() noexcept -> std::uint32_t;
    auto u64() noexcept -> std::uint64_t;
    auto text() noexcept -> std::string;
    auto module_count() noexcept -> std::uint32_t;
    auto complete() const noexcept -> bool;
    auto has_error() const noexcept -> bool;

private:
    std::string_view bytes;
    std::size_t offset = 0uz;
    bool valid = true;
};

RequestReader::RequestReader(std::string_view bytes) noexcept
    : bytes(bytes) {}

auto RequestReader::byte() noexcept -> std::uint8_t {
    if (offset == bytes.size()) {
        valid = false;
        return 0;
    }
    const auto result = static_cast<std::uint8_t>(bytes[offset]);
    ++offset;
    return result;
}

auto RequestReader::u32() noexcept -> std::uint32_t {
    auto result = 0u;
    for (auto shift = 0u; shift < 32u; shift += 8u) {
        result |= static_cast<std::uint32_t>(byte()) << shift;
    }
    return result;
}

auto RequestReader::u64() noexcept -> std::uint64_t {
    auto result = std::uint64_t(0);
    for (auto shift = 0u; shift < 64u; shift += 8u) {
        result |= static_cast<std::uint64_t>(byte()) << shift;
    }
    return result;
}

auto RequestReader::text() noexcept -> std::string {
    const auto size = u32();
    if (!valid || size > bytes.size() - offset) {
        valid = false;
        return {};
    }
    auto result = std::string(bytes.substr(offset, size));
    offset += size;
    return result;
}

auto RequestReader::module_count() noexcept -> std::uint32_t {
    const auto count = u32();
    // Each module has at least two four-byte string lengths.
    if (!valid || count > (bytes.size() - offset) / 8uz) {
        valid = false;
        return 0;
    }
    return count;
}

auto RequestReader::complete() const noexcept -> bool {
    return valid && offset == bytes.size();
}

auto RequestReader::has_error() const noexcept -> bool {
    return !valid;
}

class ResponseWriter final {
public:
    auto byte(std::uint8_t value) noexcept -> void;
    auto u32(std::uint32_t value) noexcept -> void;
    auto u64(std::uint64_t value) noexcept -> void;
    auto text(std::string_view value) noexcept -> void;
    auto location(const WorkspaceVersionedLocation& value) noexcept -> void;

    template<typename Values, typename Write>
    auto list(const Values& values, const Write& write) noexcept -> void {
        if (values.size() > analyzer_frame_limit / 4u) {
            valid = false;
            return;
        }
        u32(static_cast<std::uint32_t>(values.size()));
        for (const auto& value : values) {
            if (!valid) {
                return;
            }
            write(value);
        }
    }

    template<typename Value, typename Write>
    auto optional(const std::optional<Value>& value, const Write& write) noexcept -> void {
        byte(value.has_value());
        if (value && valid) {
            write(*value);
        }
    }

    auto finish() && noexcept -> std::expected<std::string, std::string>;

private:
    std::string bytes;
    bool valid = true;
};

auto ResponseWriter::byte(std::uint8_t value) noexcept -> void {
    if (bytes.size() == analyzer_frame_limit) {
        valid = false;
    }
    if (valid) {
        bytes.push_back(static_cast<char>(value));
    }
}

auto ResponseWriter::u32(std::uint32_t value) noexcept -> void {
    for (auto shift = 0u; shift < 32u; shift += 8u) {
        byte(static_cast<std::uint8_t>(value >> shift));
    }
}

auto ResponseWriter::u64(std::uint64_t value) noexcept -> void {
    for (auto shift = 0u; shift < 64u; shift += 8u) {
        byte(static_cast<std::uint8_t>(value >> shift));
    }
}

auto ResponseWriter::text(std::string_view value) noexcept -> void {
    if (value.size() > analyzer_frame_limit) {
        valid = false;
        return;
    }
    u32(static_cast<std::uint32_t>(value.size()));
    if (!valid || value.size() > analyzer_frame_limit - bytes.size()) {
        valid = false;
        return;
    }
    bytes.append(value);
}

auto ResponseWriter::location(const WorkspaceVersionedLocation& value) noexcept -> void {
    text(value.document);
    u64(std::bit_cast<std::uint64_t>(value.version));
    u32(value.range.start());
    u32(value.range.end());
}

auto ResponseWriter::finish() && noexcept -> std::expected<std::string, std::string> {
    if (!valid) {
        return std::unexpected("response exceeds frame limit");
    }
    return std::move(bytes);
}
} // namespace

auto decode_analyzer_request(std::string_view payload) noexcept
    -> std::expected<AnalyzerRequest, std::string> {
    if (payload.size() > analyzer_frame_limit) {
        return std::unexpected("request exceeds frame limit");
    }
    auto reader = RequestReader(payload);
    auto result = std::optional<AnalyzerRequest>();
    switch (reader.byte()) {
        case 1: {
            auto document = reader.text();
            const auto version = std::bit_cast<std::int64_t>(reader.u64());
            auto text = reader.text();
            result = AnalyzerUpdate {
                .document = std::move(document),
                .version = version,
                .text = std::move(text)
            };
            break;
        }
        case 2: result = AnalyzerClose {.document = reader.text()}; break;
        case 3: {
            const auto count = reader.module_count();
            auto modules = std::vector<AnalyzerProjectModule>();
            for (auto index = 0u; index < count; ++index) {
                auto document = reader.text();
                auto path = reader.text();
                if (reader.has_error()) {
                    return std::unexpected("malformed request payload");
                }
                modules.push_back(
                    {.document = std::move(document), .module_path = std::move(path)}
                );
            }
            result = AnalyzerReplaceProject {.modules = std::move(modules)};
            break;
        }
        case 4: result = AnalyzerCheck {}; break;
        case 5: {
            auto document = reader.text();
            const auto offset = reader.u32();
            result = AnalyzerHover {.document = std::move(document), .offset = offset};
            break;
        }
        case 6: {
            auto document = reader.text();
            const auto offset = reader.u32();
            result = AnalyzerDefinition {.document = std::move(document), .offset = offset};
            break;
        }
        case 7: {
            auto document = reader.text();
            const auto offset = reader.u32();
            result = AnalyzerReferences {.document = std::move(document), .offset = offset};
            break;
        }
        case 8:  result = AnalyzerStop {}; break;
        default: return std::unexpected("unknown request operation");
    }
    if (!reader.complete()) {
        return std::unexpected("malformed request payload");
    }
    return std::move(*result);
}

auto encode_analyzer_response(const AnalyzerResponse& response) noexcept
    -> std::expected<std::string, std::string> {
    auto writer = ResponseWriter();
    // Response tags are explicit wire values, independent of variant ordering.
    const auto tag = response.result.visit(
        Overloaded {
            [](const AnalyzerFailure&) static noexcept { return 0; },
            [](const AnalyzerAcknowledgement&) static noexcept { return 1; },
            [](const AnalyzerCheckResult&) static noexcept { return 2; },
            [](const AnalyzerHoverResult&) static noexcept { return 3; },
            [](const AnalyzerDefinitionResult&) static noexcept { return 4; },
            [](const AnalyzerReferencesResult&) static noexcept { return 5; }
        }
    );
    writer.byte(static_cast<std::uint8_t>(tag));
    writer.list(response.document_versions, [&](const WorkspaceDocumentVersion& document) noexcept {
        writer.text(document.document);
        writer.u64(std::bit_cast<std::uint64_t>(document.version));
    });
    const auto write_location = [&](const WorkspaceVersionedLocation& location) noexcept {
        writer.location(location);
    };
    const auto write_label = [&](const AnalyzerDiagnosticLabel& label) noexcept {
        writer.location(label.location);
        writer.text(label.message);
    };
    response.result.visit(
        Overloaded {
            [&](const AnalyzerFailure& value) noexcept {
                writer.text(value.code);
                writer.text(value.message);
            },
            [](AnalyzerAcknowledgement) static noexcept {},
            [&](const AnalyzerCheckResult& value) noexcept {
                writer.byte(value.published);
                writer.list(value.diagnostics, [&](const AnalyzerDiagnostic& diagnostic) noexcept {
                    writer.text(diagnostic.severity);
                    writer.text(diagnostic.code);
                    writer.text(diagnostic.message);
                    writer.optional(diagnostic.primary, write_label);
                    writer.list(diagnostic.related, write_label);
                    writer.list(diagnostic.notes, [&](const AnalyzerDiagnosticNote& note) noexcept {
                        writer.optional(note.location, write_location);
                        writer.text(note.message);
                    });
                    writer.list(diagnostic.helps, [&](const std::string& text) noexcept {
                        writer.text(text);
                    });
                });
                writer.list(value.output, [&](const AnalyzerOutput& output) noexcept {
                    writer.text(output.stream);
                    writer.text(output.bytes);
                });
            },
            [&](const AnalyzerHoverResult& value) noexcept {
                writer.optional(
                    value.information,
                    [&](const AnalyzerHoverInformation& info) noexcept {
                        writer.location(info.location);
                        writer.text(info.type_text);
                    }
                );
            },
            [&](const AnalyzerDefinitionResult& value) noexcept {
                writer.optional(value.location, write_location);
            },
            [&](const AnalyzerReferencesResult& value) noexcept {
                writer.optional(value.locations, [&](const auto& locations) noexcept {
                    writer.list(locations, write_location);
                });
            }
        }
    );
    return std::move(writer).finish();
}
