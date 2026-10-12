module carven:test.internal.semantic.analysis.simd;

import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
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
                    const auto positions = std::array {0u, 15u, 16u, 31u, 32u, 255u};
                    auto indices = std::string();
                    auto expected = std::vector<std::uint32_t>();
                    for (auto lane = 0uz; lane < index_width; ++lane) {
                        const auto position = positions[lane % positions.size()];
                        indices += std::format("{}{},", lane == 0 ? "" : " ", position);
                        expected.push_back(position < table_width ? position + 1 : 0);
                    }
                    const auto source = std::format(
                        R"(import std::simd using *;
                        const fn gather(const table: u8x{0}, indices: u8x{1}) -> u8x{1} => table.lookup(indices);
                        const result = gather(u8x{0}::from_array([{2}]), u8x{1}::from_array([{3}]));
                        fn runtime(indices: u8x{1}) -> u8x{1} => gather(u8x{0}::from_array([{2}]), indices);
                    )",
                        table_width,
                        index_width,
                        entries,
                        indices
                    );
                    const auto program = analyze_test_program(source);
                    expect_equal(program.declarations().module_constants().size(), 1uz);
                    for (const auto [id, declaration] : program.declarations().module_constants()) {
                        static_cast<void>(id);
                        const auto& fact = program.constants().constant(declaration.value);
                        const auto* vector = std::get_if<SIMDConstant>(&fact.value);
                        if (!expect(vector != nullptr)) {
                            continue;
                        }
                        expect_equal(vector->lanes, expected)
                            .note("table lanes: ", table_width, "; index lanes: ", index_width);
                        expect_equal(
                            std::get<BuiltinTypeValue>(program.types().type(fact.type).value).kind,
                            index_width == 16 ? BuiltinType::U8x16 : BuiltinType::U8x32
                        );
                    }
                }
            }
        };
});

} // namespace
