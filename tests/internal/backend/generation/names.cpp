module carven:test.internal.backend.generation.names;

import :backend.generation.names;
import :backend.target.name;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Target names: callable-local suffix state is isolated", [] static noexcept {
        const auto enclosing = std::flat_set<std::string> {"operand"};
        auto first = TargetNameAllocator(enclosing);
        auto second = TargetNameAllocator(enclosing);

        const auto first_name = first.fresh(TargetTemporaryNameKind::Operand);
        const auto second_name = second.fresh(TargetTemporaryNameKind::Operand);
        const auto next_name = first.fresh(TargetTemporaryNameKind::Operand);
        ct::expect(first_name == second_name);
        ct::expect(first_name != next_name);
        ct::expect(!enclosing.contains(std::string(first_name.spelling())));
        ct::expect(!enclosing.contains(std::string(next_name.spelling())));
        ct::expect_equal(enclosing, std::flat_set<std::string> {"operand"});
    });

    ct::test(
        "Target names: local bindings and temporaries share a collision domain",
        [] static noexcept {
            const auto enclosing = std::flat_set<std::string> {"operand"};
            auto names = TargetNameAllocator(enclosing);
            constexpr auto scope = TargetScopeID {.ordinal = 0};

            const auto binding = names.local_symbol("operand", 0, scope);
            const auto temporary = names.fresh(TargetTemporaryNameKind::Operand);

            ct::expect(binding != temporary);
            ct::expect(!enclosing.contains(std::string(binding.spelling())));
            ct::expect(!enclosing.contains(std::string(temporary.spelling())));
            ct::expect(binding == names.local_symbol("operand", 0, scope));
        }
    );

    ct::test("Target names: input reservations protect the callable scope", [] static noexcept {
        auto names = TargetNameAllocator {};
        constexpr auto scope = TargetScopeID {.ordinal = 0};
        names.reserve("value", scope);

        const auto local = names.local_symbol("value", 0, scope);
        ct::expect(local.spelling() != "value");
        ct::expect(local == names.local_symbol("value", 0, scope));
    });

    ct::test(
        "Target names: source encoding is injective and avoids reserved and enclosing names",
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
            ct::each(
                owners,
                [](auto name) static noexcept { return name; },
                [&](auto name) noexcept {
                    auto claimed = std::flat_set<std::string>();
                    ct::each(
                        cases,
                        [](auto spelling) static noexcept { return spelling; },
                        [&](auto spelling) noexcept {
                            const auto encoded = source_target_identifier(spelling, name);
                            const auto text = encoded.spelling();
                            ct::expect(TargetIdentifier::accepts_spelling(text));
                            ct::expect(!text.contains("__"));
                            ct::expect(
                                !(text.size() >= 2uz
                                  && text[0] == '_'
                                  && text[1] >= 'A'
                                  && text[1] <= 'Z')
                            );
                            ct::expect(text != name);
                            ct::expect(claimed.insert(std::string(text)).second);
                            ct::expect(encoded == source_target_identifier(spelling, name));
                        }
                    );
                }
            );
        }
    );

    ct::test(
        "Target names: public encoding separates safe and escaped spellings",
        [] static noexcept {
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
                ct::expect(names.insert(std::string(name.spelling())).second);
                ct::expect(name == public_target_identifier(spelling));
            }
            ct::expect_equal(
                public_target_identifier("pricing").spelling(),
                std::string_view("pricing")
            );
            ct::expect_equal(
                public_target_identifier("export").spelling(),
                std::string_view("cv_escaped_6578706f7274")
            );
        }
    );
});

} // namespace
