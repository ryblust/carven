module carven:test.internal.semantic.analysis.relationships;

import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite(
    [] static noexcept {
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
                        std::array {
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
                    const auto index =
                        OwnershipRegionIndex(graph.edges, graph.objects, graph.aliases);
                    each(graph.cases, &Case::name, [&](const auto& item) noexcept {
                        expect_equal(
                            index.matches(item.left, item.right, item.relation),
                            item.expected
                        );
                    });
                });
            };

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
                    expect_equal(
                        result.view().storage_loans.front().backing.object,
                        backing.object
                    );
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
    }
);

} // namespace
