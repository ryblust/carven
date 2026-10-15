module carven:test.internal.semantic.analysis.simd;

import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "SIMD lookup: index width determines the result and table width bounds each lane"_test =
        [] static noexcept {
            for (const auto table_width : {16uz, 32uz}) {
                for (const auto index_width : {16uz, 32uz}) {
                    auto entries = std::string();
                    for (auto lane = 0uz; lane < table_width; ++lane) {
                        entries += std::format("{}{},", lane == 0 ? "" : " ", lane + 1);
                    }
                    constexpr auto positions = std::array {0u, 15u, 16u, 31u, 32u, 255u};
                    auto indices = std::string();
                    auto expected = std::string();
                    for (auto lane = 0uz; lane < index_width; ++lane) {
                        const auto position = positions[lane % positions.size()];
                        indices += std::format("{}{},", lane == 0 ? "" : " ", position);
                        expected += std::format(
                            "{}{},",
                            lane == 0 ? "" : " ",
                            position < table_width ? position + 1 : 0
                        );
                    }
                    const auto source = std::format(
                        R"(
                        const fn gather(const table: u8x{0}, indices: u8x{1}) -> u8x{1} => table.lookup(indices);
                        const result = gather(u8x{0}::from_array([{2}]), u8x{1}::from_array([{3}]));
                        const {{ assert(result.to_array() == [{4}]); }}
                        fn runtime(indices: u8x{1}) -> u8x{1} => gather(u8x{0}::from_array([{2}]), indices);
                    )",
                        table_width,
                        index_width,
                        entries,
                        indices,
                        expected
                    );
                    static_cast<void>(analyze_test_program(source));
                }
            }
        };
});

} // namespace
