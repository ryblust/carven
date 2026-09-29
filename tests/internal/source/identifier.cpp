module carven:test.internal.source.identifier;

import :source.identifier;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Source identifier: classification is independent of token kinds", [] static noexcept {
        const auto keyword = classify_identifier("match");
        if (!ct::expect(std::holds_alternative<KeywordIdentifier>(keyword))) {
            return;
        }
        ct::expect_equal(std::get<KeywordIdentifier>(keyword).keyword, SourceKeyword::Match);
        static constexpr auto ordinary_ids = std::to_array<std::string_view>({
            "name",
            "_",
            "__value",
            "__carven_module",
            "value_42",
            "module_42",
            "cv",
            "craft",
        });
        ct::each(ordinary_ids, std::identity {}, [](std::string_view spelling) static noexcept {
            ct::expect(std::holds_alternative<OrdinaryIdentifier>(classify_identifier(spelling)))
                .note("spelling: ", spelling);
        });

        static constexpr auto keyword_ids = std::to_array<std::string_view>({
            "import",
            "match",
            "private",
            "while",
        });
        ct::each(keyword_ids, std::identity {}, [](std::string_view spelling) static noexcept {
            ct::expect(std::holds_alternative<KeywordIdentifier>(classify_identifier(spelling)))
                .note("spelling: ", spelling);
        });

        static constexpr auto invalid_ids = std::to_array<std::string_view>({
            "",
            "42name",
            "42module",
            "hyphen-name",
            "dot.name",
            "变量",
        });
        ct::each(invalid_ids, std::identity {}, [](std::string_view spelling) static noexcept {
            ct::expect(std::holds_alternative<InvalidIdentifier>(classify_identifier(spelling)))
                .note("spelling: ", spelling);
        });
    });
});

} // namespace
