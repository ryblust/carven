module carven:test.analyzer.protocol;

import :analyzer.protocol;
import :analyzer.session;
import :test.harness.framework;
import std;

namespace {

const TestSuite tests([] static noexcept {
    "Analyzer protocol: malformed complete payloads cannot produce partial requests"_test =
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::string payload;
            };
            const auto inputs = std::array {
                Input {.name = "empty", .payload = {}},
                Input {.name = "unknown operation", .payload = std::string("\xff", 1)},
                Input {.name = "trailing bytes", .payload = std::string("\x04junk", 5)},
                Input {.name = "truncated text", .payload = std::string("\x01\xff\xff\xff\xff", 5)},
                Input {
                    .name = "impossible module count",
                    .payload = std::string("\x03\xff\xff\xff\xff", 5)
                },
                Input {.name = "missing offset", .payload = std::string("\x05\0\0\0\0", 5)}
            };
            each(inputs, &Input::name, [](const Input& input) static noexcept {
                expect(!decode_analyzer_request(input.payload).has_value());
            });
        };

    "Analyzer protocol: signed versions preserve every bit in requests and responses"_test =
        [] static noexcept {
            struct Input final {
                std::string_view name;
                std::int64_t version;
                std::array<unsigned char, 8> bytes;
            };
            const auto inputs = std::array {
                Input {.name = "zero", .version = 0, .bytes = {}},
                Input {
                    .name = "negative one",
                    .version = -1,
                    .bytes = {255, 255, 255, 255, 255, 255, 255, 255}
                },
                Input {
                    .name = "minimum",
                    .version = std::numeric_limits<std::int64_t>::min(),
                    .bytes = {0, 0, 0, 0, 0, 0, 0, 128}
                },
                Input {
                    .name = "maximum",
                    .version = std::numeric_limits<std::int64_t>::max(),
                    .bytes = {255, 255, 255, 255, 255, 255, 255, 127}
                }
            };
            each(inputs, &Input::name, [](const Input& input) static noexcept {
                const auto version_bytes = std::string(
                    reinterpret_cast<const char*>(input.bytes.data()),
                    input.bytes.size()
                );
                auto payload = std::string("\x01\x01\0\0\0x", 6) + version_bytes;
                payload.append(4, '\0');
                const auto decoded = decode_analyzer_request(payload);
                if (!expect(decoded.has_value())) {
                    return;
                }
                const auto* update = std::get_if<AnalyzerUpdate>(&*decoded);
                if (!expect(update != nullptr)) {
                    return;
                }
                expect_equal(update->version, input.version);
                const auto encoded = encode_analyzer_response(
                    AnalyzerResponse {
                        .document_versions = {{.document = "x", .version = input.version}},
                        .result =
                            AnalyzerCheckResult {.published = true, .diagnostics = {}, .output = {}}
                    }
                );
                if (!expect(encoded.has_value())) {
                    return;
                }
                expect_equal(
                    *encoded,
                    std::string("\x02\x01\0\0\0\x01\0\0\0x", 10) + version_bytes
                        + std::string("\x01\0\0\0\0\0\0\0\0", 9)
                );
            });
        };

    "Analyzer protocol: decoded strings own their bytes independently of the payload"_test =
        [] static noexcept {
            auto payload = std::string("\x01\x03\0\0\0x\0y", 8);
            payload.append(8, '\0');
            payload.append("\x01\0\0\0", 4);
            payload.push_back('\xff');
            const auto decoded = decode_analyzer_request(payload);
            if (!expect(decoded.has_value())) {
                return;
            }
            payload.assign(payload.size(), 'q');
            const auto* update = std::get_if<AnalyzerUpdate>(&*decoded);
            if (!expect(update != nullptr)) {
                return;
            }
            expect_equal(update->document, std::string("x\0y", 3));
            expect_equal(update->text, std::string("\xff", 1));
        };

    "Analyzer protocol: message limits reject excess and admit boundary responses"_test =
        [] static noexcept {
            constexpr auto empty_failure_size = 13uz;
            auto response = AnalyzerResponse {
                .document_versions = {},
                .result = AnalyzerFailure {
                    .code = {},
                    .message = std::string(analyzer_frame_limit - empty_failure_size, 'x')
                }
            };
            const auto encoded = encode_analyzer_response(response);
            if (!expect(encoded.has_value())) {
                return;
            }
            expect_equal(encoded->size(), static_cast<std::size_t>(analyzer_frame_limit));
            auto* failure = std::get_if<AnalyzerFailure>(&response.result);
            require(failure != nullptr);
            failure->message.push_back('x');
            expect(!encode_analyzer_response(response).has_value());
            expect(
                !decode_analyzer_request(std::string(analyzer_frame_limit + 1uz, '\0')).has_value()
            );
        };
});

} // namespace
