module carven:test.internal.source.identifier;

import :source.identifier;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Source identifier: classification is independent of token kinds"_test = [] static noexcept {
        const auto keyword = classify_identifier("match");
        if (!expect(std::holds_alternative<KeywordIdentifier>(keyword))) {
            return;
        }
        expect_equal(std::get<KeywordIdentifier>(keyword).keyword, SourceKeyword::Match);
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
        each(ordinary_ids, std::identity {}, [](std::string_view spelling) static noexcept {
            expect(std::holds_alternative<OrdinaryIdentifier>(classify_identifier(spelling)))
                .note("spelling: ", spelling);
        });

        static constexpr auto keyword_ids = std::to_array<std::string_view>({
            "import",
            "match",
            "private",
            "while",
        });
        each(keyword_ids, std::identity {}, [](std::string_view spelling) static noexcept {
            expect(std::holds_alternative<KeywordIdentifier>(classify_identifier(spelling)))
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
        each(invalid_ids, std::identity {}, [](std::string_view spelling) static noexcept {
            expect(std::holds_alternative<InvalidIdentifier>(classify_identifier(spelling)))
                .note("spelling: ", spelling);
        });
    };
});

} // namespace
