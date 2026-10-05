module carven:test.internal.semantic.analysis.relationships;

import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Semantic relationships: unknown element projection preserves set identity"_test =
        [] static noexcept {
            const auto program = analyze_test_program("fn source() {}");
            for (const auto [id, body] : program.bodies().entries()) {
                static_cast<void>(id);
                const auto origin = body.region().origin;
                const auto backing = OwnershipPlace {0uz, {}};
                const auto source = OwnershipRelationships(
                    OwnershipRelationshipRows {
                        .callable_loans =
                            {{{0uz}, backing, std::nullopt, origin, false},
                             {{1uz}, backing, std::nullopt, origin, false}},
                        .captures = {{{0uz}, backing, origin}, {{1uz}, backing, origin}},
                        .storage_loans = {{{0uz}, backing, origin}, {{1uz}, backing, origin}}
                    }
                );
                const auto result = project_relationships(source, {std::nullopt});
                if (!expect(result.view().callable_loans.size() == 1uz)) {
                    return;
                }
                if (!expect(result.view().captures.size() == 1uz)) {
                    return;
                }
                if (!expect(result.view().storage_loans.size() == 1uz)) {
                    return;
                }
                expect(result.view().storage_loans.front().holder.empty());
                expect(result.view().callable_loans.front().holder.empty());
                expect(result.view().captures.front().holder.empty());
                auto merged = result;
                merge_relationships(merged, result);
                expect(merged == result);
                merged.edit().storage_loans.front().backing.object = 1uz;
                expect_equal(result.view().storage_loans.front().backing.object, backing.object);
            }
        };

    "Semantic relationships: equivalent facts retain a stable diagnostic origin"_test =
        [] static noexcept {
            const auto program = analyze_test_program("fn first() {} fn second() {}");
            auto origins = std::vector<ProgramOriginID>();
            for (const auto [id, body] : program.bodies().entries()) {
                static_cast<void>(id);
                origins.push_back(body.region().origin);
            }
            if (!expect(origins.size() == 2uz)) {
                return;
            }
            std::ranges::sort(origins);
            const auto backing = OwnershipPlace {0uz, {}};
            auto first = OwnershipRelationships(
                OwnershipRelationshipRows {
                    .callable_loans =
                        {{{}, backing, std::nullopt, origins[1], true},
                         {{}, backing, std::nullopt, origins[0], false}},
                    .captures = {{{}, backing, origins[1]}, {{}, backing, origins[0]}},
                    .storage_loans = {{{}, backing, origins[1]}, {{}, backing, origins[0]}}
                }
            );
            for (auto& loan : first.edit().callable_loans) {
                loan.direct_only = false;
            }
            auto second = first;
            std::ranges::reverse(second.edit().callable_loans);
            std::ranges::reverse(second.edit().captures);
            std::ranges::reverse(second.edit().storage_loans);
            normalize_relationships(first);
            normalize_relationships(second);
            expect(first == second);
            if (!expect(first.view().callable_loans.size() == 1uz)) {
                return;
            }
            if (!expect(first.view().captures.size() == 1uz)) {
                return;
            }
            if (!expect(first.view().storage_loans.size() == 1uz)) {
                return;
            }
            expect(first.view().storage_loans.front().origin == origins.front());
            expect(first.view().callable_loans.front().origin == origins.front());
            expect(second.view().callable_loans.front().origin == origins.front());
            expect(first.view().captures.front().origin == origins.front());
            expect(second.view().captures.front().origin == origins.front());
        };
});

} // namespace
