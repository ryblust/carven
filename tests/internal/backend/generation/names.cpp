module carven:test.internal.backend.generation.names;

import :backend.generation.names;
import :backend.target.name;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Target names: callable-local suffix state is isolated"_test = [] static noexcept {
        const auto enclosing = std::flat_set<std::string> {"operand"};
        auto first = TargetNameAllocator(enclosing);
        auto second = TargetNameAllocator(enclosing);

        const auto first_name = first.fresh(TargetTemporaryNameKind::Operand);
        const auto second_name = second.fresh(TargetTemporaryNameKind::Operand);
        const auto next_name = first.fresh(TargetTemporaryNameKind::Operand);
        expect(first_name == second_name);
        expect(first_name != next_name);
        expect(!enclosing.contains(std::string(first_name.spelling())));
        expect(!enclosing.contains(std::string(next_name.spelling())));
        expect_equal(enclosing, std::flat_set<std::string> {"operand"});
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

    "Target names: input reservations protect the callable scope"_test = [] static noexcept {
        auto names = TargetNameAllocator {};
        constexpr auto scope = TargetScopeID {.ordinal = 0};
        names.reserve("value", scope);

        const auto local = names.local_symbol("value", 0, scope);
        expect(local.spelling() != "value");
        expect(local == names.local_symbol("value", 0, scope));
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
