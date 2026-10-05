module carven:test.internal.semantic.semir.type;

import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Construction types: children must already exist in the owning store"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            const auto foreign = analyze_test_program("");
            auto types = ConstructionTypeStore(program.identity());
            auto references = MutableProgramTable<int, TypeTermID>(program.identity());
            const auto self = references.add(0);
            static_cast<void>(references.add(0));
            const auto future = references.add(0);
            auto foreign_references = MutableProgramTable<int, TypeTermID>(foreign.identity());
            const auto foreign_child = foreign_references.add(0);
            auto failures = MutableProgramTable<int, FailureTermID>(program.identity());
            const auto failure = failures.add(0);
            const auto canonical = CanonicalTypeStoreBuilder(program.identity());
            const auto integer = canonical.builtin_type(BuiltinType::I32);

            expect(expect_termination("type-array-child-must-exist", [&] noexcept {
                static_cast<void>(types.append(
                    {.value = ConstructionArrayTypeValue {.element = self, .extent = 1u}}
                ));
            }));

            expect(expect_termination("type-parameter-child-must-exist", [&] noexcept {
                static_cast<void>(types.append(
                    {.value = ConstructionCallableViewTypeValue {
                         .parameters =
                             {{.stage = ParameterStage::Runtime,
                               .access = AccessMode::Read,
                               .type = future}},
                         .result = integer,
                         .failures = failure
                     }}
                ));
            }));
            expect(expect_termination("type-result-child-must-share-owner", [&] noexcept {
                static_cast<void>(types.append(
                    {.value = ConstructionCallableViewTypeValue {
                         .parameters = {},
                         .result = foreign_child,
                         .failures = failure
                     }}
                ));
            }));
        };

    "Construction types: nested callable shapes retain canonical contracts"_test = [] static noexcept {
        const auto program = analyze_test_program(
            "struct Failure {}\n"
            "fn first(values: [fn(&i32) -> i32 throw Failure; 2]) {}\n"
            "fn second(values: [fn(&i32) -> i32 throw Failure; 2]) {}\n"
        );
        const auto callables = test_function_callables(program);
        if (!expect_equal(callables.size(), 2uz)) {
            return;
        }
        const auto first = test_callable_signature(program, callables[0]);
        const auto second = test_callable_signature(program, callables[1]);
        if (!expect_equal(first.parameters.size(), 1uz)) {
            return;
        }
        if (!expect_equal(second.parameters.size(), 1uz)) {
            return;
        }
        expect(((first.parameters.front().type) == (second.parameters.front().type)))
            .note("first.parameters.front().type == second.parameters.front().type");
        const auto& array_type = program.types().type(first.parameters.front().type);
        const auto* array = std::get_if<ArrayTypeValue>(&array_type.value);
        if (!expect(array != nullptr)) {
            return;
        }
        expect_equal(array->extent, 2u);
        const auto* view =
            std::get_if<CallableViewTypeValue>(&program.types().type(array->element).value);
        if (!expect(view != nullptr)) {
            return;
        }
        const auto& contract = program.callable_signatures().signature(view->signature);
        if (!expect_equal(contract.parameters.size(), 1uz)) {
            return;
        }
        expect_equal(contract.parameters.front().access, AccessMode::Write);
        expect(((contract.parameters.front().type) == (contract.result)))
            .note("contract.parameters.front().type == contract.result");
        expect(((program.types().type(contract.result).value)
                == (CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::I32}})))
            .note(
                "program.types().type(contract.result).value == CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::I32}}"
            );
        expect_equal(program.failure_sets().failure_set(contract.failures).members.size(), 1uz);
    };

    "External types: C string storage has a closed operand and type contract"_test =
        [] static noexcept {
            const auto type = CppTypeValue {.form = CppConstCharPointerType {}};
            expect(valid_cpp_type(type));
            expect(cpp_type_references(type).empty());
            expect(cpp_type_name(type) == nullptr);
            expect(valid_cstring_bytes("abc"));
            expect(valid_cstring_bytes(""));
            expect(!(valid_cstring_bytes(std::string("a\0b", 3))));
            expect(!(valid_cstring_bytes("\xff")));
        };

    "External types: query operand access participates in canonical identity"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            auto types = CanonicalTypeStoreBuilder(program.identity());
            const auto integer = types.builtin_type(BuiltinType::I32);
            const auto pointer = types.intern(
                {.value = PointerTypeValue {.target = integer, .access = PointerAccess::Read}}
            );
            const auto query = [&](AccessMode access) noexcept {
                return CanonicalType {
                    .value = CppTypeValue {
                        .form = CppQueryType {
                            .expression = CppIndexQuery {
                                .receiver = {.type = pointer, .access = access},
                                .index = {.type = integer, .access = AccessMode::Read},
                            },
                        },
                    },
                };
            };
            const auto read = types.intern(query(AccessMode::Read));
            const auto write = types.intern(query(AccessMode::Write));
            const auto take = types.intern(query(AccessMode::Take));
            expect(read != write).note("read and write type IDs differ");
            expect(read != take).note("read and take type IDs differ");
            expect(write != take).note("write and take type IDs differ");
            expect(((types.intern(query(AccessMode::Read))) == (read)))
                .note("types.intern(query(AccessMode::Read)) == read");
            expect(((types.intern(query(AccessMode::Write))) == (write)))
                .note("types.intern(query(AccessMode::Write)) == write");
            expect(((types.intern(query(AccessMode::Take))) == (take)))
                .note("types.intern(query(AccessMode::Take)) == take");
        };

    "Pointer types: nested declared callable contracts have stable identities"_test =
        [] static noexcept {
            const auto program = analyze_test_program(
                "struct Failure {}\n"
                "fn first(p: ptr<ptr<&fn(&i32) -> i32 throw Failure>>) {}\n"
                "fn second(p: ptr<ptr<&fn(&i32) -> i32 throw Failure>>) {}\n"
                "fn writer(p: ptr<&ptr<&fn(&i32) -> i32 throw Failure>>) {}\n"
            );
            const auto callables = test_function_callables(program);
            if (!expect_equal(callables.size(), 3uz)) {
                return;
            }
            const auto first =
                test_callable_signature(program, callables[0]).parameters.front().type;
            const auto second =
                test_callable_signature(program, callables[1]).parameters.front().type;
            const auto writer =
                test_callable_signature(program, callables[2]).parameters.front().type;
            expect(((first) == (second))).note("first == second");
            expect(first != writer).note("read and write callable IDs differ");
            const auto* reader_type =
                std::get_if<PointerTypeValue>(&program.types().type(first).value);
            const auto* writer_type =
                std::get_if<PointerTypeValue>(&program.types().type(writer).value);
            if (!expect(reader_type != nullptr)) {
                return;
            }
            if (!expect(writer_type != nullptr)) {
                return;
            }
            expect_equal(reader_type->access, PointerAccess::Read);
            expect_equal(writer_type->access, PointerAccess::Write);
            expect(((reader_type->target) == (writer_type->target)))
                .note("reader_type->target == writer_type->target");
            const auto* inner =
                std::get_if<PointerTypeValue>(&program.types().type(reader_type->target).value);
            if (!expect(inner != nullptr)) {
                return;
            }
            expect_equal(inner->access, PointerAccess::Write);
            expect(
                std::holds_alternative<CallableViewTypeValue>(
                    program.types().type(inner->target).value
                )
            );
        };

    "Canonical types: builtin queries are complete and stable across publication"_test =
        [] static noexcept {
            const auto program = analyze_test_program("");
            auto builder = CanonicalTypeStoreBuilder(program.identity());
            const auto& reader = builder;
            auto identities = std::vector<TypeID>();
            for (const auto kind : builtin_types) {
                const auto id = reader.builtin_type(kind);
                expect(
                    reader.copy(id).value == CanonicalTypeValue {BuiltinTypeValue {.kind = kind}}
                );
                expect(
                    builder.intern(CanonicalType {.value = BuiltinTypeValue {.kind = kind}}) == id
                );
                identities.push_back(id);
            }
            const auto compound = builder.intern(
                CanonicalType {
                    .value = ArrayTypeValue {
                        .element = reader.builtin_type(BuiltinType::I32),
                        .extent = 3u,
                    }
                }
            );
            const auto store = std::move(builder).seal();
            expect(store.size() == builtin_types.size() + 1uz);
            expect(store.contains(compound));
            for (auto index = 0uz; index < builtin_types.size(); ++index) {
                expect(store.builtin_type(builtin_types[index]) == identities[index]);
            }
        };

    "Type contents: cyclic slice graphs reach an order-independent fixed point"_test =
        [] static noexcept {
            const auto program = analyze_test_program("struct A { seed: i32 } struct B {}\n");

            struct FailureReader final {
                ProgramIdentity identity;
                FailureTermID term;
                FailureSetID set;

                auto owner() const noexcept -> ProgramIdentity { return identity; }

                auto contains(FailureTermID id) const noexcept -> bool { return id == term; }

                auto failure_set(FailureTermID id) const noexcept -> FailureSetID {
                    require(id == term);
                    return set;
                }
            };

            struct Scenario final {
                bool seeded;
                bool reverse;
            };

            const auto scenarios = std::to_array<Scenario>({
                {.seeded = false, .reverse = false},
                {.seeded = true, .reverse = false},
                {.seeded = true, .reverse = true},
            });
            each(
                scenarios,
                [](const Scenario& scenario) static noexcept {
                    return std::format("seeded {} reverse {}", scenario.seeded, scenario.reverse);
                },
                [&](const auto& scenario) noexcept {
                    const auto& [seeded, reverse] = scenario;
                    auto types = CanonicalTypeStoreBuilder(program.identity());
                    auto declarations =
                        DeclarationBuilder(program.identity(), program.provenance().identity());
                    const auto module_id = declarations.reserve_module();
                    const auto first = declarations.reserve_struct();
                    const auto second = declarations.reserve_struct();
                    const auto enumeration = declarations.reserve_enum();
                    const auto enum_case = declarations.reserve_enum_case();
                    const auto& source = program.declarations().structure(first);
                    auto nominal = std::vector<TypeID>();
                    for (const auto id : {first, second}) {
                        nominal.push_back(
                            types.intern({.value = StructTypeValue {.structure = id}})
                        );
                    }
                    auto slices = std::vector<TypeID>();
                    for (const auto type : nominal) {
                        slices.push_back(types.intern({.value = SliceTypeValue {.element = type}}));
                    }
                    auto failure_terms =
                        MutableProgramTable<int, FailureTermID>(program.identity());
                    auto failure_sets = FailureSetStoreBuilder(program.identity());
                    const auto reader = FailureReader {
                        .identity = program.identity(),
                        .term = failure_terms.add(0),
                        .set = failure_sets.intern({}),
                    };
                    auto signatures = CallableSignatureStoreBuilder(program.identity());
                    auto construction = ConstructionTypeStore(program.identity());
                    const auto view_term = construction.append(
                        {.value = ConstructionCallableViewTypeValue {
                             .parameters = {},
                             .result = types.builtin_type(BuiltinType::I32),
                             .failures = reader.term,
                         }}
                    );
                    const auto resolution =
                        std::move(construction).canonicalize(reader, types, signatures);
                    const auto view = resolution.resolve(view_term);
                    const auto pointer = types.intern(
                        {.value = PointerTypeValue {
                             .target = nominal.front(),
                             .access = PointerAccess::Read,
                         }}
                    );
                    const auto owner = types.intern(
                        {.value = ArrayTypeValue {
                             .element = view,
                             .extent = 1u,
                         }}
                    );
                    const auto native =
                        types.intern({.value = CppTypeValue {.form = CppConstCharPointerType {}}});
                    const auto native_array =
                        types.intern({.value = ArrayTypeValue {.element = native, .extent = 1u}});
                    const auto native_pointer = types.intern(
                        {.value = PointerTypeValue {
                             .target = native,
                             .access = PointerAccess::Read,
                         }}
                    );
                    const auto native_slice =
                        types.intern({.value = SliceTypeValue {.element = native}});
                    for (auto index = 0uz; index < nominal.size(); ++index) {
                        auto fields = std::vector<ConstructionStructField> {
                            {.name = source.name,
                             .type = slices[1uz - index],
                             .origin = source.origin},
                        };
                        if (index == 0uz && seeded) {
                            fields.push_back(
                                {.name = source.fields.front().name,
                                 .type = owner,
                                 .origin = source.origin}
                            );
                            fields.push_back(
                                {.name = program.declarations().structure(second).name,
                                 .type = native,
                                 .origin = source.origin}
                            );
                        }
                        if (reverse) {
                            std::ranges::reverse(fields);
                        }
                        const auto id = index == 0uz ? first : second;
                        declarations.define(
                            id,
                            ConstructionStructDeclaration {
                                .kind = RecordKind::Struct,
                                .module_id = module_id,
                                .name = program.declarations().structure(id).name,
                                .origin = source.origin,
                                .visibility = source.visibility,
                                .fields = std::move(fields),
                            }
                        );
                    }
                    declarations.define(
                        enum_case,
                        ConstructionEnumCaseDeclaration {
                            .owner = enumeration,
                            .name = source.fields.front().name,
                            .origin = source.origin,
                            .payload_types = {nominal.front()},
                            .constant = std::nullopt,
                        }
                    );
                    declarations.define(
                        enumeration,
                        EnumDeclaration {
                            .module_id = module_id,
                            .name = source.name,
                            .origin = source.origin,
                            .visibility = source.visibility,
                            .representation = PayloadEnumRepresentation {},
                            .cases = {enum_case},
                            .supports_equality = false,
                        }
                    );
                    const auto enum_type =
                        types.intern({.value = EnumTypeValue {.enumeration = enumeration}});
                    declarations.define(module_id, program.declarations().module_decl(module_id));
                    static_cast<void>(declarations.finish_heads());
                    for (const auto type : nominal) {
                        expect((query_type_contents(types, declarations.construction_view(), type)
                                    .contains_callable_view
                                == seeded))
                            .note(
                                "query_type_contents(types, declarations.construction_view(), type)\n                            .contains_callable_vie...",
                                "seeded = ",
                                seeded,
                                "reverse = ",
                                reverse
                            );
                    }
                    declarations.finish_callable_signatures();
                    const auto closed_declarations = std::move(declarations).seal(resolution);
                    const auto closed_types = std::move(types).seal();
                    const auto contents = compute_type_contents(closed_types, closed_declarations);
                    for (const auto type : {nominal[0], nominal[1], slices[0], slices[1]}) {
                        expect(((contents[type.index()].contains_callable_view) == (seeded)))
                            .note(
                                "contents[type.index()].contains_callable_view == seeded",
                                "seeded = ",
                                seeded,
                                "reverse = ",
                                reverse
                            );
                        expect(!(contents[type.index()].contains_closure_owner))
                            .note("seeded = ", seeded, "reverse = ", reverse);
                    }
                    expect(contents[native.index()].contains_native_value)
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(contents[native_array.index()].contains_native_value)
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(((contents[nominal[0].index()].contains_native_value) == (seeded)))
                        .note(
                            "contents[nominal[0].index()].contains_native_value == seeded",
                            "seeded = ",
                            seeded,
                            "reverse = ",
                            reverse
                        );
                    expect(!(contents[nominal[1].index()].contains_native_value))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(((contents[enum_type.index()].contains_native_value) == (seeded)))
                        .note(
                            "contents[enum_type.index()].contains_native_value == seeded",
                            "seeded = ",
                            seeded,
                            "reverse = ",
                            reverse
                        );
                    expect(!(contents[native_pointer.index()].contains_native_value))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[native_slice.index()].contains_native_value))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[pointer.index()].contains_native_value))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[slices[0].index()].contains_native_value))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[slices[1].index()].contains_native_value))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(((contents[nominal[0].index()].contains_storage_owner) == (seeded)))
                        .note(
                            "contents[nominal[0].index()].contains_storage_owner == seeded",
                            "seeded = ",
                            seeded,
                            "reverse = ",
                            reverse
                        );
                    expect(!(contents[nominal[1].index()].contains_storage_owner))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[slices[0].index()].contains_storage_owner))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[slices[1].index()].contains_storage_owner))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[pointer.index()].contains_callable_view))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                    expect(!(contents[pointer.index()].contains_storage_owner))
                        .note("seeded = ", seeded, "reverse = ", reverse);
                }
            );
        };
});

} // namespace
