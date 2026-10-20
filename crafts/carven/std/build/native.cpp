#include <carven/std/build/native.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace carven::standard::build {

namespace {

auto path_from_utf8(std::string_view value) noexcept -> std::filesystem::path {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

// Deterministic, non-cryptographic fingerprints for local build records.
// Length framing distinguishes values, paths, empty files, and file boundaries.
class ContentFingerprint final {
public:
    auto feed(std::string_view bytes) noexcept -> void;
    auto frame(std::string_view text) noexcept -> void;
    auto finish() const noexcept -> std::string;

private:
    std::uint64_t first = 0xcbf29ce484222325;
    std::uint64_t second = 0x6c62272e07bb0142;
};

auto ContentFingerprint::feed(std::string_view bytes) noexcept -> void {
    for (const auto value : bytes) {
        const auto byte = static_cast<unsigned char>(value);
        first = (first ^ byte) * 0x100000001b3;
        const auto mixed = second ^ byte;
        second = ((mixed << 11) | (mixed >> 53)) * 0x9e3779b185ebca87;
    }
}

auto ContentFingerprint::frame(std::string_view text) noexcept -> void {
    feed(std::format("{}:", text.size()));
    feed(text);
}

auto ContentFingerprint::finish() const noexcept -> std::string {
    return std::format("{:016x}{:016x}", first, second);
}

auto file_fingerprint(std::string_view path) noexcept -> std::string {
    if (path.find('\0') != std::string_view::npos) {
        return {};
    }
    auto file = std::ifstream(path_from_utf8(path), std::ios::binary);
    if (!file) {
        return {};
    }
    auto digest = ContentFingerprint();
    digest.frame("carven-file-v1");
    auto buffer = std::array<char, 16384>();
    auto size = std::uint64_t();
    while (file) {
        file.read(buffer.data(), buffer.size());
        const auto count = static_cast<std::size_t>(file.gcount());
        digest.feed(std::string_view(buffer.data(), count));
        size += count;
    }
    if (file.bad()) {
        return {};
    }
    digest.frame(std::format("{}", size));
    return digest.finish();
}

auto record_fingerprint(
    runtime::Slice<runtime::String> paths,
    runtime::Slice<runtime::String> values,
    std::unordered_map<std::string, std::string>& files
) noexcept -> runtime::String {
    auto digest = ContentFingerprint();
    digest.frame("carven-content-v2");
    digest.frame(std::format("{}", values.size()));
    for (const auto& value : values) {
        digest.frame(value.as_str());
    }
    digest.frame(std::format("{}", paths.size()));
    for (const auto& entry : paths) {
        const auto path = entry.as_str();
        auto [file, inserted] = files.try_emplace(std::string(path));
        if (inserted) {
            file->second = file_fingerprint(path);
        }
        if (file->second.empty()) {
            return {};
        }
        digest.frame(path);
        digest.frame(file->second);
    }
    return runtime::String::from_str(digest.finish());
}

} // namespace

auto fingerprint(
    runtime::Slice<runtime::String> paths,
    runtime::Slice<runtime::String> values
) noexcept -> runtime::String {
    auto files = std::unordered_map<std::string, std::string>();
    return record_fingerprint(paths, values, files);
}

auto fingerprints(runtime::Slice<runtime::Vector<runtime::String>> paths) noexcept
    -> runtime::Vector<runtime::String> {
    auto files = std::unordered_map<std::string, std::string>();
    auto results = runtime::Vector<runtime::String>::with_capacity(paths.size());
    for (const auto& group : paths) {
        results.push(record_fingerprint(runtime::slice(group), {}, files));
    }
    return results;
}

} // namespace carven::standard::build
