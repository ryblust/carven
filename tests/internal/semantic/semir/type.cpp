module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.type;

import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.table;
import :semantic.semir.type;
import :test.internal.harness.death;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Construction types: children must already exist in the owning store") {
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

    CHECK(expect_termination("type-array-child-must-exist", [&] noexcept {
        static_cast<void>(
            types.append({.value = ConstructionArrayTypeValue {.element = self, .extent = 1u}})
        );
    }));

    CHECK(expect_termination("type-parameter-child-must-exist", [&] noexcept {
        static_cast<void>(types.append(
            {.value = ConstructionCallableViewTypeValue {
                 .parameters = {{.access = AccessMode::Read, .type = future}},
                 .result = integer,
                 .failures = failure
             }}
        ));
    }));
    CHECK(expect_termination("type-result-child-must-share-owner", [&] noexcept {
        static_cast<void>(types.append(
            {.value = ConstructionCallableViewTypeValue {
                 .parameters = {},
                 .result = foreign_child,
                 .failures = failure
             }}
        ));
    }));
}

TEST_CASE("Construction types: nested callable shapes retain canonical contracts") {
    const auto program = analyze_test_program(
        "struct Failure {}\n"
        "fn first(values: [fn(&i32) -> i32 throw Failure; 2]) {}\n"
        "fn second(values: [fn(&i32) -> i32 throw Failure; 2]) {}\n"
    );
    const auto callables = test_function_callables(program);
    REQUIRE_EQ(callables.size(), 2uz);
    const auto first = test_callable_signature(program, callables[0]);
    const auto second = test_callable_signature(program, callables[1]);
    REQUIRE_EQ(first.parameters.size(), 1uz);
    REQUIRE_EQ(second.parameters.size(), 1uz);
    CHECK_EQ(first.parameters.front().type, second.parameters.front().type);
    const auto& array_type = program.types().type(first.parameters.front().type);
    const auto* array = std::get_if<ArrayTypeValue>(&array_type.value);
    REQUIRE(array != nullptr);
    CHECK_EQ(array->extent, 2u);
    const auto* view =
        std::get_if<CallableViewTypeValue>(&program.types().type(array->element).value);
    REQUIRE(view != nullptr);
    const auto& contract = program.callable_signatures().signature(view->signature);
    REQUIRE_EQ(contract.parameters.size(), 1uz);
    CHECK_EQ(contract.parameters.front().access, AccessMode::Write);
    CHECK_EQ(contract.parameters.front().type, contract.result);
    CHECK_EQ(
        program.types().type(contract.result).value,
        CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::I32}}
    );
    CHECK_EQ(program.failure_sets().failure_set(contract.failures).members.size(), 1uz);
}

TEST_CASE("External types: C string storage has a closed operand and type contract") {
    const auto type = CppTypeValue {.form = CppConstCharPointerType {}};
    CHECK(valid_cpp_type(type));
    CHECK(cpp_type_references(type).empty());
    CHECK(cpp_type_name(type) == nullptr);
    CHECK(cpp_operation_accepts_arity(CppCStringOperation {.bytes = "abc"}, 0uz));
    CHECK_FALSE(cpp_operation_accepts_arity(CppCStringOperation {.bytes = "abc"}, 1uz));
    CHECK_FALSE(
        cpp_operation_accepts_arity(CppCStringOperation {.bytes = std::string("a\0b", 3)}, 0uz)
    );
    CHECK_FALSE(cpp_operation_accepts_arity(CppCStringOperation {.bytes = "\xff"}, 0uz));
}

TEST_CASE("External types: query operand access participates in canonical identity") {
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
    CHECK_NE(read, write);
    CHECK_NE(read, take);
    CHECK_NE(write, take);
    CHECK_EQ(types.intern(query(AccessMode::Read)), read);
    CHECK_EQ(types.intern(query(AccessMode::Write)), write);
    CHECK_EQ(types.intern(query(AccessMode::Take)), take);
}

TEST_CASE("Pointer types: nested declared callable contracts have stable identities") {
    const auto program = analyze_test_program(
        "struct Failure {}\n"
        "fn first(p: ptr<ptr<&fn(&i32) -> i32 throw Failure>>) {}\n"
        "fn second(p: ptr<ptr<&fn(&i32) -> i32 throw Failure>>) {}\n"
        "fn writer(p: ptr<&ptr<&fn(&i32) -> i32 throw Failure>>) {}\n"
    );
    const auto callables = test_function_callables(program);
    REQUIRE_EQ(callables.size(), 3uz);
    const auto first = test_callable_signature(program, callables[0]).parameters.front().type;
    const auto second = test_callable_signature(program, callables[1]).parameters.front().type;
    const auto writer = test_callable_signature(program, callables[2]).parameters.front().type;
    CHECK_EQ(first, second);
    CHECK_NE(first, writer);
    const auto* reader_type = std::get_if<PointerTypeValue>(&program.types().type(first).value);
    const auto* writer_type = std::get_if<PointerTypeValue>(&program.types().type(writer).value);
    REQUIRE(reader_type != nullptr);
    REQUIRE(writer_type != nullptr);
    CHECK_EQ(reader_type->access, PointerAccess::Read);
    CHECK_EQ(writer_type->access, PointerAccess::Write);
    CHECK_EQ(reader_type->target, writer_type->target);
    const auto* inner =
        std::get_if<PointerTypeValue>(&program.types().type(reader_type->target).value);
    REQUIRE(inner != nullptr);
    CHECK_EQ(inner->access, PointerAccess::Write);
    CHECK(std::holds_alternative<CallableViewTypeValue>(program.types().type(inner->target).value));
}

TEST_CASE("Canonical types: builtin queries are complete and stable across publication") {
    const auto program = analyze_test_program("");
    auto builder = CanonicalTypeStoreBuilder(program.identity());
    const auto& reader = builder;
    auto identities = std::vector<TypeID>();
    for (const auto kind : builtin_types) {
        const auto id = reader.builtin_type(kind);
        CHECK(reader.copy(id).value == CanonicalTypeValue {BuiltinTypeValue {.kind = kind}});
        CHECK(builder.intern(CanonicalType {.value = BuiltinTypeValue {.kind = kind}}) == id);
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
    CHECK(store.size() == builtin_types.size() + 1uz);
    CHECK(store.contains(compound));
    for (auto index = 0uz; index < builtin_types.size(); ++index) {
        CHECK(store.builtin_type(builtin_types[index]) == identities[index]);
    }
}

TEST_CASE("Type contents: cyclic slice graphs reach an order-independent fixed point") {
    const auto program = analyze_test_program("struct A { seed: i32 } struct B {}\n");

    struct FailureReader final {
        ProgramIdentity identity;
        FailureTermID term;
        FailureSetID set;

        auto owner() const noexcept -> ProgramIdentity { return identity; }

        auto contains(FailureTermID id) const noexcept -> bool { return id == term; }

        auto failure_set(FailureTermID id) const noexcept -> FailureSetID {
            REQUIRE(id == term);
            return set;
        }
    };

    for (const auto seeded : {false, true}) {
        for (const auto reverse : {false, true}) {
            CAPTURE(seeded);
            CAPTURE(reverse);
            auto types = CanonicalTypeStoreBuilder(program.identity());
            auto declarations =
                DeclarationBuilder(program.identity(), program.provenance().identity());
            const auto module = declarations.reserve_module();
            const auto first = declarations.reserve_struct();
            const auto second = declarations.reserve_struct();
            const auto& source = program.declarations().structure(first);
            auto nominal = std::vector<TypeID>();
            for (const auto id : {first, second}) {
                nominal.push_back(types.intern({.value = StructTypeValue {.structure = id}}));
            }
            auto slices = std::vector<TypeID>();
            for (const auto type : nominal) {
                slices.push_back(types.intern({.value = SliceTypeValue {.element = type}}));
            }
            auto failure_terms = MutableProgramTable<int, FailureTermID>(program.identity());
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
            const auto resolution = std::move(construction).canonicalize(reader, types, signatures);
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
            for (auto index = 0uz; index < nominal.size(); ++index) {
                auto fields = std::vector<ConstructionStructField> {
                    {.name = source.name, .type = slices[1uz - index], .origin = source.origin},
                };
                if (index == 0uz && seeded) {
                    fields.push_back(
                        {.name = source.fields.front().name, .type = owner, .origin = source.origin}
                    );
                }
                if (reverse) {
                    std::ranges::reverse(fields);
                }
                const auto id = index == 0uz ? first : second;
                declarations.define(
                    id,
                    ConstructionStructDeclaration {
                        .module_id = module,
                        .name = program.declarations().structure(id).name,
                        .origin = source.origin,
                        .visibility = source.visibility,
                        .fields = std::move(fields),
                        .capabilities = {.equality = false},
                    }
                );
            }
            declarations.define(module, program.declarations().module_decl(module));
            static_cast<void>(declarations.finish_heads());
            for (const auto type : nominal) {
                CHECK_EQ(
                    query_type_contents(types, declarations.construction_view(), type)
                        .callable_view,
                    seeded
                );
            }
            declarations.finish_callable_signatures();
            const auto closed_declarations = std::move(declarations).seal(resolution);
            const auto closed_types = std::move(types).seal();
            const auto contents = compute_type_contents(closed_types, closed_declarations);
            for (const auto type : {nominal[0], nominal[1], slices[0], slices[1]}) {
                CHECK_EQ(contents[type.index()].callable_view, seeded);
                CHECK_FALSE(contents[type.index()].closure_owner);
            }
            CHECK_EQ(contents[nominal[0].index()].storage_owner, seeded);
            CHECK_FALSE(contents[nominal[1].index()].storage_owner);
            CHECK_FALSE(contents[slices[0].index()].storage_owner);
            CHECK_FALSE(contents[slices[1].index()].storage_owner);
            CHECK_FALSE(contents[pointer.index()].callable_view);
            CHECK_FALSE(contents[pointer.index()].storage_owner);
        }
    }
}
