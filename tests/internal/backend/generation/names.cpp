module carven:test.internal.backend.generation.names;

import :backend.generation.names;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Target names: callable-local suffix state is isolated", [] static noexcept {
        const auto enclosing = std::flat_set<std::string> {"operand"};
        auto first = TargetNameAllocator(enclosing);
        auto second = TargetNameAllocator(enclosing);

        ct::expect_equal(
            first.fresh(TargetTemporaryNameKind::Operand).spelling(),
            std::string_view("operand_2")
        );
        ct::expect_equal(
            second.fresh(TargetTemporaryNameKind::Operand).spelling(),
            std::string_view("operand_2")
        );
        ct::expect_equal(
            first.fresh(TargetTemporaryNameKind::Operand).spelling(),
            std::string_view("operand_3")
        );
        ct::expect_equal(enclosing.size(), 1uz);
    });

    ct::test(
        "Target names: local bindings and temporaries share a collision domain",
        [] static noexcept {
            const auto enclosing = std::flat_set<std::string> {"operand"};
            auto names = TargetNameAllocator(enclosing);
            constexpr auto scope = TargetScopeID {.ordinal = 0};

            const auto binding = names.local_symbol("operand", 0, scope);
            const auto temporary = names.fresh(TargetTemporaryNameKind::Operand);

            ct::expect_equal(binding.spelling(), std::string_view("operand_2"));
            ct::expect_equal(temporary.spelling(), std::string_view("operand_3"));
        }
    );

    ct::test("Target names: input reservations protect the callable scope", [] static noexcept {
        auto names = TargetNameAllocator {};
        constexpr auto scope = TargetScopeID {.ordinal = 0};
        names.reserve("value", scope);

        ct::expect_equal(
            names.local_symbol("value", 0, scope).spelling(),
            std::string_view("value_2")
        );
    });

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
                const auto name = TargetNameAllocator::public_identifier(spelling);
                ct::expect(names.insert(std::string(name.spelling())).second);
                ct::expect(name == TargetNameAllocator::public_identifier(spelling));
            }
            ct::expect_equal(
                TargetNameAllocator::public_identifier("pricing").spelling(),
                std::string_view("pricing")
            );
            ct::expect_equal(
                TargetNameAllocator::public_identifier("export").spelling(),
                std::string_view("cv_escaped_6578706f7274")
            );
        }
    );
});

} // namespace
