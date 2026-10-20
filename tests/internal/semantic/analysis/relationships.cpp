module carven:test.internal.semantic.analysis.relationships;

import :diagnostics.code;
import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Storage regions: path intersections preserve object and projection distinctions"_test =
        [] static noexcept {
            using Relation = OwnershipRegionRelation;
            struct Case final {
                std::string_view name;
                OwnershipPlace left;
                OwnershipPlace right;
                Relation relation;
                bool expected;
            };
            struct Graph final {
                std::string_view name;
                std::size_t objects;
                std::vector<OwnershipStorageEdge> edges;
                std::vector<std::pair<std::size_t, std::size_t>> aliases;
                std::vector<Case> cases;
            };
            const auto
                graphs =
                    std::
                        array {
                            Graph {
                                .name = "isolated storage",
                                .objects = 2uz,
                                .edges = {},
                                .aliases = {},
                                .cases =
                                    {
                                        Case {
                                            .name = "same object",
                                            .left = {0uz, {}},
                                            .right = {0uz, {}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "different roots",
                                            .left = {0uz, {}},
                                            .right = {1uz, {}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "reflexive ancestry",
                                            .left = {0uz, {}},
                                            .right = {0uz, {}},
                                            .relation = Relation::Ancestor,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "strict ancestry needs a path",
                                            .left = {0uz, {}},
                                            .right = {0uz, {}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "inline ancestor",
                                            .left = {0uz, {}},
                                            .right = {0uz, {7uz}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "different fields",
                                            .left = {0uz, {7uz}},
                                            .right = {0uz, {8uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                    },
                            },
                            Graph {
                                .name = "unknown selection and known siblings",
                                .objects = 4uz,
                                .edges =
                                    {
                                        {{0uz, {1uz}}, 1uz, std::nullopt},
                                        {{0uz, {1uz}}, 2uz, 0uz},
                                        {{0uz, {1uz}}, 3uz, 1uz},
                                    },
                                .aliases = {},
                                .cases =
                                    {
                                        Case {
                                            .name = "unknown can select zero",
                                            .left = {1uz, {7uz}},
                                            .right = {2uz, {7uz}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "unknown can select one",
                                            .left = {1uz, {7uz}},
                                            .right = {3uz, {7uz}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "possible alias is not transitive",
                                            .left = {2uz, {7uz}},
                                            .right = {3uz, {7uz}},
                                            .relation = Relation::Alias,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "known siblings do not overlap",
                                            .left = {2uz, {}},
                                            .right = {3uz, {}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "unknown preserves terminal field",
                                            .left = {1uz, {7uz}},
                                            .right = {2uz, {8uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "carrier protects selected storage",
                                            .left = {0uz, {1uz}},
                                            .right = {2uz, {7uz}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = true
                                        },
                                    },
                            },
                            Graph {
                                .name = "shared prefixes and distinct destinations",
                                .objects = 6uz,
                                .edges =
                                    {
                                        {{0uz, {}}, 1uz, 7uz},
                                        {{0uz, {7uz}}, 2uz, 8uz},
                                        {{1uz, {}}, 3uz, 9uz},
                                        {{4uz, {}}, 1uz, 6uz},
                                        {{5uz, {7uz}}, 2uz, 8uz},
                                    },
                                .aliases = {},
                                .cases =
                                    {
                                        Case {
                                            .name = "inline path reaches its destination",
                                            .left = {2uz, {}},
                                            .right = {0uz, {7uz, 8uz}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "shared object reaches its descendant",
                                            .left = {3uz, {}},
                                            .right = {4uz, {6uz, 9uz}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "shared prefix is not an object identity",
                                            .left = {2uz, {}},
                                            .right = {4uz, {6uz, 8uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "distinct destination suffixes",
                                            .left = {2uz, {9uz}},
                                            .right = {3uz, {8uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "multiple carriers retain ancestry",
                                            .left = {5uz, {7uz}},
                                            .right = {2uz, {9uz}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = true
                                        },
                                    },
                            },
                            Graph {
                                .name = "pairwise caller roots",
                                .objects = 3uz,
                                .edges = {},
                                .aliases = {{0uz, 2uz}, {1uz, 2uz}},
                                .cases =
                                    {
                                        Case {
                                            .name = "unknown root may be zero",
                                            .left = {0uz, {}},
                                            .right = {2uz, {}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "root aliases are symmetric",
                                            .left = {2uz, {}},
                                            .right = {1uz, {}},
                                            .relation = Relation::Alias,
                                            .expected = true
                                        },
                                        Case {
                                            .name =
                                                "shared possible root does not identify siblings",
                                            .left = {0uz, {}},
                                            .right = {1uz, {}},
                                            .relation = Relation::Alias,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "known roots remain disjoint",
                                            .left = {0uz, {7uz}},
                                            .right = {1uz, {7uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "root alias preserves inline suffix",
                                            .left = {0uz, {7uz}},
                                            .right = {2uz, {8uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "root alias supports directed prefixes",
                                            .left = {0uz, {}},
                                            .right = {2uz, {7uz}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = true
                                        },
                                    },
                            },
                            Graph {
                                .name = "multi-object owning cycle",
                                .objects = 3uz,
                                .edges =
                                    {
                                        {{0uz, {0uz}}, 1uz, std::nullopt},
                                        {{1uz, {0uz}}, 0uz, std::nullopt},
                                        {{2uz, {}}, 0uz, 0uz},
                                    },
                                .aliases = {},
                                .cases =
                                    {
                                        Case {
                                            .name = "external carrier protects the cycle",
                                            .left = {2uz, {0uz, 0uz}},
                                            .right = {1uz, {1uz}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = true
                                        },
                                        Case {
                                            .name = "cycle preserves different fields",
                                            .left = {1uz, {1uz}},
                                            .right = {1uz, {2uz}},
                                            .relation = Relation::Overlap,
                                            .expected = false
                                        },
                                        Case {
                                            .name = "terminal field is not an ancestor",
                                            .left = {1uz, {1uz}},
                                            .right = {1uz, {2uz}},
                                            .relation = Relation::StrictAncestor,
                                            .expected = false
                                        },
                                    },
                            },
                            Graph {
                                .name = "cyclic owning paths",
                                .objects = 3uz,
                                .edges =
                                    {
                                        {{0uz, {1uz}}, 1uz, 0uz},
                                        {{1uz, {1uz}}, 1uz, 0uz},
                                        {{2uz, {}}, 0uz, 0uz},
                                    },
                                .aliases = {},
                                .cases = {
                                    Case {
                                        .name = "recursive region contains a deeper path",
                                        .left = {1uz, {7uz}},
                                        .right = {0uz, {1uz, 0uz, 1uz, 0uz, 7uz}},
                                        .relation = Relation::Overlap,
                                        .expected = true
                                    },
                                    Case {
                                        .name = "cycle admits strict self ancestry",
                                        .left = {1uz, {}},
                                        .right = {1uz, {}},
                                        .relation = Relation::StrictAncestor,
                                        .expected = true
                                    },
                                    Case {
                                        .name = "cycle preserves terminal fields",
                                        .left = {1uz, {7uz}},
                                        .right = {1uz, {8uz}},
                                        .relation = Relation::Overlap,
                                        .expected = false
                                    },
                                    Case {
                                        .name = "external owner remains an ancestor",
                                        .left = {2uz, {}},
                                        .right = {1uz, {7uz}},
                                        .relation = Relation::StrictAncestor,
                                        .expected = true
                                    },
                                    Case {
                                        .name = "a descendant is not its external owner",
                                        .left = {1uz, {}},
                                        .right = {2uz, {}},
                                        .relation = Relation::Ancestor,
                                        .expected = false
                                    },
                                },
                            },
            };
            each(graphs, &Graph::name, [](const auto& graph) static noexcept {
                const auto index = OwnershipRegionIndex(graph.edges, graph.objects, graph.aliases);
                each(graph.cases, &Case::name, [&](const auto& item) noexcept {
                    expect_equal(
                        index.matches(item.left, item.right, item.relation),
                        item.expected
                    );
                });
            });
        };

    "Storage effects: helper scalar writes preserve pending operation backing"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(import std::async using close;

                    async fn bump(&value: i32) -> i32 { value += 1; return value; }
                    fn helper_set(&value: i32) { value = 4; }
                    fn relay(&value: i32) { helper_set(&value); }
                    async fn accepted() {
                        var value: i32 = 1;
                        let op = bump(&value);
                        relay(&value);
                        print(await op);
                        var values = Vector<i32> {};
                        values.push(1);
                        async let child = bump(&values[0]);
                        relay(&values[0]);
                        print(await child);
                        values.push(2);
                        async let closed = bump(&values[0]);
                        await close(closed);
                        values.clear();
                    }
                )"));
            for (const auto mutation : {
                     "append(&values);",
                     "remove_first(&values);",
                     "clear(&values);",
                     "take_owner(&&values);",
                 }) {
                const auto text = std::string(R"(
                        async fn bump(&value: i32) -> i32 => value;
                        fn append(&values: Vector<i32>) { values.push(2); }
                        fn remove_first(&values: Vector<i32>) { let item = values.remove(0); }
                        fn clear(&values: Vector<i32>) { values.clear(); }
                        fn take_owner(&&values: Vector<i32>) {}
                        async fn rejected() {
                            var values = Vector<i32> {};
                            values.push(1);
                            async let child = bump(&values[0]);
                    )")
                    + mutation + " print(await child); }";
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
        };

    "Semantic relationships: unknown element projection preserves set identity"_test =
        [] static noexcept {
            const auto program = analyze_test_program("fn source() {}");
            for (const auto [id, body] : program.bodies().entries()) {
                static_cast<void>(id);
                const auto origin = body.region().origin;
                constexpr auto backing = OwnershipPlace {.object = 0uz, .path = {}};
                const auto source = OwnershipRelationships(
                    OwnershipRelationshipRows {
                        .callable_loans =
                            {{.holder = {0uz},
                              .backing = backing,
                              .callable = std::nullopt,
                              .origin = origin,
                              .direct_only = false},
                             {.holder = {1uz},
                              .backing = backing,
                              .callable = std::nullopt,
                              .origin = origin,
                              .direct_only = false}},
                        .captures =
                            {{.holder = {0uz}, .target = backing, .origin = origin},
                             {.holder = {1uz}, .target = backing, .origin = origin}},
                        .storage_loans =
                            {{.holder = {0uz},
                              .backing = backing,
                              .origin = origin,
                              .protection = OwnershipLoanProtection::Contents,
                              .sequence_type = std::nullopt},
                             {.holder = {1uz},
                              .backing = backing,
                              .origin = origin,
                              .protection = OwnershipLoanProtection::Contents,
                              .sequence_type = std::nullopt}},
                        .formal_bundles = {}
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
            constexpr auto backing = OwnershipPlace {.object = 0uz, .path = {}};
            auto first = OwnershipRelationships(
                OwnershipRelationshipRows {
                    .callable_loans =
                        {{.holder = {},
                          .backing = backing,
                          .callable = std::nullopt,
                          .origin = origins[1],
                          .direct_only = true},
                         {.holder = {},
                          .backing = backing,
                          .callable = std::nullopt,
                          .origin = origins[0],
                          .direct_only = false}},
                    .captures =
                        {{.holder = {}, .target = backing, .origin = origins[1]},
                         {.holder = {}, .target = backing, .origin = origins[0]}},
                    .storage_loans =
                        {{.holder = {},
                          .backing = backing,
                          .origin = origins[1],
                          .protection = OwnershipLoanProtection::Contents,
                          .sequence_type = std::nullopt},
                         {.holder = {},
                          .backing = backing,
                          .origin = origins[0],
                          .protection = OwnershipLoanProtection::Contents,
                          .sequence_type = std::nullopt}},
                    .formal_bundles = {}
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
