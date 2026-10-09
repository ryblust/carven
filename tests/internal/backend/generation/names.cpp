module carven:test.internal.backend.generation.names;

import :backend.generation.names;
import :backend.target.name;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Names: content spelling preserves identity and source separation"_test = [] static noexcept {
        const auto stems = std::array {"CarvenQuery", "CarvenDisplay", "carven_constant"};
        each(
            stems,
            [](const auto& stem) static noexcept { return stem; },
            [](const auto& stem) static noexcept {
                const auto source = source_target_identifier(stem);
                expect(source.spelling() != stem);
                expect(
                    std::format("{}_1234567890123456", source.spelling())
                    != std::format("{}_1234567890123456", stem)
                );
            }
        );
        const auto requests = std::array {
            TargetContentName {.preferred = "Shared", .content = "second"},
            TargetContentName {.preferred = "Shared", .content = "first"},
            TargetContentName {.preferred = "Shared", .content = "first"},
        };
        auto occupied = std::flat_set<std::string> {"Shared"};
        const auto planned = claim_content_identifiers(requests, occupied);
        if (!expect_equal(planned.size(), requests.size())) {
            return;
        }
        expect(planned[0] != planned[1]);
        expect(planned[1] == planned[2]);
        expect(planned[0].spelling() != "Shared");
        expect(planned[1].spelling() != "Shared");
        const auto reversed = std::array {requests[2], requests[1], requests[0]};
        auto reverse_occupied = std::flat_set<std::string> {"Shared"};
        const auto reverse_names = claim_content_identifiers(reversed, reverse_occupied);
        if (!expect_equal(reverse_names.size(), requests.size())) {
            return;
        }
        expect(planned[0] == reverse_names[2]);
        expect(planned[1] == reverse_names[1]);
        expect(planned[2] == reverse_names[0]);
    };

    "Target names: local bindings and temporaries share a collision domain"_test =
        [] static noexcept {
            const auto enclosing = std::flat_set<std::string> {"operand"};
            auto names = TargetNameAllocator(enclosing);
            constexpr auto scope = TargetScopeID {.ordinal = 0};

            const auto binding = names.local_symbol("operand", 0, scope);
            const auto temporary = names.fresh(TargetTemporaryNameKind::Operand);

            expect(binding != temporary);
            expect(!enclosing.contains(std::string(binding.spelling())));
            expect(!enclosing.contains(std::string(temporary.spelling())));
            expect(binding == names.local_symbol("operand", 0, scope));
        };

    "Target names: source encoding is injective and avoids reserved and enclosing names"_test =
        [] static noexcept {
            const auto cases = std::to_array<std::string_view>(
                {"value",
                 "Record",
                 "class",
                 "class_cv",
                 "__A",
                 "A_cv",
                 "a__b",
                 "a__b_cv",
                 "CarvenDisplay",
                 "CarvenDisplay_cv",
                 "cv_name_5f5f41",
                 "cv_name_member_5f5f41",
                 "cv_name_",
                 "cv_name_member_"}
            );
            // Keep the encoded owner alive while its spelling is borrowed.
            const auto owner = source_target_identifier("__A");
            const auto owners = std::array {std::string_view("Record"), owner.spelling()};
            each(
                owners,
                [](auto name) static noexcept { return name; },
                [&](auto name) noexcept {
                    auto claimed = std::flat_set<std::string>();
                    each(
                        cases,
                        [](auto spelling) static noexcept { return spelling; },
                        [&](auto spelling) noexcept {
                            const auto encoded = source_target_identifier(spelling, name);
                            const auto text = encoded.spelling();
                            expect(TargetIdentifier::accepts_spelling(text));
                            expect(!text.contains("__"));
                            expect(
                                !(text.size() >= 2uz
                                  && text[0] == '_'
                                  && text[1] >= 'A'
                                  && text[1] <= 'Z')
                            );
                            expect(text != name);
                            expect(claimed.insert(std::string(text)).second);
                            expect(encoded == source_target_identifier(spelling, name));
                        }
                    );
                }
            );
        };

    "Target names: public encoding separates safe and escaped spellings"_test = [] static noexcept {
        const auto cases = std::to_array<std::string_view>(
            {"pricing",
             "match",
             "export",
             "export_cv",
             "class",
             "_Upper",
             "a__b",
             "cv_escaped_",
             "cv_escaped_6578706f7274"}
        );
        auto names = std::flat_set<std::string>();
        for (const auto spelling : cases) {
            const auto name = public_target_identifier(spelling);
            expect(names.insert(std::string(name.spelling())).second);
            expect(name == public_target_identifier(spelling));
        }
        expect_equal(public_target_identifier("pricing").spelling(), std::string_view("pricing"));
        expect_equal(
            public_target_identifier("export").spelling(),
            std::string_view("cv_escaped_6578706f7274")
        );
    };
});

} // namespace
