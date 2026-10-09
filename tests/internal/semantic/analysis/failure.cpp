module carven:test.internal.semantic.analysis.failure;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.diagnostics;
import :semantic.analysis.failure;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :source.text;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Semantic failures: deferred application includes contributions added after reservation"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto first = types.builtin_type(BuiltinType::I32);
            const auto second = types.builtin_type(BuiltinType::Bool);
            const auto parameter = GenericFailureParameter {.definition = definition, .index = 0u};
            const auto source = terms.add_empty_term();
            const auto result =
                terms.application(source, {{parameter, sets.intern({first})}}, sets);
            terms.add_contribution(source, terms.add_generic_parameter(parameter));
            terms.add_member(source, second);
            terms.add_contribution(source, source);
            expect(terms.complete_applications(sets));
            auto diagnostics = DiagnosticSink();
            const auto solved = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                FailureTypeDiagnosticNames(
                    types,
                    declarations.construction_view(),
                    provenance.reader()
                ),
                AnalysisDiagnostics(diagnostics),
                false
            );
            if (!expect(solved.has_value())) {
                return;
            }
            expect(solved->closed_set(result) == std::optional(sets.intern({first, second})));
            expect(diagnostics.empty());
        };

    "Semantic failures: selective closure completes earlier cached applications"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto left = GenericFailureParameter {.definition = definition, .index = 0u};
            const auto right = GenericFailureParameter {.definition = definition, .index = 1u};
            const auto left_effect = terms.add_generic_parameter(left);
            const auto right_effect = terms.add_generic_parameter(right);
            const auto member = types.builtin_type(BuiltinType::I32);
            const auto concrete = sets.intern({member});
            const auto source = terms.add_union_term({left_effect});
            const auto earlier =
                terms.application(source, {{left, concrete}, {right, concrete}}, sets);
            const auto renamed = terms.application(source, {{left, right_effect}}, sets);
            const auto wrapper = terms.add_union_term({renamed});
            const auto selected = terms.application(wrapper, {{right, concrete}}, sets);
            const auto roots = std::array<ConstructionFailureRef, 1> {selected};
            expect(terms.complete_applications(sets, roots));
            auto diagnostics = DiagnosticSink();
            const auto solved = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                FailureTypeDiagnosticNames(
                    types,
                    declarations.construction_view(),
                    provenance.reader()
                ),
                AnalysisDiagnostics(diagnostics),
                false
            );
            if (!expect(solved.has_value())) {
                return;
            }
            expect(solved->closed_set(earlier) == std::optional(concrete));
            expect(solved->closed_set(selected) == std::optional(concrete));
            expect(diagnostics.empty());
        };

    "Semantic failures: swapped recursive environments reuse a finite equation graph"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto first = types.builtin_type(BuiltinType::I32);
            const auto second = types.builtin_type(BuiltinType::Bool);
            const auto left = GenericFailureParameter {.definition = definition, .index = 0u};
            const auto right = GenericFailureParameter {.definition = definition, .index = 1u};
            const auto left_effect = terms.add_generic_parameter(left);
            const auto right_effect = terms.add_generic_parameter(right);
            const auto swap =
                FailureParameterEnvironment {{left, right_effect}, {right, left_effect}};
            const auto source = terms.add_empty_term();
            const auto swapped = terms.application(source, swap, sets);
            expect(terms.application(source, swap, sets) == swapped);
            expect(terms.application(swapped, swap, sets) == source);
            terms.add_contribution(source, left_effect);
            terms.add_contribution(source, swapped);
            expect(terms.complete_applications(sets));
            expect_equal(terms.snapshot().terms().size(), 2uz);
            const auto closed = terms.application(
                source,
                {
                    {left, sets.intern({first})},
                    {right, sets.intern({second})},
                },
                sets
            );
            expect(terms.complete_applications(sets));
            expect_equal(terms.snapshot().terms().size(), 4uz);
            auto diagnostics = DiagnosticSink();
            const auto solved = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                FailureTypeDiagnosticNames(
                    types,
                    declarations.construction_view(),
                    provenance.reader()
                ),
                AnalysisDiagnostics(diagnostics),
                false
            );
            if (!expect(solved.has_value())) {
                return;
            }
            expect(solved->closed_set(closed) == std::optional(sets.intern({first, second})));
            expect(diagnostics.empty());
        };

    "Semantic failures: guard conjunctions normalize without restricting nonrecursive depth"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto parameter = GenericFailureParameter {.definition = definition, .index = 0u};
            const auto left = terms.add_generic_parameter(parameter);
            const auto right = terms.add_generic_parameter({.definition = definition, .index = 1u});
            const auto payload =
                terms.add_generic_concrete({types.builtin_type(BuiltinType::Bool)});
            const auto once = terms.add_generic_guard(left, payload);
            expect(terms.add_generic_guard(once, payload) == once);
            expect(terms.add_generic_guard(left, once) == once);
            const auto nested =
                terms.add_generic_guard(left, terms.add_generic_guard(right, payload));
            expect(terms.add_generic_guard(right, once) == nested);
            expect(
                terms.add_generic_guard(terms.add_generic_guard(left, right), payload) == nested
            );
            const auto source = terms.add_union_term({left});
            const auto application = terms.application(source, {{parameter, nested}}, sets);
            expect(terms.complete_applications(sets));
            expect(!terms.growing_parameter().has_value());
            expect_equal(terms.snapshot().terms().size(), 2uz);
            expect(terms.application(source, {{parameter, nested}}, sets) == application);
        };

    "Semantic failures: guarded recursive slot permutations stabilize"_test = [] static noexcept {
        const auto program = analyze_test_program("");
        const auto provenance = CompilationProvenanceBuilder();
        const auto types = CanonicalTypeStoreBuilder(program.identity());
        auto identities =
            MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
        const auto definition = identities.add(0u);
        auto terms = FailureConstraintStore(program.identity(), provenance.identity());
        auto sets = FailureSetStoreBuilder(program.identity());
        const auto left = GenericFailureParameter {.definition = definition, .index = 0u};
        const auto right = GenericFailureParameter {.definition = definition, .index = 1u};
        const auto left_effect = terms.add_generic_parameter(left);
        const auto right_effect = terms.add_generic_parameter(right);
        const auto payload = terms.add_generic_concrete({types.builtin_type(BuiltinType::Bool)});
        const auto source = terms.add_union_term({left_effect});
        const auto environment = FailureParameterEnvironment {
            {left, terms.add_generic_guard(right_effect, payload)},
            {right, left_effect},
        };
        const auto application = terms.application(source, environment, sets);
        terms.add_contribution(source, application);
        expect(terms.complete_applications(sets));
        expect(!terms.growing_parameter().has_value());
        expect_equal(terms.snapshot().terms().size(), 4uz);
        const auto term_count = terms.snapshot().terms().size();
        expect(terms.complete_applications(sets));
        expect_equal(terms.snapshot().terms().size(), term_count);
        expect(terms.application(source, environment, sets) == application);
    };

    "Semantic failures: ordinary term arguments preserve stable guarded substitution"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto parameter = GenericFailureParameter {.definition = definition, .index = 0u};
            const auto other = terms.add_generic_parameter({.definition = definition, .index = 1u});
            const auto member = types.builtin_type(BuiltinType::Bool);
            const auto payload = sets.intern({member});
            const auto argument = terms.add_empty_term();
            const auto source = terms.add_union_term({terms.add_generic_parameter(parameter)});
            const auto application = terms.application(source, {{parameter, argument}}, sets);
            terms.add_contribution(source, application);
            terms.add_guarded_contribution(argument, other, payload);
            expect(terms.complete_applications(sets));
            expect(!terms.growing_parameter().has_value());
            const auto term_count = terms.snapshot().terms().size();
            const auto normalized =
                terms.add_generic_guard(other, terms.add_generic_concrete({member}));
            expect(terms.application(source, {{parameter, argument}}, sets) == application);
            expect(terms.application(source, {{parameter, normalized}}, sets) == application);
            expect(terms.complete_applications(sets));
            expect_equal(terms.snapshot().terms().size(), term_count);
            auto diagnostics = DiagnosticSink();
            const auto solved = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                FailureTypeDiagnosticNames(
                    types,
                    declarations.construction_view(),
                    provenance.reader()
                ),
                AnalysisDiagnostics(diagnostics),
                false
            );
            if (!expect(solved.has_value())) {
                return;
            }
            expect(
                solved->support(application)
                == GenericFailureMask {.complemented = false, .members = {member}}
            );
            expect(solved->known_members(application).empty());
            expect(!solved->closed_set(application).has_value());
            expect(diagnostics.empty());
        };

    "Semantic failures: recursive union and mask substitutions stabilize"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto parameter = GenericFailureParameter {.definition = definition, .index = 0u};
            const auto effect = terms.add_generic_parameter(parameter);
            const auto excluded = types.builtin_type(BuiltinType::I32);
            const auto included = types.builtin_type(BuiltinType::Bool);
            const auto filtered = terms.add_generic_residual(effect, {excluded});
            const auto transformed =
                terms.add_generic_union({filtered, terms.add_generic_concrete({included})});
            const auto source = terms.add_union_term({effect});
            const auto application = terms.application(source, {{parameter, transformed}}, sets);
            terms.add_contribution(source, application);
            expect(terms.complete_applications(sets));
            expect(!terms.growing_parameter().has_value());
            expect_equal(terms.snapshot().terms().size(), 2uz);
            expect(terms.application(application, {{parameter, transformed}}, sets) == application);
        };

    "Semantic failures: recursive guard growth is rejected through parameter masks"_test =
        [] static noexcept {
            for (const auto masked : {false, true}) {
                const auto program = analyze_test_program("");
                const auto provenance = CompilationProvenanceBuilder();
                const auto types = CanonicalTypeStoreBuilder(program.identity());
                auto identities =
                    MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
                const auto definition = identities.add(0u);
                auto terms = FailureConstraintStore(program.identity(), provenance.identity());
                auto sets = FailureSetStoreBuilder(program.identity());
                const auto parameter =
                    GenericFailureParameter {.definition = definition, .index = 0u};
                const auto effect = terms.add_generic_parameter(parameter);
                const auto first = types.builtin_type(BuiltinType::I32);
                const auto second = types.builtin_type(BuiltinType::Bool);
                const auto payload =
                    terms.add_generic_concrete({types.builtin_type(BuiltinType::Usize)});
                const auto value = masked ? terms.add_generic_residual(effect, {first}) : effect;
                const auto gate = masked ? terms.add_generic_residual(effect, {second}) : effect;
                const auto transformed =
                    terms.add_generic_union({value, terms.add_generic_guard(gate, payload)});
                const auto source = terms.add_union_term({effect});
                const auto application =
                    terms.application(source, {{parameter, transformed}}, sets);
                terms.add_contribution(source, application);
                expect(!terms.complete_applications(sets));
                expect(terms.growing_parameter() == std::optional(parameter));
                expect_equal(terms.snapshot().terms().size(), 2uz);
                expect(!terms.complete_applications(sets));
                expect_equal(terms.snapshot().terms().size(), 2uz);
            }
        };

    "Semantic failures: parameter masks quantify over an open nominal universe"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            const auto first = types.builtin_type(BuiltinType::I32);
            const auto second = types.builtin_type(BuiltinType::Bool);
            const auto parameter =
                terms.add_generic_parameter({.definition = definition, .index = 0u});
            const auto other = terms.add_generic_parameter({.definition = definition, .index = 1u});
            const auto first_set = terms.add_generic_concrete({first});
            const auto known_universe = terms.add_generic_concrete({first, second});
            const auto residual = terms.add_generic_residual(parameter, {first});
            const auto selected = terms.add_generic_intersection(parameter, {first});
            const auto restored = terms.add_generic_union({residual, first_set});
            expect(terms.generic_subset(residual, parameter) == FailureProof::Proven);
            expect(terms.generic_subset(selected, first_set) == FailureProof::Proven);
            expect(terms.generic_subset(parameter, restored) == FailureProof::Proven);
            expect(terms.generic_subset(first_set, parameter) == FailureProof::Disproven);
            expect(terms.generic_subset(parameter, other) == FailureProof::Disproven);
            expect(terms.generic_subset(parameter, known_universe) == FailureProof::Disproven);
            expect(terms.generic_empty(parameter) == FailureProof::Unknown);
            expect(terms.generic_non_empty(parameter) == FailureProof::Unknown);
            expect(
                terms.generic_empty(terms.add_generic_residual(parameter, {first, second}))
                == FailureProof::Unknown
            );
            expect(
                terms.generic_empty(terms.add_generic_intersection(parameter, {}))
                == FailureProof::Proven
            );
            expect(terms.add_generic_guard(parameter, parameter) == parameter);
        };

    "Semantic failures: filtering a guard preserves its activation gate"_test = [] static noexcept {
        const auto program = analyze_test_program("");
        const auto provenance = CompilationProvenanceBuilder();
        const auto types = CanonicalTypeStoreBuilder(program.identity());
        auto identities =
            MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
        const auto definition = identities.add(0u);
        auto terms = FailureConstraintStore(program.identity(), provenance.identity());
        auto sets = FailureSetStoreBuilder(program.identity());
        const auto first = types.builtin_type(BuiltinType::I32);
        const auto second = types.builtin_type(BuiltinType::Bool);
        const auto parameter = terms.add_generic_parameter({.definition = definition, .index = 0u});
        const auto gate = terms.add_generic_intersection(parameter, {first});
        const auto guarded = terms.add_generic_guard(gate, terms.add_generic_concrete({second}));
        const auto residual = terms.add_generic_residual(guarded, {first});
        const auto first_only = sets.intern({first});
        auto active = FailureInstantiation(
            terms,
            sets,
            {
                .parameter = [=](GenericFailureParameter) noexcept -> ConstructionFailureRef {
                    return first_only;
                },
                .member = {},
                .origin = {},
            }
        );
        const auto delivered = active.substitute(residual);
        if (!expect(std::holds_alternative<FailureSetID>(delivered))) {
            return;
        }
        expect(sets.copy(std::get<FailureSetID>(delivered)).members == std::vector {second});
        const auto empty = sets.empty_set();
        auto inactive = FailureInstantiation(
            terms,
            sets,
            {
                .parameter = [=](GenericFailureParameter) noexcept -> ConstructionFailureRef {
                    return empty;
                },
                .member = {},
                .origin = {},
            }
        );
        expect(std::get<FailureSetID>(inactive.substitute(residual)) == empty);
    };

    "Semantic failures: guarded recursive roots remain exact across instance cloning"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto first = types.builtin_type(BuiltinType::I32);
            const auto second = types.builtin_type(BuiltinType::Bool);
            const auto parameter =
                terms.add_generic_parameter({.definition = definition, .index = 0u});
            const auto root = terms.add_union_term({parameter});
            const auto added = sets.intern({second});
            terms.add_guarded_contribution(root, root, added);
            const auto self_excited = terms.add_empty_term();
            terms.add_guarded_contribution(self_excited, self_excited, added);
            auto diagnostics = DiagnosticSink();
            const auto names = FailureTypeDiagnosticNames(
                types,
                declarations.construction_view(),
                provenance.reader()
            );
            const auto preview = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                names,
                AnalysisDiagnostics(diagnostics),
                false
            );
            if (!expect(preview.has_value())) {
                return;
            }
            expect(preview->normalize(root) == ConstructionFailureRef(root));
            expect(!preview->closed_set(root).has_value());
            expect(preview->closed_set(self_excited) == std::optional(sets.empty_set()));
            const auto empty = sets.empty_set();
            auto empty_instance = FailureInstantiation(
                terms,
                sets,
                {
                    .parameter = [=](GenericFailureParameter) noexcept -> ConstructionFailureRef {
                        return empty;
                    },
                    .member = {},
                    .origin = {},
                }
            );
            const auto empty_root = empty_instance.substitute(root);
            const auto first_only = sets.intern({first});
            auto active_instance = FailureInstantiation(
                terms,
                sets,
                {
                    .parameter = [=](GenericFailureParameter) noexcept -> ConstructionFailureRef {
                        return first_only;
                    },
                    .member = {},
                    .origin = {},
                }
            );
            const auto active_root = active_instance.substitute(root);
            const auto closed = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                names,
                AnalysisDiagnostics(diagnostics),
                false
            );
            if (!expect(closed.has_value())) {
                return;
            }
            expect(closed->failure_set(empty_root) == empty);
            expect(closed->failure_set(active_root) == sets.intern({first, second}));
            expect(
                preview->term_copy(root).guarded_inputs.front().gate == ConstructionFailureRef(root)
            );
            expect(diagnostics.empty());
        };

    "Semantic failures: conditional propagation survives an empty specialization"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            auto sources = SourceManager();
            const auto source = sources.append_virtual("generic-failure.cv", "?");
            require(source.has_value());
            auto provenance = CompilationProvenanceBuilder();
            const auto snapshot = provenance.intern_source_snapshot(sources.view(*source));
            const auto origin = provenance.append_origin({
                .value = ProgramSourceOrigin {.source_id = snapshot, .span = Span::at(0u)},
            });
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            auto identities =
                MutableProgramTable<std::uint8_t, GenericDeclarationID>(program.identity());
            const auto definition = identities.add(0u);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto parameter =
                terms.add_generic_parameter({.definition = definition, .index = 0u});
            const auto root = terms.add_union_term({parameter});
            terms.require_non_empty(root, origin, true);
            auto diagnostics = DiagnosticSink();
            const auto names = FailureTypeDiagnosticNames(
                types,
                declarations.construction_view(),
                provenance.reader()
            );
            const auto checked = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                names,
                AnalysisDiagnostics(diagnostics)
            );
            if (!expect(checked.has_value())) {
                return;
            }
            expect_equal(checked->conditional_non_empty().size(), 1uz);
            const auto empty = sets.empty_set();
            auto instance = FailureInstantiation(
                terms,
                sets,
                {
                    .parameter = [=](GenericFailureParameter) noexcept -> ConstructionFailureRef {
                        return empty;
                    },
                    .member = {},
                    .origin = {},
                }
            );
            const auto instance_root = instance.substitute(root);
            const auto specialized = solve_failure_constraints(
                terms.snapshot(),
                sets,
                provenance.reader(),
                names,
                AnalysisDiagnostics(diagnostics)
            );
            if (!expect(specialized.has_value())) {
                return;
            }
            expect(specialized->closed_set(instance_root) == std::optional(empty));
            expect(diagnostics.empty());
            auto invalid = FailureConstraintStore(program.identity(), provenance.identity());
            invalid.require_non_empty(sets.empty_set(), origin, true);
            const auto rejected = solve_failure_constraints(
                invalid.snapshot(),
                sets,
                provenance.reader(),
                names,
                AnalysisDiagnostics(diagnostics)
            );
            expect(!rejected.has_value());
            expect_diagnostic(diagnostics.values(), DiagnosticCode::EffectPropagateRedundant);
        };

    "Semantic failures: every failure position requires the completed nominal Copy capability"_test =
        [] static noexcept {
            const auto sources = std::array<std::string_view, 6> {
                R"(
                    fn declared() throw Failure {}
                    struct Failure { pending: AsyncOperation<i32> }
                )",
                R"(
                    struct Holder { pending: (AsyncOperation<i32>,) }
                    struct Failure { holder: Holder }
                    fn declared() throw Failure {}
                )",
                R"(
                    enum Failure { Pending(AsyncOperation<i32>), Empty }
                    fn declared() throw Failure {}
                )",
                R"(
                    struct Failure { pending: AsyncOperation<i32> }
                    fn consume(&&pending: AsyncOperation<void, throw Failure>) { let _ = &&pending; }
                )",
                R"(
                    struct Failure { pending: AsyncOperation<i32> }
                    async fn leaf() -> i32 => 7;
                    private fn emit() { throw Failure { pending: leaf() }; }
                )",
                R"(
                    struct Failure { pending: AsyncOperation<i32> }
                    fn handled() { try {} catch { Failure(_) => {}, } }
                )",
            };
            for (const auto source : sources) {
                expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::EffectThrowType
                );
            }
            static_cast<void>(analyze_test_program(R"(
                struct RecursiveFailure { callback: ptr<fn() -> void throw RecursiveFailure> }
                fn declared() throw RecursiveFailure {}
                struct EmptyFailure { absent: [AsyncOperation<i32>; 0] }
                fn empty() throw EmptyFailure {}
                struct NativeFailure { value: ::Opaque }
                fn native() throw NativeFailure {}
            )"));
        };
    "Semantic failures: guarded propagation reaches delayed inputs and "
    "filtered fixed points"_test = [] static noexcept {
        struct Scenario final {
            std::string_view name;
            bool source_first;
            bool seeded;
        };
        const auto scenarios = std::array {
            Scenario {.name = "source first", .source_first = true, .seeded = true},
            Scenario {.name = "gate first", .source_first = false, .seeded = true},
            Scenario {.name = "no surviving seeds", .source_first = false, .seeded = false},
        };
        const auto program = analyze_test_program("");
        each(
            scenarios,
            [](const Scenario& scenario) static noexcept { return scenario.name; },
            [&](const Scenario& scenario) noexcept {
                const auto provenance = CompilationProvenanceBuilder();
                const auto types = CanonicalTypeStoreBuilder(program.identity());
                const auto declarations =
                    DeclarationBuilder(program.identity(), provenance.identity());
                auto terms = FailureConstraintStore(program.identity(), provenance.identity());
                auto sets = FailureSetStoreBuilder(program.identity());
                const auto first = types.builtin_type(BuiltinType::I32);
                const auto second = types.builtin_type(BuiltinType::Bool);

                const auto target = terms.add_empty_term();
                const auto ready = terms.add_concrete_term(
                    scenario.seeded ? std::vector {first} : std::vector<TypeID> {}
                );
                const auto delayed = terms.add_concrete_term(
                    scenario.seeded ? std::vector {second} : std::vector<TypeID> {}
                );
                const auto first_hop = terms.add_union_term({delayed});
                const auto second_hop = terms.add_union_term({first_hop});
                terms.add_guarded_contribution(
                    target,
                    scenario.source_first ? second_hop : ready,
                    scenario.source_first ? ready : second_hop
                );
                terms.add_guarded_contribution(
                    target,
                    scenario.source_first ? second_hop : ready,
                    scenario.source_first ? ready : second_hop
                );
                terms.add_contribution(ready, ready);

                const auto cycle_left = terms.add_empty_term();
                const auto cycle_right = terms.add_empty_term();
                terms.equate(cycle_left, cycle_right);
                terms.add_guarded_contribution(cycle_left, cycle_right, cycle_left);
                const auto disabled = terms.add_empty_term();
                terms.add_guarded_contribution(disabled, cycle_left, ready);

                const auto excluded = terms.add_residual_term(ready, {first});
                terms.add_member(excluded, first);
                const auto retained = terms.add_intersection_term(ready, {second});
                terms.add_member(retained, first);
                const auto filtered = terms.add_union_term({excluded, retained});
                const auto residual_growth = terms.add_residual_term(ready, {first});
                terms.add_member(residual_growth, first);
                terms.add_contribution(residual_growth, second_hop);
                const auto retained_growth = terms.add_intersection_term(ready, {second});
                terms.add_member(retained_growth, first);
                terms.add_contribution(retained_growth, second_hop);

                auto diagnostics = DiagnosticSink();
                const auto solved = solve_failure_constraints(
                    std::move(terms).finish(),
                    sets,
                    provenance.reader(),
                    FailureTypeDiagnosticNames(
                        types,
                        declarations.construction_view(),
                        provenance.reader()
                    ),
                    AnalysisDiagnostics(diagnostics)
                );
                if (!expect_equal(solved.has_value(), true)) {
                    return;
                }
                expect_equal(diagnostics.empty(), true);
                const auto expected_target = scenario.seeded
                    ? std::vector {scenario.source_first ? first : second}
                    : std::vector<TypeID> {};
                expect(sets.copy(solved->failure_set(target)).members == expected_target)
                    .note("guarded contribution retains its source members");
                const auto expected_growth =
                    scenario.seeded ? std::vector {second} : std::vector<TypeID> {};
                for (const auto term : {first_hop, second_hop, residual_growth, retained_growth}) {
                    expect(sets.copy(solved->failure_set(term)).members == expected_growth)
                        .note("delayed propagation and filtering retain the second member");
                }
                const auto empty_set = sets.empty_set();
                for (const auto term :
                     {cycle_left, cycle_right, disabled, excluded, retained, filtered}) {
                    expect_equal(solved->contains(term), true);
                    expect(solved->failure_set(term) == empty_set)
                        .note("empty terms use the canonical empty failure set");
                    expect_equal(sets.copy(solved->failure_set(term)).members.empty(), true);
                }
            }
        );
    };

    "Semantic failures: known sets contribute without becoming inference variables"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto provenance = CompilationProvenanceBuilder();
            const auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto declarations = DeclarationBuilder(program.identity(), provenance.identity());
            const auto first = types.builtin_type(BuiltinType::I32);
            const auto second = types.builtin_type(BuiltinType::Bool);
            auto terms = FailureConstraintStore(program.identity(), provenance.identity());
            auto sets = FailureSetStoreBuilder(program.identity());
            const auto known = sets.intern({first, first});
            const auto empty_known = sets.empty_set();
            const auto inferred = terms.add_union_term({known, empty_known});
            terms.add_member(inferred, second);
            const auto enabled = terms.add_empty_term();
            const auto disabled = terms.add_empty_term();
            terms.add_guarded_contribution(enabled, known, known);
            terms.add_guarded_contribution(disabled, empty_known, known);
            const auto complete = sets.intern({first, second});
            const auto residual = terms.add_residual_term(complete, {first});
            const auto intersection = terms.add_intersection_term(complete, {first});
            expect(sets.copy(known).members == std::vector {first});
            expect(sets.copy(empty_known).members.empty());

            auto diagnostics = DiagnosticSink();
            const auto solved = solve_failure_constraints(
                std::move(terms).finish(),
                sets,
                provenance.reader(),
                FailureTypeDiagnosticNames(
                    types,
                    declarations.construction_view(),
                    provenance.reader()
                ),
                AnalysisDiagnostics(diagnostics)
            );
            if (!expect(solved.has_value())) {
                return;
            }
            expect(sets.copy(solved->failure_set(known)).members == std::vector {first});
            expect(sets.copy(solved->failure_set(empty_known)).members.empty());
            const auto inferred_members = sets.copy(solved->failure_set(inferred)).members;
            expect_equal(inferred_members.size(), 2uz);
            expect(std::ranges::find(inferred_members, first) != inferred_members.end());
            expect(std::ranges::find(inferred_members, second) != inferred_members.end());
            expect(sets.copy(solved->failure_set(enabled)).members == std::vector {first});
            expect(sets.copy(solved->failure_set(disabled)).members.empty());
            expect(sets.copy(solved->failure_set(residual)).members == std::vector {second});
            expect(sets.copy(solved->failure_set(intersection)).members == std::vector {first});
            expect(diagnostics.empty());
        };

    "Semantic effects: every branch contributes its failures whatever its condition"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Failure {}
            private fn fail() -> bool throw Failure { throw Failure {}; }
            private fn runtime_if() { let flag = false; if flag { fail()?; } }
            private fn runtime_logic() -> bool { let flag = false; return flag && fail()?; }
            private fn runtime_match() -> bool {
                let subject = true; return match subject { true => true, false => fail()?, };
            }
            private fn named_if() { const flag = false; if flag { fail()?; } }
            private fn named_logic() -> bool { const flag = false; return flag && fail()?; }
            private fn named_match() -> bool {
                const subject = true; return match subject { true => true, false => fail()?, };
            }
            private fn literal_if() { if false { fail()?; } }
            private fn literal_logic() -> bool => false && fail()?;
            private fn literal_while() { while false { fail()?; } }
            private fn literal_value() -> bool => if true { true } else { fail()? };
            private fn literal_match() -> bool => match true { true => true, false => fail()?, };
            private fn static_if() { const if false { fail()?; } }
        )");
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 13uz)) {
                return;
            }
            for (auto index = 0uz; index < callables.size(); ++index) {
                scenario(std::format("function {}", index), [&]() noexcept {
                    expect_equal(
                        test_callable_failures(program, callables[index]).members.size(),
                        1uz
                    );
                });
            }
            expect_diagnostic(
                analyze_test_errors("private fn invalid() { if false { let value: bool = 1; } }"),
                DiagnosticCode::TypeMismatch
            );
        };

    "Semantic control: callable final signatures own their effective failures"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "private fn inferred() {}\n"
                "fn published() {}\n"
                "struct Failure {}\n"
                "fn declared() throw Failure {}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 3uz)) {
                return;
            }
            expect(test_callable_failures(program, callables[0]).members.empty());
            expect(test_callable_failures(program, callables[1]).members.empty());
            expect_equal(test_callable_failures(program, callables[2]).members.size(), 1uz);
        };

    "Semantic effects: match bounds and coverage follow patterns, not the subject value"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Failure {}
        fn fail() -> i32 throw Failure { throw Failure {}; }
        private fn bounded() -> i32 {
            return match 2 { 3..fail()? => 1, _ => 7, };
        }
        fn declared() -> i32 throw Failure => match true { true => 7, false => 0, };
    )");
            const auto callables = test_function_callables(program);
            if (!expect(callables.size() == 3uz)) {
                return;
            }
            expect(test_callable_failures(program, callables[1]).members.size() == 1uz);
            expect(test_callable_failures(program, callables[2]).members.size() == 1uz);
            expect_diagnostic(
                analyze_test_errors("fn invalid() -> i32 => match true { true => 7, };"),
                DiagnosticCode::MatchNonExhaustive
            );
            expect_diagnostic(
                analyze_test_errors(
                    "fn invalid() -> i32 => match true { true => 7, false => false, };"
                ),
                DiagnosticCode::TypeMismatch
            );
        };

    "Semantic failures: implicit entry infers the escaping failure set"_test = [] static noexcept {
        const auto program = analyze_test_program(
            "struct First {} struct Second {}\n"
            "private fn later() throw First + Second { throw First {}; }\n"
            "later()?;\n"
            "throw Second {};\n"
        );
        const auto callables = test_function_callables(program);
        if (!expect_equal(callables.size(), 2uz)) {
            return;
        }
        expect_equal(test_callable_failures(program, callables.back()).members.size(), 2uz);

        const auto handled = analyze_test_program(
            "struct Failure {}\n"
            "private fn later() throw Failure { throw Failure {}; }\n"
            "try { later()?; } catch { Failure(_) => {}, }\n"
        );
        const auto handled_callables = test_function_callables(handled);
        if (!expect_equal(handled_callables.size(), 2uz)) {
            return;
        }
        expect(test_callable_failures(handled, handled_callables.back()).members.empty());
    };

    "Semantic failures: canonical set identity is independent of declaration order"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct AlphaFailure {}\n"
                "struct BetaFailure {}\n"
                "private fn alpha_first() throw AlphaFailure + BetaFailure {}\n"
                "private fn beta_first() throw BetaFailure + AlphaFailure {}\n"
                "private fn handled() {\n"
                "    try { alpha_first()?; } catch { AlphaFailure(_) => {}, BetaFailure(_) => {}, }\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_greater_equal(callables.size(), 2uz)) {
                return;
            }
            expect(((test_callable_signature(program, callables[0]).failures)
                    == (test_callable_signature(program, callables[1]).failures)))
                .note(
                    "test_callable_signature(program, callables[0]).failures == test_callable_signature(program, callables[1]).failures"
                );

            expect(test_callable_failures(program, callables.back()).members.empty());
        };

    "Semantic control: direct and mutually recursive inference reach one fixed point"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct Failure {}\n"
                "private fn direct() { throw Failure {}; }\n"
                "private fn left(stop: bool) -> void {\n"
                "    if stop { throw Failure {}; }\n"
                "    right(true)?;\n"
                "}\n"
                "private fn right(stop: bool) -> void {\n"
                "    if stop { left(true)?; }\n"
                "}\n"
                "fn recover() {\n"
                "    try { direct()?; left(false)?; } catch { Failure(_) => {}, }\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 4uz)) {
                return;
            }
            for (auto index = 0uz; index < 3; ++index) {
                expect_equal(test_callable_failures(program, callables[index]).members.size(), 1uz);
            }
            expect(((test_callable_signature(program, callables[0]).failures)
                    == (test_callable_signature(program, callables[1]).failures)))
                .note(
                    "test_callable_signature(program, callables[0]).failures == test_callable_signature(program, callables[1]).failures"
                );
            expect(((test_callable_signature(program, callables[1]).failures)
                    == (test_callable_signature(program, callables[2]).failures)))
                .note(
                    "test_callable_signature(program, callables[1]).failures == test_callable_signature(program, callables[2]).failures"
                );
            expect(test_callable_failures(program, callables[3]).members.empty());
        };

    "Semantic control: fixed contracts are dependency boundaries"_test = [] static noexcept {
        const auto program = analyze_test_program(
            "struct DeclaredFailure {}\n"
            "struct BodyFailure {}\n"
            "private fn source() { throw BodyFailure {}; }\n"
            "private fn fixed(fail: bool) throw DeclaredFailure {\n"
            "    if fail { throw DeclaredFailure {}; }\n"
            "    try { source()?; } catch { BodyFailure(_) => {}, }\n"
            "}\n"
            "private fn caller() { fixed(true)?; }\n"
        );
        const auto callables = test_function_callables(program);
        if (!expect_equal(callables.size(), 3uz)) {
            return;
        }
        const auto fixed_failures = test_callable_failures(program, callables[1]).members;
        const auto caller_failures = test_callable_failures(program, callables[2]).members;
        if (!expect_equal(fixed_failures.size(), 1uz)) {
            return;
        }
        expect(((caller_failures) == (fixed_failures))).note("caller_failures == fixed_failures");
    };

    "Semantic failures: composite expressions retain every pending invocation"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct FirstFailure {}\n"
                "struct SecondFailure {}\n"
                "struct Pair { left: i32, right: i32 }\n"
                "private fn first() -> i32 throw FirstFailure { throw FirstFailure {}; }\n"
                "private fn second() -> i32 throw SecondFailure { throw SecondFailure {}; }\n"
                "private fn sum(pair: Pair) -> i32 { return pair.left + pair.right; }\n"
                "private fn binary() -> i32 throw FirstFailure + SecondFailure {\n"
                "    return (first() + second())?;\n"
                "}\n"
                "private fn aggregate() -> i32 throw FirstFailure + SecondFailure {\n"
                "    return sum({ left: first(), right: second() })?;\n"
                "}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 5uz)) {
                return;
            }
            expect_equal(test_callable_failures(program, callables[3]).members.size(), 2uz);
            expect_equal(test_callable_failures(program, callables[4]).members.size(), 2uz);
        };

    "Semantic effects: known function calls use the target contract through view widening"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Failure {}
        fn plain() -> i32 => 7;
        fn known() -> i32 {
            let callback: fn() -> i32 throw Failure = plain;
            let copy = callback;
            return copy();
        }
        fn declared() -> i32 throw Failure => 7;
        fn known_declared() -> i32 throw Failure {
            let callback: fn() -> i32 throw Failure = declared;
            return callback()?;
        }
    )");
            const auto callables = test_function_callables(program);
            if (!expect(callables.size() == 4uz)) {
                return;
            }
            const auto& signature = program.callable_signatures().signature(
                program.declarations().callable(callables[1]).signature
            );
            expect(program.failure_sets().failure_set(signature.failures).members.empty());
            const auto redundant = analyze_test_errors(R"(
        struct Failure {}
        fn plain() -> i32 => 7;
        fn invalid() -> i32 {
            let callback: fn() -> i32 throw Failure = plain;
            return callback()?;
        }
    )");
            expect_diagnostic(redundant, DiagnosticCode::EffectPropagateRedundant);
        };
});

} // namespace
