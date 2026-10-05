module;
#include <cstdio>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

module carven:analyzer.main;

import :analyzer.protocol;
import :analyzer.session;
import std;

namespace {
auto read_frame() noexcept -> std::expected<std::optional<std::string>, std::string> {
    auto header = std::array<unsigned char, 4>();
    const auto count = std::fread(header.data(), 1uz, header.size(), stdin);
    if (std::ferror(stdin)) {
        return std::unexpected("cannot read stdin");
    }
    if (count == 0uz && std::feof(stdin)) {
        return std::nullopt;
    }
    if (count != header.size()) {
        return std::unexpected("truncated frame header");
    }
    auto size = 0u;
    for (auto index = 0uz; index < header.size(); ++index) {
        size |= static_cast<std::uint32_t>(header[index]) << (index * 8uz);
    }
    if (size > analyzer_frame_limit) {
        return std::unexpected("request exceeds frame limit");
    }
    auto payload = std::string(size, '\0');
    const auto read = std::fread(payload.data(), 1uz, payload.size(), stdin);
    if (std::ferror(stdin)) {
        return std::unexpected("cannot read stdin");
    }
    if (read != payload.size()) {
        return std::unexpected("truncated frame payload");
    }
    return payload;
}

auto write_frame(std::string_view payload) noexcept -> bool {
    auto header = std::array<unsigned char, 4>();
    const auto size = static_cast<std::uint32_t>(payload.size());
    for (auto index = 0uz; index < header.size(); ++index) {
        header[index] = static_cast<unsigned char>(size >> (index * 8uz));
    }
    return std::fwrite(header.data(), 1uz, header.size(), stdout) == header.size()
        && std::fwrite(payload.data(), 1uz, payload.size(), stdout) == payload.size()
        && std::fflush(stdout) == 0;
}
} // namespace

extern "C++" auto main(int argc, char**) noexcept -> int {
    if (argc != 1) {
        std::println(
            stderr,
            "carven-analyzer: accepts no arguments; communicate through stdin/stdout"
        );
        return 2;
    }
#if defined(_WIN32)
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::println(stderr, "carven-analyzer: cannot enable binary standard streams");
        return 2;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    auto session = AnalyzerSession();
    for (;;) {
        auto frame = read_frame();
        if (!frame) {
            std::println(stderr, "carven-analyzer: {}", frame.error());
            return 2;
        }
        if (!*frame) {
            return 0;
        }
        auto request = decode_analyzer_request(**frame);
        const auto stop = request && std::holds_alternative<AnalyzerStop>(*request);
        const auto response = request
            ? session.execute(std::move(*request))
            : AnalyzerResponse {
                  .document_versions = {},
                  .result = AnalyzerFailure {.code = "request", .message = request.error()}
              };
        auto payload = encode_analyzer_response(response);
        if (!payload) {
            payload = encode_analyzer_response(
                AnalyzerResponse {
                    .document_versions = {},
                    .result = AnalyzerFailure {.code = "response_limit", .message = payload.error()}
                }
            );
        }
        if (!payload || !write_frame(*payload)) {
            std::println(stderr, "carven-analyzer: cannot write response");
            return 2;
        }
        if (stop) {
            return 0;
        }
    }
}
