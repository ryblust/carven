module carven:analyzer.protocol.impl;

import :analyzer.protocol;
import :source.text;
import :workspace.analysis;
import :workspace.folding;
import std;

namespace {

class Reader final {
    std::span<const std::uint8_t> bytes;
    std::size_t cursor = 0;
    bool valid = true;

public:
    explicit Reader(std::span<const std::uint8_t> bytes) noexcept;
    auto complete() const noexcept -> bool;
    auto byte() noexcept -> std::uint8_t;
    auto number(std::size_t width = 4) noexcept -> std::uint64_t;
    auto count() noexcept -> std::uint32_t;
    auto text() noexcept -> std::string;
    auto strings() noexcept -> std::vector<std::string>;
    auto optional_number() noexcept -> std::optional<std::uint32_t>;
};

Reader::Reader(std::span<const std::uint8_t> bytes) noexcept
    : bytes(bytes) {}

auto Reader::complete() const noexcept -> bool {
    return valid && cursor == bytes.size();
}

auto Reader::byte() noexcept -> std::uint8_t {
    if (cursor == bytes.size()) {
        valid = false;
        return 0;
    }
    return bytes[cursor++];
}

auto Reader::number(std::size_t width) noexcept -> std::uint64_t {
    auto value = std::uint64_t(0);
    for (auto index = 0uz; index < width; ++index) {
        value = (value << 8) | byte();
    }
    return value;
}

auto Reader::count() noexcept -> std::uint32_t {
    const auto size = number();
    if (size > bytes.size() - cursor) {
        valid = false;
        return 0;
    }
    return static_cast<std::uint32_t>(size);
}

auto Reader::text() noexcept -> std::string {
    const auto size = count();
    if (size == 0) {
        return {};
    }
    const auto value = std::string(reinterpret_cast<const char*>(bytes.data() + cursor), size);
    cursor += size;
    return value;
}

auto Reader::strings() noexcept -> std::vector<std::string> {
    auto result = std::vector<std::string>();
    const auto size = count();
    for (auto index = 0u; index < size; ++index) {
        result.push_back(text());
    }
    return result;
}

auto Reader::optional_number() noexcept -> std::optional<std::uint32_t> {
    return byte() ? std::optional(static_cast<std::uint32_t>(number())) : std::nullopt;
}

class Writer final {
    std::vector<std::uint8_t> bytes;
    const std::map<std::string, std::string, std::less<>>& client_uris;

public:
    explicit Writer(const std::map<std::string, std::string, std::less<>>& client_uris) noexcept;
    auto byte(std::uint8_t value) noexcept -> void;
    auto number(std::uint64_t value, std::size_t width = 4) noexcept -> void;
    auto text(std::string_view value) noexcept -> void;
    auto uri(std::string_view value) noexcept -> void;
    auto take_bytes() && noexcept -> std::vector<std::uint8_t>;

    template<typename Item>
    auto list(const std::vector<Item>& values) noexcept -> void {
        number(values.size());
        for (const auto& value : values) {
            write(value);
        }
    }

    template<typename Item>
    auto write(const std::optional<Item>& value) noexcept -> void {
        byte(value.has_value());
        if (value) {
            write(*value);
        }
    }

    template<typename Item>
    auto write(const AnalyzerTextItems<Item>& value) noexcept -> void {
        text(value.text);
        list(value.items);
    }

    auto write(std::monostate value) noexcept -> void;
    auto write(const std::string& value) noexcept -> void;
    auto write(AnalyzerHello value) noexcept -> void;
    auto write(AnalyzerRange value) noexcept -> void;
    auto write(const AnalyzerEdit& value) noexcept -> void;
    auto write(const AnalyzerDocument& value) noexcept -> void;
    auto write(const AnalyzerDiagnosticRelated& value) noexcept -> void;
    auto write(const AnalyzerDiagnostic& value) noexcept -> void;
    auto write(const AnalyzerWatch& value) noexcept -> void;
    auto write(const AnalyzerDocumentEdits& value) noexcept -> void;
    auto write(const AnalyzerAction& value) noexcept -> void;
    auto write(AnalyzerToken value) noexcept -> void;
    auto write(const AnalyzerHint& value) noexcept -> void;
    auto write(WorkspaceFold value) noexcept -> void;
    auto write(const AnalyzerSymbol& value) noexcept -> void;
    auto write(const AnalyzerSignature& value) noexcept -> void;
    auto write(const AnalyzerRename& value) noexcept -> void;
    auto write(const AnalyzerCompletion& value) noexcept -> void;
    auto write(const AnalyzerLocation& value) noexcept -> void;
    auto write(const AnalyzerHover& value) noexcept -> void;
    auto write(const WorkspaceCallable& value) noexcept -> void;
    auto write(Span value) noexcept -> void;
    auto write(const WorkspaceCall& value) noexcept -> void;
    auto write(const AnalyzerProjects& value) noexcept -> void;
    auto write(const AnalyzerWorkspace& value) noexcept -> void;
    auto write(const AnalyzerSymbols& value) noexcept -> void;
    auto write(const AnalyzerCompletions& value) noexcept -> void;
    auto write(const AnalyzerLocations& value) noexcept -> void;
    auto write(const AnalyzerCalls& value) noexcept -> void;
};

Writer::Writer(const std::map<std::string, std::string, std::less<>>& client_uris) noexcept
    : client_uris(client_uris) {}

auto Writer::byte(std::uint8_t value) noexcept -> void {
    bytes.push_back(value);
}

auto Writer::number(std::uint64_t value, std::size_t width) noexcept -> void {
    for (auto index = width; index-- > 0uz;) {
        byte(static_cast<std::uint8_t>(value >> (index * 8)));
    }
}

auto Writer::text(std::string_view value) noexcept -> void {
    number(value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

auto Writer::uri(std::string_view value) noexcept -> void {
    const auto found = client_uris.find(value);
    text(found == client_uris.end() ? value : std::string_view(found->second));
}

auto Writer::take_bytes() && noexcept -> std::vector<std::uint8_t> {
    return std::move(bytes);
}

auto Writer::write(std::monostate) noexcept -> void {}

auto Writer::write(const std::string& value) noexcept -> void {
    text(value);
}

auto Writer::write(AnalyzerHello value) noexcept -> void {
    number(value.version);
}

auto Writer::write(AnalyzerRange value) noexcept -> void {
    number(value.start);
    number(value.end);
}

auto Writer::write(const AnalyzerEdit& value) noexcept -> void {
    write(value.range);
    text(value.replacement);
}

auto Writer::write(const AnalyzerDocument& value) noexcept -> void {
    uri(value.uri);
    text(value.text);
    number(static_cast<std::uint64_t>(value.version), 8);
    byte(value.open);
}

auto Writer::write(const AnalyzerDiagnosticRelated& value) noexcept -> void {
    uri(value.uri);
    write(value.range);
    text(value.message);
}

auto Writer::write(const AnalyzerDiagnostic& value) noexcept -> void {
    uri(value.uri);
    number(static_cast<std::uint64_t>(value.version), 8);
    write(value.range);
    text(value.message);
    text(value.code);
    byte(static_cast<std::uint8_t>(value.severity));
    list(value.related);
    list(value.notes);
}

auto Writer::write(const AnalyzerWatch& value) noexcept -> void {
    text(value.base_uri);
    text(value.pattern);
}

auto Writer::write(const AnalyzerDocumentEdits& value) noexcept -> void {
    uri(value.uri);
    text(value.text);
    list(value.edits);
}

auto Writer::write(const AnalyzerAction& value) noexcept -> void {
    text(value.title);
    byte(static_cast<std::uint8_t>(value.kind));
    write(value.diagnostic);
    list(value.documents);
}

auto Writer::write(AnalyzerToken value) noexcept -> void {
    write(value.range);
    number(value.kind);
    number(value.modifiers);
}

auto Writer::write(const AnalyzerHint& value) noexcept -> void {
    number(value.position);
    text(value.label);
    byte(static_cast<std::uint8_t>(value.kind));
}

auto Writer::write(WorkspaceFold value) noexcept -> void {
    number(value.range.start());
    number(value.range.end());
    byte(static_cast<std::uint8_t>(value.kind));
}

auto Writer::write(const AnalyzerSymbol& value) noexcept -> void {
    text(value.name);
    byte(static_cast<std::uint8_t>(value.kind));
    uri(value.uri);
    write(value.range);
    write(value.selection);
    list(value.children);
}

auto Writer::write(const AnalyzerSignature& value) noexcept -> void {
    text(value.label);
    list(value.parameters);
    byte(value.active_parameter.has_value());
    if (value.active_parameter) {
        number(*value.active_parameter);
    }
}

auto Writer::write(const AnalyzerRename& value) noexcept -> void {
    write(value.range);
    text(value.placeholder);
    list(value.documents);
}

auto Writer::write(const AnalyzerCompletion& value) noexcept -> void {
    text(value.label);
    byte(static_cast<std::uint8_t>(value.kind));
    text(value.detail);
    text(value.import_module);
    write(value.import_edit);
}

auto Writer::write(const AnalyzerLocation& value) noexcept -> void {
    uri(value.uri);
    write(value.range);
}

auto Writer::write(const AnalyzerHover& value) noexcept -> void {
    text(value.text);
    uri(value.uri);
    write(value.range);
    text(value.documentation);
}

auto Writer::write(const WorkspaceCallable& value) noexcept -> void {
    text(value.name);
    text(value.detail);
    byte(value.method);
    uri(value.location.document);
    write(AnalyzerRange {.start = value.range.start(), .end = value.range.end()});
    write(AnalyzerRange {.start = value.location.range.start(), .end = value.location.range.end()});
}

auto Writer::write(Span value) noexcept -> void {
    write(AnalyzerRange {.start = value.start(), .end = value.end()});
}

auto Writer::write(const WorkspaceCall& value) noexcept -> void {
    write(value.callable);
    list(value.ranges);
}

auto Writer::write(const AnalyzerProjects& value) noexcept -> void {
    list(value.projects);
}

auto Writer::write(const AnalyzerWorkspace& value) noexcept -> void {
    list(value.documents);
    number(value.diagnostic_texts.size());
    for (const auto& [uri, text] : value.diagnostic_texts) {
        this->uri(uri);
        this->text(text);
    }
    list(value.diagnostics);
    list(value.warnings);
    list(value.watchers);
}

auto Writer::write(const AnalyzerSymbols& value) noexcept -> void {
    list(value.symbols);
    list(value.documents);
}

auto Writer::write(const AnalyzerCompletions& value) noexcept -> void {
    write(value.replacement);
    list(value.completions);
}

auto Writer::write(const AnalyzerLocations& value) noexcept -> void {
    list(value.locations);
    list(value.documents);
}

auto Writer::write(const AnalyzerCalls& value) noexcept -> void {
    write(value.callable);
    list(value.calls);
    list(value.documents);
}

} // namespace

auto decode_analyzer_request(std::span<const std::uint8_t> bytes) noexcept
    -> std::optional<AnalyzerRequest> {
    auto input = Reader(bytes);
    const auto method = input.byte();
    if (method > static_cast<std::uint8_t>(AnalyzerMethod::OutgoingCalls)) {
        return std::nullopt;
    }
    auto result = AnalyzerRequest {
        .method = static_cast<AnalyzerMethod>(method),
        .revision = 0,
        .uri = {},
        .text = {},
        .target = {},
        .query = {},
        .new_name = {},
        .context = {},
        .version = 0,
        .open = false,
        .offset = 0,
        .start = std::nullopt,
        .end = std::nullopt,
        .include_declaration = false,
        .quick_fix = false,
        .organize_imports = false,
        .roots = {},
        .projects = std::nullopt
    };
    switch (result.method) {
        case AnalyzerMethod::Hello:
        case AnalyzerMethod::Shutdown:  break;
        case AnalyzerMethod::Discover:  result.roots = input.strings(); break;
        case AnalyzerMethod::Workspace: {
            result.roots = input.strings();
            result.target = input.text();
            if (input.byte()) {
                result.projects.emplace();
                const auto count = input.count();
                for (auto index = 0u; index < count; ++index) {
                    const auto root = input.text();
                    const auto directory = input.text();
                    const auto available = input.byte() != 0;
                    auto project = AnalyzerProjectInput {
                        .root = root,
                        .directory = directory,
                        .roots = input.strings(),
                        .scripts = input.strings(),
                        .targets = {},
                        .available = available
                    };
                    const auto targets = input.count();
                    for (auto target = 0u; target < targets; ++target) {
                        auto value = AnalyzerTargetInput {
                            .name = input.text(),
                            .default_target = input.byte() != 0,
                            .sources = input.strings()
                        };
                        project.targets.push_back(std::move(value));
                    }
                    result.projects->push_back(std::move(project));
                }
            }
            break;
        }
        case AnalyzerMethod::Update:
            result.uri = input.text();
            result.version = static_cast<std::int64_t>(input.number(8));
            result.open = input.byte() != 0;
            result.text = input.text();
            break;
        case AnalyzerMethod::Close: result.uri = input.text(); break;
        default:
            result.revision = input.number(8);
            if (result.method == AnalyzerMethod::WorkspaceSymbols) {
                result.query = input.text();
                break;
            }
            result.uri = input.text();
            if (result.method == AnalyzerMethod::SemanticTokens
                || result.method == AnalyzerMethod::InlayHints
                || result.method == AnalyzerMethod::CodeActions) {
                result.start = input.optional_number();
                result.end = input.optional_number();
                if (result.method == AnalyzerMethod::CodeActions) {
                    result.quick_fix = input.byte() != 0;
                    result.organize_imports = input.byte() != 0;
                }
            } else if (result.method != AnalyzerMethod::Formatting
                       && result.method != AnalyzerMethod::DocumentSymbols
                       && result.method != AnalyzerMethod::FoldingRanges) {
                result.offset = static_cast<std::uint32_t>(input.number());
                if (result.method == AnalyzerMethod::IncomingCalls
                    || result.method == AnalyzerMethod::OutgoingCalls) {
                    result.context = input.text();
                }
                if (result.method == AnalyzerMethod::References) {
                    result.include_declaration = input.byte() != 0;
                }
                if (result.method == AnalyzerMethod::Rename) {
                    result.new_name = input.text();
                }
            }
            break;
    }
    return input.complete() ? std::optional(std::move(result)) : std::nullopt;
}

auto encode_analyzer_reply(
    const AnalyzerReply& reply,
    const std::map<std::string, std::string, std::less<>>& client_uris
) noexcept -> std::vector<std::uint8_t> {
    auto output = Writer(client_uris);
    if (!reply.result) {
        const auto& failure = reply.result.error();
        if (const auto* message = std::get_if<std::string>(&failure)) {
            output.byte(false);
            output.text(*message);
        } else {
            const auto& query = std::get<AnalyzerQueryFailure>(failure);
            output.byte(true);
            output.number(reply.revision, 8);
            output.byte(true);
            output.byte(static_cast<std::uint8_t>(query.code));
            output.text(query.message);
        }
        return std::move(output).take_bytes();
    }

    output.byte(true);
    output.number(reply.revision, 8);
    output.byte(false);
    std::visit([&](const auto& value) noexcept { output.write(value); }, *reply.result);
    return std::move(output).take_bytes();
}
