module carven:analyzer.protocol;

import :analyzer.session;
import std;

// Bounds each frame before allocation; encoding also rejects larger responses.
inline constexpr auto analyzer_frame_limit = 16u * 1024u * 1024u;

auto decode_analyzer_request(std::string_view payload) noexcept
    -> std::expected<AnalyzerRequest, std::string>;
auto encode_analyzer_response(const AnalyzerResponse& response) noexcept
    -> std::expected<std::string, std::string>;
