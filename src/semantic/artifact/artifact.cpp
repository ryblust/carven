module carven:semantic.artifact.impl;

import :semantic.analysis.catalog;
import :semantic.analysis.failure;
import :semantic.analysis.program;
import :semantic.artifact;
import :semantic.semir.async;
import :semantic.semir.body;
import :semantic.semir.completion;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.generic;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.passing;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.table;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.document;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :source.provenance.ids;
import :source.text;
import :support.binary;
import :support.typed_id;
import std;

struct SemIRProgramArtifactState final {
    ProgramIdentity program_identity;
    CompilationProvenance compilation_provenance;
    CanonicalTypeStore type_store;
    ConstantStore constant_store;
    FailureSetStore failure_set_store;
    CallableSignatureStore callable_signature_store;
    DeclarationStore declaration_store;
    BodyStore body_store;
    TestStore test_store;
    std::vector<StaticInstance> static_instance_store;
    ImmutableProgramTable<GenericDeclarationContract, GenericDeclarationID>
        generic_declaration_store;
    std::vector<GenericNominalInstance> generic_instance_store;
    std::vector<GenericFunctionInstance> generic_function_store;
    std::map<CallableID, std::size_t> generic_instance_functions;
    std::map<NominalDeclarationRef, std::size_t> generic_instance_nominals;
    std::map<std::pair<FunctionID, std::vector<ConstantID>>, CallableID> static_instance_index;
    std::map<CallableID, std::size_t> instance_callables;
    std::vector<bool> executed_bodies;
    std::vector<bool> test_stops;
    std::unique_ptr<const AsyncCancellationFacts> cancellation_facts;
    std::vector<TypeContents> contents;
    std::vector<CallableSurface> callable_surfaces;
    ReadPassingFacts read_passing_facts;
};

SemIRProgram::SemIRProgram(SemIRProgramArtifactState state) noexcept
    : program_identity(state.program_identity),
      compilation_provenance(std::move(state.compilation_provenance)),
      type_store(std::move(state.type_store)),
      constant_store(std::move(state.constant_store)),
      failure_set_store(std::move(state.failure_set_store)),
      callable_signature_store(std::move(state.callable_signature_store)),
      declaration_store(std::move(state.declaration_store)),
      body_store(std::move(state.body_store)),
      test_store(std::move(state.test_store)),
      static_instance_store(std::move(state.static_instance_store)),
      generic_declaration_store(std::move(state.generic_declaration_store)),
      generic_instance_store(std::move(state.generic_instance_store)),
      generic_function_store(std::move(state.generic_function_store)),
      generic_instance_functions(std::move(state.generic_instance_functions)),
      generic_instance_nominals(std::move(state.generic_instance_nominals)),
      static_instance_index(std::move(state.static_instance_index)),
      instance_callables(std::move(state.instance_callables)),
      executed_bodies(std::move(state.executed_bodies)),
      test_stops(std::move(state.test_stops)),
      cancellation_facts(std::move(state.cancellation_facts)),
      contents(std::move(state.contents)),
      callable_surfaces(std::move(state.callable_surfaces)),
      read_passing_facts(std::move(state.read_passing_facts)) {}

enum class SemanticArtifactTable : std::uint8_t {
    Modules,
    Functions,
    Structs,
    Enums,
    EnumCases,
    ModuleConstants,
    Callables,
    Bodies,
    Tests,
    Types,
    Constants,
    FailureSets,
    Signatures,
    GenericDeclarations,
    GenericTypes,
    TypeTerms,
    FailureTerms,
    GenericFailures,
    Sources,
    ProvenanceModules,
    Spellings,
    Origins,
    Count,
};

using SemanticArtifactNodeReference =
    std::variant<const SemanticExpression*, const SemanticRegion*, const SemanticStatement*>;
using SemanticArtifactNode = std::variant<
    std::unique_ptr<SemanticExpression>,
    std::unique_ptr<SemanticRegion>,
    std::unique_ptr<SemanticStatement>>;

struct SemanticArtifactContext final {
    ProgramIdentity program;
    ProvenanceIdentity provenance;
    std::array<std::size_t, static_cast<std::size_t>(SemanticArtifactTable::Count)> counts;
    std::map<std::pair<std::uint32_t, BodyIdentityDomain>, std::array<std::size_t, 3>> bodies;
    std::map<BodyID, std::vector<const SemAwait*>> awaits;
    std::vector<SemanticArtifactNodeReference> node_order;
    std::map<const void*, std::size_t> node_indices;
    std::vector<SemanticArtifactNode> node_values;
};

template<typename ID>
consteval auto semantic_artifact_table() noexcept -> SemanticArtifactTable {
    if constexpr (std::same_as<ID, ModuleID>) {
        return SemanticArtifactTable::Modules;
    } else if constexpr (std::same_as<ID, FunctionID>) {
        return SemanticArtifactTable::Functions;
    } else if constexpr (std::same_as<ID, StructID>) {
        return SemanticArtifactTable::Structs;
    } else if constexpr (std::same_as<ID, EnumID>) {
        return SemanticArtifactTable::Enums;
    } else if constexpr (std::same_as<ID, EnumCaseID>) {
        return SemanticArtifactTable::EnumCases;
    } else if constexpr (std::same_as<ID, ModuleConstantID>) {
        return SemanticArtifactTable::ModuleConstants;
    } else if constexpr (std::same_as<ID, CallableID>) {
        return SemanticArtifactTable::Callables;
    } else if constexpr (std::same_as<ID, BodyID>) {
        return SemanticArtifactTable::Bodies;
    } else if constexpr (std::same_as<ID, TestID>) {
        return SemanticArtifactTable::Tests;
    } else if constexpr (std::same_as<ID, TypeID>) {
        return SemanticArtifactTable::Types;
    } else if constexpr (std::same_as<ID, ConstantID>) {
        return SemanticArtifactTable::Constants;
    } else if constexpr (std::same_as<ID, FailureSetID>) {
        return SemanticArtifactTable::FailureSets;
    } else if constexpr (std::same_as<ID, CallableSignatureID>) {
        return SemanticArtifactTable::Signatures;
    } else if constexpr (std::same_as<ID, GenericDeclarationID>) {
        return SemanticArtifactTable::GenericDeclarations;
    } else if constexpr (std::same_as<ID, GenericTypeID>) {
        return SemanticArtifactTable::GenericTypes;
    } else if constexpr (std::same_as<ID, TypeTermID>) {
        return SemanticArtifactTable::TypeTerms;
    } else if constexpr (std::same_as<ID, FailureTermID>) {
        return SemanticArtifactTable::FailureTerms;
    } else if constexpr (std::same_as<ID, GenericFailureID>) {
        return SemanticArtifactTable::GenericFailures;
    } else if constexpr (std::same_as<ID, ProgramSourceID>) {
        return SemanticArtifactTable::Sources;
    } else if constexpr (std::same_as<ID, ProgramModuleID>) {
        return SemanticArtifactTable::ProvenanceModules;
    } else if constexpr (std::same_as<ID, ProgramSpellingID>) {
        return SemanticArtifactTable::Spellings;
    } else if constexpr (std::same_as<ID, ProgramOriginID>) {
        return SemanticArtifactTable::Origins;
    } else {
        static_assert(std::same_as<ID, void>, "unknown semantic artifact ID");
    }
}

template<typename... Values>
auto semantic_artifact_owned_tuple(std::tuple<Values...>) noexcept
    -> std::tuple<std::remove_cvref_t<Values>...>;

template<typename T>
inline constexpr auto semantic_artifact_table_owner = false;

template<typename T>
struct SemanticArtifactTableTraits;

template<typename Value, typename RowID>
inline constexpr auto semantic_artifact_table_owner<ImmutableProgramTable<Value, RowID>> = true;

template<typename Value, typename RowID>
struct SemanticArtifactTableTraits<ImmutableProgramTable<Value, RowID>> final {
    using ID = RowID;
    static constexpr auto body_local = false;
};

template<typename Value, typename RowID>
inline constexpr auto semantic_artifact_table_owner<MutableProgramTable<Value, RowID>> = true;

template<typename Value, typename RowID>
struct SemanticArtifactTableTraits<MutableProgramTable<Value, RowID>> final {
    using ID = RowID;
    static constexpr auto body_local = false;
};

template<typename Value, typename RowID>
inline constexpr auto semantic_artifact_table_owner<ReservedProgramTable<Value, RowID>> = true;

template<typename Value, typename RowID>
struct SemanticArtifactTableTraits<ReservedProgramTable<Value, RowID>> final {
    using ID = RowID;
    static constexpr auto body_local = false;
};

template<typename Value, typename RowID>
inline constexpr auto semantic_artifact_table_owner<ImmutableBodyTable<Value, RowID>> = true;

template<typename Value, typename RowID>
struct SemanticArtifactTableTraits<ImmutableBodyTable<Value, RowID>> final {
    using ID = RowID;
    static constexpr auto body_local = true;
};

template<typename T>
inline constexpr auto semantic_artifact_local_id = false;

template<typename Tag>
inline constexpr auto semantic_artifact_local_id<BodyLocalID<Tag>> = true;

template<typename T>
inline constexpr auto semantic_artifact_program_id = false;

template<typename Tag>
inline constexpr auto semantic_artifact_program_id<ProgramID<Tag>> = true;

template<typename Tag>
inline constexpr auto semantic_artifact_program_id<ProvenanceID<Tag>> = true;

template<typename Tag>
inline constexpr auto semantic_artifact_program_id<TypedID<Tag>> = true;

template<typename T>
auto valid_artifact_body_value(
    const T& value,
    BodyIdentity owner,
    const std::array<std::size_t, 3>& bounds
) noexcept -> bool;

class SemanticArtifactAccess final {
public:
    using ConstructionStorage = ProgramDraft::ConstructionStorage;

    template<typename T>
    static auto replace(T& target, T value) noexcept -> void {
        std::destroy_at(std::addressof(target));
        std::construct_at(std::addressof(target), std::move(value));
    }

    static auto context() noexcept -> SemanticArtifactContext {
        return SemanticArtifactContext {
            .program = ProgramIdentity::fresh(),
            .provenance = ProvenanceIdentity::fresh(),
            .counts = {},
            .bodies = {},
            .awaits = {},
            .node_order = {},
            .node_indices = {},
            .node_values = {},
        };
    }

    template<typename ID>
    static auto program_id(ProgramIdentity owner, std::uint32_t index) noexcept -> ID {
        return ID(owner, index);
    }

    template<typename ID>
    static auto provenance_id(ProvenanceIdentity owner, std::uint32_t index) noexcept -> ID {
        return ID(owner, index);
    }

    static auto body_identity(
        ProgramIdentity program,
        std::uint32_t index,
        BodyIdentityDomain domain
    ) noexcept -> BodyIdentity {
        return BodyIdentity(program, index, domain);
    }

    static auto body_key(BodyIdentity owner) noexcept
        -> std::pair<std::uint32_t, BodyIdentityDomain> {
        return {owner.body_index_value, owner.domain};
    }

    template<typename ID>
    static auto local_id(BodyIdentity owner, std::uint32_t index) noexcept -> ID {
        return ID(owner, index);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, BodyType>
    static auto fields(T& value) noexcept {
        return std::tie(value.value);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, BodyFailures>
    static auto fields(T& value) noexcept {
        return std::tie(value.value);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, IntegerConstant>
    static auto fields(T& value) noexcept {
        return std::tie(value.stored_magnitude, value.stored_negative);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, ExitSet>
    static auto fields(T& value) noexcept {
        return std::tie(value.bits);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, CanonicalTypeStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows, value.builtins);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, ConstantStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, FailureSetStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, CallableSignatureStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, CanonicalTypeStoreBuilder>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows, value.candidates, value.builtins);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, ConstantStoreBuilder>
    static auto fields(T& value) noexcept {
        return std::tie(value.program_identity, value.provenance_identity, value.rows, value.index);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, FailureSetStoreBuilder>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, CallableSignatureStoreBuilder>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, ConstructionTypeStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, LifetimeRegionTree>
    static auto fields(T& value) noexcept {
        return std::tie(value.region_rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, SemIRBody>
    static auto fields(T& value) noexcept {
        return std::tie(value.data);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, BodyStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, TestStore>
    static auto fields(T& value) noexcept {
        return std::tie(value.rows);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, DeclarationStore>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.module_rows,
            value.function_rows,
            value.struct_rows,
            value.enum_rows,
            value.enum_case_rows,
            value.module_constant_rows,
            value.callable_rows,
            value.body_callables,
            value.callable_functions
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, DeclarationBuilder>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.program_identity,
            value.provenance_identity,
            value.state,
            value.modules,
            value.functions,
            value.structures,
            value.enumerations,
            value.enum_cases,
            value.module_constants,
            value.callable_contracts,
            value.callable_signature_ids,
            value.callable_implementations,
            value.body_callables,
            value.callable_order
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, GenericFailureAlgebra>
    static auto fields(T& value) noexcept {
        return std::tie(value.expressions);
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, FailureConstraintStore>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.program_identity,
            value.provenance_identity,
            value.term_table,
            value.requirements,
            value.symbolic_graph,
            value.application_cache,
            value.applications,
            value.applications_by_result,
            value.active_application,
            value.growth,
            value.closing_applications
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, AnalysisCatalog>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.modules,
            value.symbols,
            value.function_symbols,
            value.struct_symbols,
            value.generic_symbols,
            value.enum_symbols,
            value.enum_case_symbols,
            value.visible_candidates,
            value.import_bindings,
            value.cpp_bindings,
            value.cpp_selections
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, CompilationProvenance>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.storage.sources,
            value.storage.modules,
            value.storage.spellings,
            value.storage.origins
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, SemIRProgram>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.program_identity,
            value.compilation_provenance,
            value.type_store,
            value.constant_store,
            value.failure_set_store,
            value.callable_signature_store,
            value.declaration_store,
            value.body_store,
            value.test_store,
            value.static_instance_store,
            value.generic_declaration_store,
            value.generic_instance_store,
            value.generic_function_store,
            value.generic_instance_functions,
            value.generic_instance_nominals,
            value.static_instance_index,
            value.instance_callables,
            value.executed_bodies,
            value.test_stops,
            value.cancellation_facts,
            value.contents,
            value.callable_surfaces,
            value.read_passing_facts
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, CheckedDefinitions>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.program_identity,
            value.provenance,
            value.construction,
            value.catalog
        );
    }

    template<typename T>
        requires std::same_as<std::remove_cv_t<T>, StructuredBodyDraft>
    static auto fields(T& value) noexcept {
        return std::tie(
            value.id,
            value.kind,
            value.provenance_identity,
            value.inputs,
            value.lifetime_regions,
            value.bindings,
            value.patterns,
            value.region,
            value.residual,
            value.specialized,
            value.generic_definition
        );
    }

    template<typename Value, typename ID>
    static auto fields(const ImmutableProgramTable<Value, ID>& value) noexcept {
        return std::tie(value.program_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(ImmutableProgramTable<Value, ID>& value) noexcept {
        return std::tie(value.program_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(const MutableProgramTable<Value, ID>& value) noexcept {
        return std::tie(value.program_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(MutableProgramTable<Value, ID>& value) noexcept {
        return std::tie(value.program_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(const ReservedProgramTable<Value, ID>& value) noexcept {
        return std::tie(value.program_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(ReservedProgramTable<Value, ID>& value) noexcept {
        return std::tie(value.program_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(const ImmutableBodyTable<Value, ID>& value) noexcept {
        return std::tie(value.body_identity, value.storage);
    }

    template<typename Value, typename ID>
    static auto fields(ImmutableBodyTable<Value, ID>& value) noexcept {
        return std::tie(value.body_identity, value.storage);
    }

    template<typename T, typename Tuple>
    static auto restore(Tuple values, SemanticArtifactContext& context) noexcept -> T {
        if constexpr (semantic_artifact_table_owner<T>) {
            return restore_table(
                std::type_identity<T> {},
                std::get<0>(values),
                std::move(std::get<1>(values))
            );
        } else if constexpr (std::same_as<T, BodyType>
                             || std::same_as<T, BodyFailures>
                             || std::same_as<T, IntegerConstant>
                             || std::same_as<T, ExitSet>
                             || std::same_as<T, CanonicalTypeStore>
                             || std::same_as<T, ConstantStore>
                             || std::same_as<T, FailureSetStore>
                             || std::same_as<T, CallableSignatureStore>
                             || std::same_as<T, LifetimeRegionTree>
                             || std::same_as<T, SemIRBody>
                             || std::same_as<T, TestStore>) {
            return std::apply(
                [](auto&&... fields) static noexcept -> T { return T(std::move(fields)...); },
                values
            );
        } else if constexpr (std::same_as<T, CanonicalTypeStoreBuilder>) {
            auto result = CanonicalTypeStoreBuilder(std::get<0>(values).owner());
            replace(result.rows, std::move(std::get<0>(values)));
            replace(result.candidates, std::move(std::get<1>(values)));
            replace(result.builtins, std::move(std::get<2>(values)));
            return result;
        } else if constexpr (std::same_as<T, FailureSetStoreBuilder>) {
            auto result = FailureSetStoreBuilder(std::get<0>(values).owner());
            replace(result.rows, std::move(std::get<0>(values)));
            return result;
        } else if constexpr (std::same_as<T, CallableSignatureStoreBuilder>) {
            auto result = CallableSignatureStoreBuilder(std::get<0>(values).owner());
            replace(result.rows, std::move(std::get<0>(values)));
            return result;
        } else if constexpr (std::same_as<T, ConstructionTypeStore>) {
            auto result = ConstructionTypeStore(std::get<0>(values).owner());
            replace(result.rows, std::move(std::get<0>(values)));
            return result;
        } else if constexpr (std::same_as<T, GenericFailureAlgebra>) {
            auto result = GenericFailureAlgebra(std::get<0>(values).owner());
            replace(result.expressions, std::move(std::get<0>(values)));
            return result;
        } else if constexpr (std::same_as<T, ConstantStoreBuilder>) {
            auto result = ConstantStoreBuilder(std::get<0>(values), std::get<1>(values));
            replace(result.program_identity, std::move(std::get<0>(values)));
            replace(result.provenance_identity, std::move(std::get<1>(values)));
            replace(result.rows, std::move(std::get<2>(values)));
            replace(result.index, std::move(std::get<3>(values)));
            return result;
        } else if constexpr (std::same_as<T, DeclarationBuilder>) {
            auto result = DeclarationBuilder(std::get<0>(values), std::get<1>(values));
            replace(result.program_identity, std::move(std::get<0>(values)));
            replace(result.provenance_identity, std::move(std::get<1>(values)));
            replace(result.state, std::move(std::get<2>(values)));
            replace(result.modules, std::move(std::get<3>(values)));
            replace(result.functions, std::move(std::get<4>(values)));
            replace(result.structures, std::move(std::get<5>(values)));
            replace(result.enumerations, std::move(std::get<6>(values)));
            replace(result.enum_cases, std::move(std::get<7>(values)));
            replace(result.module_constants, std::move(std::get<8>(values)));
            replace(result.callable_contracts, std::move(std::get<9>(values)));
            replace(result.callable_signature_ids, std::move(std::get<10>(values)));
            replace(result.callable_implementations, std::move(std::get<11>(values)));
            replace(result.body_callables, std::move(std::get<12>(values)));
            replace(result.callable_order, std::move(std::get<13>(values)));
            return result;
        } else if constexpr (std::same_as<T, FailureConstraintStore>) {
            auto result = FailureConstraintStore(std::get<0>(values), std::get<1>(values));
            replace(result.program_identity, std::move(std::get<0>(values)));
            replace(result.provenance_identity, std::move(std::get<1>(values)));
            replace(result.term_table, std::move(std::get<2>(values)));
            replace(result.requirements, std::move(std::get<3>(values)));
            replace(result.symbolic_graph, std::move(std::get<4>(values)));
            replace(result.application_cache, std::move(std::get<5>(values)));
            replace(result.applications, std::move(std::get<6>(values)));
            replace(result.applications_by_result, std::move(std::get<7>(values)));
            replace(result.active_application, std::move(std::get<8>(values)));
            replace(result.growth, std::move(std::get<9>(values)));
            replace(result.closing_applications, std::move(std::get<10>(values)));
            return result;
        } else if constexpr (std::same_as<T, AnalysisCatalog>) {
            auto result = AnalysisCatalog();
            replace(result.modules, std::move(std::get<0>(values)));
            replace(result.symbols, std::move(std::get<1>(values)));
            replace(result.function_symbols, std::move(std::get<2>(values)));
            replace(result.struct_symbols, std::move(std::get<3>(values)));
            replace(result.generic_symbols, std::move(std::get<4>(values)));
            replace(result.enum_symbols, std::move(std::get<5>(values)));
            replace(result.enum_case_symbols, std::move(std::get<6>(values)));
            replace(result.visible_candidates, std::move(std::get<7>(values)));
            replace(result.import_bindings, std::move(std::get<8>(values)));
            replace(result.cpp_bindings, std::move(std::get<9>(values)));
            replace(result.cpp_selections, std::move(std::get<10>(values)));
            return result;
        } else if constexpr (std::same_as<T, DeclarationStore>) {
            auto result = DeclarationStore(
                std::move(std::get<0>(values)),
                std::move(std::get<1>(values)),
                std::move(std::get<2>(values)),
                std::move(std::get<3>(values)),
                std::move(std::get<4>(values)),
                std::move(std::get<5>(values)),
                std::move(std::get<6>(values)),
                std::move(std::get<7>(values))
            );
            result.callable_functions = std::move(std::get<8>(values));
            return result;
        } else if constexpr (std::same_as<T, BodyStore>) {
            auto result = BodyStore(ImmutableProgramTable<SemIRBody, BodyID>(context.program, {}));
            replace(result.rows, std::move(std::get<0>(values)));
            return result;
        } else if constexpr (std::same_as<T, CompilationProvenance>) {
            auto storage = CompilationProvenanceStorage();
            storage.provenance_identity = context.provenance;
            storage.sources = std::move(std::get<0>(values));
            storage.modules = std::move(std::get<1>(values));
            storage.spellings = std::move(std::get<2>(values));
            storage.origins = std::move(std::get<3>(values));
            return CompilationProvenance(std::move(storage));
        } else if constexpr (std::same_as<T, CheckedDefinitions>) {
            return CheckedDefinitions(
                std::get<0>(values),
                std::move(*std::get<1>(values)),
                std::move(std::get<2>(values)),
                std::move(std::get<3>(values))
            );
        } else if constexpr (std::same_as<T, SemIRProgram>) {
            return std::apply(
                [](auto&&... fields) static noexcept -> SemIRProgram {
                    return SemIRProgram(SemIRProgramArtifactState {std::move(fields)...});
                },
                values
            );
        } else {
            return std::apply(
                [](auto&&... fields) static noexcept -> T { return T(std::move(fields)...); },
                values
            );
        }
    }

    template<typename Value, typename ID>
    static auto restore_table(
        std::type_identity<MutableProgramTable<Value, ID>>,
        ProgramIdentity owner,
        std::vector<Value> rows
    ) noexcept {
        auto result = MutableProgramTable<Value, ID>(owner);
        result.storage = std::move(rows);
        return result;
    }

    template<typename Value, typename ID>
    static auto restore_table(
        std::type_identity<ReservedProgramTable<Value, ID>>,
        ProgramIdentity owner,
        std::vector<std::optional<Value>> rows
    ) noexcept {
        auto result = ReservedProgramTable<Value, ID>(owner);
        result.storage = std::move(rows);
        return result;
    }

    template<typename Value, typename ID>
    static auto restore_table(
        std::type_identity<ImmutableProgramTable<Value, ID>>,
        ProgramIdentity owner,
        std::vector<Value> rows
    ) noexcept {
        return ImmutableProgramTable<Value, ID>(owner, std::move(rows));
    }

    template<typename Value, typename ID>
    static auto restore_table(
        std::type_identity<ImmutableBodyTable<Value, ID>>,
        BodyIdentity owner,
        std::vector<Value> rows
    ) noexcept {
        return ImmutableBodyTable<Value, ID>(owner, std::move(rows));
    }

    static auto source_snapshot(SourceID id, const SourceDocument& input) noexcept
        -> ProgramSourceSnapshot {
        auto result = ProgramSourceSnapshot(input.sources(), input.source().source_id);
        result.source_manager_id = id;
        return result;
    }

    static auto draft_storage(const ProgramDraft& draft) noexcept -> const ConstructionStorage& {
        return draft.storage;
    }

    static auto draft_provenance(const ProgramDraft& draft) noexcept {
        const auto& value = draft.provenance_appender.storage;
        return std::tie(value.sources, value.modules, value.spellings, value.origins);
    }

    static auto set_count(
        SemanticArtifactContext& context,
        SemanticArtifactTable table,
        std::size_t size
    ) noexcept -> void {
        context.counts[static_cast<std::size_t>(table)] = size;
    }

    template<typename T>
    static auto record_body_bounds(SemanticArtifactContext& context, const T& body) noexcept
        -> void {
        const auto key = body_key(body.lifetime_regions.region_rows.owner());
        const auto sizes = std::array<std::size_t, 3> {
            body.lifetime_regions.region_rows.size(),
            body.bindings.size(),
            body.patterns.size()
        };
        auto [row, inserted] = context.bodies.emplace(key, sizes);
        if (!inserted) {
            for (auto index = 0uz; index < sizes.size(); ++index) {
                row->second[index] = std::max(row->second[index], sizes[index]);
            }
        }
    }

    static auto record_provenance(
        SemanticArtifactContext& context,
        const CompilationProvenanceStorage& value
    ) noexcept -> void {
        set_count(context, SemanticArtifactTable::Sources, value.sources.size());
        set_count(context, SemanticArtifactTable::ProvenanceModules, value.modules.size());
        set_count(context, SemanticArtifactTable::Spellings, value.spellings.size());
        set_count(context, SemanticArtifactTable::Origins, value.origins.size());
    }

    static auto describe_bounds(const SemIRProgram& value) noexcept -> SemanticArtifactContext {
        auto result = context();
        result.program = value.program_identity;
        result.provenance = value.compilation_provenance.storage.provenance_identity;
        record_provenance(result, value.compilation_provenance.storage);
        set_count(
            result,
            SemanticArtifactTable::Modules,
            value.declaration_store.module_rows.size()
        );
        set_count(
            result,
            SemanticArtifactTable::Functions,
            value.declaration_store.function_rows.size()
        );
        set_count(
            result,
            SemanticArtifactTable::Structs,
            value.declaration_store.struct_rows.size()
        );
        set_count(result, SemanticArtifactTable::Enums, value.declaration_store.enum_rows.size());
        set_count(
            result,
            SemanticArtifactTable::EnumCases,
            value.declaration_store.enum_case_rows.size()
        );
        set_count(
            result,
            SemanticArtifactTable::ModuleConstants,
            value.declaration_store.module_constant_rows.size()
        );
        set_count(
            result,
            SemanticArtifactTable::Callables,
            value.declaration_store.callable_rows.size()
        );
        set_count(result, SemanticArtifactTable::Bodies, value.body_store.rows.size());
        set_count(result, SemanticArtifactTable::Tests, value.test_store.rows.size());
        set_count(result, SemanticArtifactTable::Types, value.type_store.rows.size());
        set_count(result, SemanticArtifactTable::Constants, value.constant_store.rows.size());
        set_count(result, SemanticArtifactTable::FailureSets, value.failure_set_store.rows.size());
        set_count(
            result,
            SemanticArtifactTable::Signatures,
            value.callable_signature_store.rows.size()
        );
        set_count(
            result,
            SemanticArtifactTable::GenericDeclarations,
            value.generic_declaration_store.size()
        );
        for (const auto& body : value.body_store.rows.storage) {
            if (body) {
                record_body_bounds(result, body->data);
            }
        }
        return result;
    }

    static auto describe(const SemIRProgram& value) noexcept -> SemanticArtifactContext {
        auto result = describe_bounds(value);
        for (const auto& body : value.body_store.rows.storage) {
            if (body) {
                record_body_nodes(result, body->data);
            }
        }
        index_nodes(result);
        return result;
    }

    static auto describe(const ProgramDraft& draft) noexcept -> SemanticArtifactContext {
        auto result = context();
        result.program = draft.program_identity;
        result.provenance = draft.provenance_appender.storage.provenance_identity;
        record_provenance(result, draft.provenance_appender.storage);
        describe_construction_bounds(result, draft.storage);
        record_construction_nodes(result, draft.storage);
        return result;
    }

    static auto describe_bounds(const CheckedDefinitions& value) noexcept
        -> SemanticArtifactContext {
        auto result = context();
        result.program = value.program_identity;
        result.provenance = value.provenance->storage.provenance_identity;
        record_provenance(result, value.provenance->storage);
        describe_construction_bounds(result, *value.construction);
        return result;
    }

    static auto describe_construction_bounds(
        SemanticArtifactContext& result,
        const ConstructionStorage& value
    ) noexcept -> void {
        set_count(result, SemanticArtifactTable::Modules, value.declarations.modules.size());
        set_count(result, SemanticArtifactTable::Functions, value.declarations.functions.size());
        set_count(result, SemanticArtifactTable::Structs, value.declarations.structures.size());
        set_count(result, SemanticArtifactTable::Enums, value.declarations.enumerations.size());
        set_count(result, SemanticArtifactTable::EnumCases, value.declarations.enum_cases.size());
        set_count(
            result,
            SemanticArtifactTable::ModuleConstants,
            value.declarations.module_constants.size()
        );
        set_count(
            result,
            SemanticArtifactTable::Callables,
            value.declarations.callable_contracts.size()
        );
        set_count(result, SemanticArtifactTable::Bodies, value.bodies.size());
        set_count(result, SemanticArtifactTable::Tests, value.test_slots.size());
        set_count(result, SemanticArtifactTable::Types, value.types.rows.size());
        set_count(result, SemanticArtifactTable::Constants, value.constants.rows.size());
        set_count(result, SemanticArtifactTable::FailureSets, value.failure_sets.rows.size());
        set_count(result, SemanticArtifactTable::Signatures, value.callable_signatures.rows.size());
        set_count(
            result,
            SemanticArtifactTable::GenericDeclarations,
            value.generic_definitions.size()
        );
        set_count(result, SemanticArtifactTable::GenericTypes, value.generic_types.size());
        set_count(result, SemanticArtifactTable::TypeTerms, value.construction_types.rows.size());
        set_count(
            result,
            SemanticArtifactTable::FailureTerms,
            value.failure_constraints.term_table.size()
        );
        set_count(
            result,
            SemanticArtifactTable::GenericFailures,
            value.failure_constraints.symbolic_graph->expressions.size()
        );
        for (const auto& body : value.bodies) {
            if (body.definition) {
                record_body_bounds(result, *body.definition);
            }
        }
        for (const auto& [id, body] : value.generic_function_bodies) {
            static_cast<void>(id);
            if (body) {
                record_body_bounds(result, *body);
            }
        }
    }

    template<typename T>
    static auto record_body_nodes(SemanticArtifactContext& context, const T& body) noexcept
        -> void {
        record_node_root(context, body.region);
        if (body.residual) {
            record_node_root(context, *body.residual);
        }
    }

    static auto record_construction_nodes(
        SemanticArtifactContext& context,
        const ConstructionStorage& value
    ) noexcept -> void {
        for (const auto& body : value.bodies) {
            if (body.definition) {
                record_body_nodes(context, *body.definition);
            }
        }
        for (const auto& [id, body] : value.generic_function_bodies) {
            static_cast<void>(id);
            if (body) {
                record_body_nodes(context, *body);
            }
        }
        index_nodes(context);
    }

    static auto record_node_root(
        SemanticArtifactContext& context,
        const SemanticRegion& region
    ) noexcept -> void {
        visit_semantic_nodes(region, [&](const auto& node) noexcept {
            context.node_order.emplace_back(std::addressof(node));
        });
    }

    static auto index_nodes(SemanticArtifactContext& context) noexcept -> void {
        std::ranges::reverse(context.node_order);
        for (const auto [index, reference] : std::views::enumerate(context.node_order)) {
            reference.visit([&](const auto* node) noexcept {
                context.node_indices.emplace(
                    static_cast<const void*>(node),
                    static_cast<std::size_t>(index)
                );
            });
        }
    }

    static auto record_awaits(SemanticArtifactContext& context, const BodyStore& store) noexcept
        -> void {
        for (const auto& body : store.rows.storage) {
            if (!body) {
                continue;
            }
            auto nodes = std::vector<const SemAwait*>();
            visit_semantic_nodes(
                body->region(),
                [&](const SemanticExpression& expression) noexcept {
                    if (const auto* node = std::get_if<SemAwait>(&expression.value)) {
                        nodes.push_back(node);
                    }
                }
            );
            context.awaits.emplace(body->id(), std::move(nodes));
        }
    }

    template<typename T, typename Tuple>
    static auto valid_record(const Tuple& values, const SemanticArtifactContext& context) noexcept
        -> bool {
        if constexpr (std::same_as<T, StructuredBodyDraft>) {
            const auto bounds = std::array<std::size_t, 3> {
                std::get<4>(values).region_rows.size(),
                std::get<5>(values).size(),
                std::get<6>(values).size()
            };
            const auto owner = std::get<4>(values).owner();
            return valid_artifact_body_value(values, owner, bounds);
        } else if constexpr (std::same_as<T, SemIRBody>) {
            const auto& body = std::get<0>(values);
            const auto bounds = std::array<std::size_t, 3> {
                body.lifetime_regions.region_rows.size(),
                body.bindings.size(),
                body.patterns.size()
            };
            return valid_artifact_body_value(body, body.lifetime_regions.owner(), bounds);
        } else if constexpr (std::same_as<T, CheckedDefinitions>) {
            return std::get<1>(values).has_value()
                && std::get<2>(values) != nullptr
                && std::get<3>(values) != nullptr;
        } else if constexpr (semantic_artifact_table_owner<T>) {
            using Traits = SemanticArtifactTableTraits<T>;
            using ID = typename Traits::ID;
            if constexpr (Traits::body_local) {
                const auto found = context.bodies.find(body_key(std::get<0>(values)));
                if (found == context.bodies.end()) {
                    return false;
                }
                constexpr auto column = std::same_as<ID, LifetimeRegionID> ? 0uz
                    : std::same_as<ID, LocalBindingID>                     ? 1uz
                                                                           : 2uz;
                return std::get<1>(values).size() <= found->second[column];
            } else {
                return std::get<1>(values).size()
                    == context.counts[static_cast<std::size_t>(semantic_artifact_table<ID>())];
            }
        } else if constexpr (std::same_as<T, SemIRProgram>) {
            const auto& bodies = std::get<7>(values).rows.storage;
            const auto& executed = std::get<17>(values);
            const auto callable_count =
                context.counts[static_cast<std::size_t>(SemanticArtifactTable::Callables)];
            if (executed.size() != bodies.size()
                || std::get<18>(values).size() != callable_count
                || std::get<19>(values) == nullptr
                || std::get<20>(values).size()
                    != context.counts[static_cast<std::size_t>(SemanticArtifactTable::Types)]
                || std::get<21>(values).size() != callable_count) {
                return false;
            }
            for (auto index = 0uz; index < bodies.size(); ++index) {
                if (executed[index] != (bodies[index] != nullptr)) {
                    return false;
                }
            }
            for (const auto& [callable, index] : std::get<13>(values)) {
                static_cast<void>(callable);
                if (index >= std::get<12>(values).size()) {
                    return false;
                }
            }
            for (const auto& [declaration, index] : std::get<14>(values)) {
                static_cast<void>(declaration);
                if (index >= std::get<11>(values).size()) {
                    return false;
                }
            }
            for (const auto& [callable, index] : std::get<16>(values)) {
                static_cast<void>(callable);
                if (index >= std::get<9>(values).size()) {
                    return false;
                }
            }
        }
        return true;
    }
};

template<typename T>
auto valid_artifact_body_fields(
    const T& value,
    BodyIdentity owner,
    const std::array<std::size_t, 3>& bounds,
    bool skip_nodes
) noexcept -> bool {
    if constexpr (std::same_as<T, SemanticExpression>
                  || std::same_as<T, SemanticRegion>
                  || std::same_as<T, SemanticStatement>) {
        if (skip_nodes) {
            return true;
        }
        auto valid = true;
        auto visitor = [&](const auto& node) noexcept {
            valid = valid
                && valid_artifact_body_fields(binary_aggregate_fields(node), owner, bounds, true);
        };
        auto traversal = SemanticTraversal<true, decltype(visitor)>(visitor);
        traversal(value);
        return valid;
    } else if constexpr (semantic_artifact_local_id<T>) {
        constexpr auto column = std::same_as<T, LifetimeRegionID> ? 0uz
            : std::same_as<T, LocalBindingID>                     ? 1uz
                                                                  : 2uz;
        return value.owner() == owner && value.index() < bounds[column];
    } else if constexpr (std::same_as<T, BodyIdentity>) {
        return value == owner;
    } else if constexpr (semantic_artifact_program_id<T>
                         || std::is_arithmetic_v<T>
                         || std::is_enum_v<T>
                         || std::same_as<T, ProgramIdentity>
                         || std::same_as<T, ProvenanceIdentity>
                         || std::same_as<T, std::string>) {
        return true;
    } else if constexpr (requires { value.visit([](const auto&) static noexcept {}); }) {
        return value.visit([&](const auto& child) noexcept {
            return valid_artifact_body_fields(child, owner, bounds, skip_nodes);
        });
    } else if constexpr (requires {
                             value.has_value();
                             *value;
                         }) {
        return !value.has_value() || valid_artifact_body_fields(*value, owner, bounds, skip_nodes);
    } else if constexpr (requires {
                             value.get();
                             *value;
                         }) {
        return value.get() == nullptr
            || valid_artifact_body_fields(*value, owner, bounds, skip_nodes);
    } else if constexpr (requires { SemanticArtifactAccess::fields(value); }) {
        return valid_artifact_body_fields(
            SemanticArtifactAccess::fields(value),
            owner,
            bounds,
            skip_nodes
        );
    } else if constexpr (std::ranges::range<T>) {
        for (const auto& child : value) {
            if (!valid_artifact_body_fields(child, owner, bounds, skip_nodes)) {
                return false;
            }
        }
        return true;
    } else if constexpr (requires { std::tuple_size<T>::value; }) {
        return std::apply(
            [&](const auto&... fields) noexcept {
                return (valid_artifact_body_fields(fields, owner, bounds, skip_nodes) && ...);
            },
            value
        );
    } else {
        static_assert(std::is_aggregate_v<T>, "body validation needs an owning field adapter");
        return valid_artifact_body_fields(
            binary_aggregate_fields(value),
            owner,
            bounds,
            skip_nodes
        );
    }
}

template<typename T>
auto valid_artifact_body_value(
    const T& value,
    BodyIdentity owner,
    const std::array<std::size_t, 3>& bounds
) noexcept -> bool {
    return valid_artifact_body_fields(value, owner, bounds, false);
}

template<typename T>
    requires requires (T& value) { SemanticArtifactAccess::fields(value); }
struct BinaryCodec<T> {
    template<typename Context>
    static auto write(BinaryWriter& writer, const T& value, Context& context) noexcept -> void {
        writer.write(SemanticArtifactAccess::fields(value), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<T, BinaryError> {
        using Fields = decltype(SemanticArtifactAccess::fields(std::declval<T&>()));
        using OwnedFields = decltype(semantic_artifact_owned_tuple(std::declval<Fields>()));
        auto fields = reader.read<OwnedFields>(context);
        if (!fields) {
            return std::unexpected(std::move(fields.error()));
        }
        if (!SemanticArtifactAccess::valid_record<T>(*fields, context)) {
            return reader.fail(
                "semantic record disagrees with its published ownership or table bounds"
            );
        }
        auto result = SemanticArtifactAccess::restore<T>(std::move(*fields), context);
        if constexpr (std::same_as<T, BodyStore>) {
            SemanticArtifactAccess::record_awaits(context, result);
        }
        return result;
    }
};

template<>
struct BinaryCodec<ProgramIdentity> {
    static auto write(
        BinaryWriter& writer,
        ProgramIdentity value,
        SemanticArtifactContext& context
    ) noexcept -> void {
        if (value != context.program) {
            writer.fail("foreign program identity in semantic artifact");
        }
    }

    static auto read(BinaryReader&, SemanticArtifactContext& context) noexcept
        -> std::expected<ProgramIdentity, BinaryError> {
        return context.program;
    }
};

template<>
struct BinaryCodec<ProvenanceIdentity> {
    static auto write(
        BinaryWriter& writer,
        ProvenanceIdentity value,
        SemanticArtifactContext& context
    ) noexcept -> void {
        if (value != context.provenance) {
            writer.fail("foreign provenance identity in semantic artifact");
        }
    }

    static auto read(BinaryReader&, SemanticArtifactContext& context) noexcept
        -> std::expected<ProvenanceIdentity, BinaryError> {
        return context.provenance;
    }
};

template<typename ID>
auto semantic_artifact_id_bound(const SemanticArtifactContext& context) noexcept -> std::size_t {
    const auto count = context.counts[static_cast<std::size_t>(semantic_artifact_table<ID>())];
    if constexpr (std::same_as<ID, BodyID>) {
        // Rigid generic definitions use their declaration ordinal as a source
        // BodyID; their local identity retains the GenericDefinition domain.
        return std::max(
            count,
            context.counts[static_cast<std::size_t>(SemanticArtifactTable::GenericDeclarations)]
        );
    }
    return count;
}

template<typename Tag>
struct BinaryCodec<ProgramID<Tag>> {
    using ID = ProgramID<Tag>;

    static auto write(BinaryWriter& writer, ID value, SemanticArtifactContext& context) noexcept
        -> void {
        if (value.owner() != context.program
            || value.index() >= semantic_artifact_id_bound<ID>(context)) {
            writer.fail("invalid program row identity in semantic artifact");
            return;
        }
        writer.unsigned_value(value.index(), 4);
    }

    static auto read(BinaryReader& reader, SemanticArtifactContext& context) noexcept
        -> std::expected<ID, BinaryError> {
        auto index = reader.unsigned_value(4);
        if (!index) {
            return std::unexpected(std::move(index.error()));
        }
        if (*index >= semantic_artifact_id_bound<ID>(context)) {
            return reader.fail("invalid program row identity in semantic artifact");
        }
        return SemanticArtifactAccess::program_id<ID>(
            context.program,
            static_cast<std::uint32_t>(*index)
        );
    }
};

template<typename Tag>
struct BinaryCodec<ProvenanceID<Tag>> {
    using ID = ProvenanceID<Tag>;

    static auto write(BinaryWriter& writer, ID value, SemanticArtifactContext& context) noexcept
        -> void {
        if (value.owner() != context.provenance
            || value.index() >= semantic_artifact_id_bound<ID>(context)) {
            writer.fail("invalid provenance row identity in semantic artifact");
            return;
        }
        writer.unsigned_value(value.index(), 4);
    }

    static auto read(BinaryReader& reader, SemanticArtifactContext& context) noexcept
        -> std::expected<ID, BinaryError> {
        auto index = reader.unsigned_value(4);
        if (!index) {
            return std::unexpected(std::move(index.error()));
        }
        if (*index >= semantic_artifact_id_bound<ID>(context)) {
            return reader.fail("invalid provenance row identity in semantic artifact");
        }
        return SemanticArtifactAccess::provenance_id<ID>(
            context.provenance,
            static_cast<std::uint32_t>(*index)
        );
    }
};

template<>
struct BinaryCodec<BodyIdentity> {
    static auto write(
        BinaryWriter& writer,
        BodyIdentity value,
        SemanticArtifactContext& context
    ) noexcept -> void {
        const auto key = SemanticArtifactAccess::body_key(value);
        if (value.program() != context.program || !context.bodies.contains(key)) {
            writer.fail("invalid local table owner in semantic artifact");
            return;
        }
        writer.write(key, context);
    }

    static auto read(BinaryReader& reader, SemanticArtifactContext& context) noexcept
        -> std::expected<BodyIdentity, BinaryError> {
        auto key = reader.read<std::pair<std::uint32_t, BodyIdentityDomain>>(context);
        if (!key) {
            return std::unexpected(std::move(key.error()));
        }
        if (!context.bodies.contains(*key)) {
            return reader.fail("invalid local table owner in semantic artifact");
        }
        return SemanticArtifactAccess::body_identity(context.program, key->first, key->second);
    }
};

template<typename ID>
consteval auto semantic_artifact_local_table() noexcept -> std::size_t {
    if constexpr (std::same_as<ID, LifetimeRegionID>) {
        return 0;
    } else if constexpr (std::same_as<ID, LocalBindingID>) {
        return 1;
    } else if constexpr (std::same_as<ID, PatternID>) {
        return 2;
    } else {
        static_assert(std::same_as<ID, void>, "unknown body-local identity");
    }
}

template<typename Tag>
struct BinaryCodec<BodyLocalID<Tag>> {
    using ID = BodyLocalID<Tag>;

    static auto write(BinaryWriter& writer, ID value, SemanticArtifactContext& context) noexcept
        -> void {
        writer.write(value.owner(), context);
        const auto found = context.bodies.find(SemanticArtifactAccess::body_key(value.owner()));
        if (found == context.bodies.end()
            || value.index() >= found->second[semantic_artifact_local_table<ID>()]) {
            writer.fail("invalid body-local row identity in semantic artifact");
            return;
        }
        writer.unsigned_value(value.index(), 4);
    }

    static auto read(BinaryReader& reader, SemanticArtifactContext& context) noexcept
        -> std::expected<ID, BinaryError> {
        auto owner = reader.read<BodyIdentity>(context);
        if (!owner) {
            return std::unexpected(std::move(owner.error()));
        }
        auto index = reader.unsigned_value(4);
        if (!index) {
            return std::unexpected(std::move(index.error()));
        }
        const auto found = context.bodies.find(SemanticArtifactAccess::body_key(*owner));
        if (found == context.bodies.end()
            || *index >= found->second[semantic_artifact_local_table<ID>()]) {
            return reader.fail("invalid body-local row identity in semantic artifact");
        }
        return SemanticArtifactAccess::local_id<ID>(*owner, static_cast<std::uint32_t>(*index));
    }
};

template<typename Tag>
struct BinaryCodec<TypedID<Tag>> {
    template<typename Context>
    static auto write(BinaryWriter& writer, TypedID<Tag> value, Context&) noexcept -> void {
        writer.unsigned_value(value.index(), 4);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context&) noexcept
        -> std::expected<TypedID<Tag>, BinaryError> {
        auto index = reader.unsigned_value(4);
        if (!index) {
            return std::unexpected(std::move(index.error()));
        }
        return TypedID<Tag>::from_index(static_cast<std::uint32_t>(*index));
    }
};

template<>
struct BinaryCodec<Span> {
    template<typename Context>
    static auto write(BinaryWriter& writer, Span value, Context& context) noexcept -> void {
        writer.write(value.start(), context);
        writer.write(value.end(), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Span, BinaryError> {
        auto start = reader.read<std::uint32_t>(context);
        if (!start) {
            return std::unexpected(std::move(start.error()));
        }
        auto end = reader.read<std::uint32_t>(context);
        if (!end) {
            return std::unexpected(std::move(end.error()));
        }
        if (*start > *end) {
            return reader.fail("invalid source span in semantic artifact");
        }
        return Span::from_bounds(*start, *end);
    }
};

template<>
struct BinaryCodec<CanonicalModulePath> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const CanonicalModulePath& value,
        Context& context
    ) noexcept -> void {
        writer.write(std::string(value.value()), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<CanonicalModulePath, BinaryError> {
        auto text = reader.read<std::string>(context);
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        auto value = CanonicalModulePath::from_value(*text);
        if (!value) {
            return reader.fail("invalid module path in semantic artifact");
        }
        return std::move(*value);
    }
};

template<>
struct BinaryCodec<ProgramSourceSnapshot> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const ProgramSourceSnapshot& value,
        Context& context
    ) noexcept -> void {
        writer.write(value.manager_source_id(), context);
        writer.write(std::string(value.display_origin()), context);
        writer.write(std::string(value.text()), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<ProgramSourceSnapshot, BinaryError> {
        auto id = reader.read<SourceID>(context);
        if (!id) {
            return std::unexpected(std::move(id.error()));
        }
        auto origin = reader.read<std::string>(context);
        if (!origin) {
            return std::unexpected(std::move(origin.error()));
        }
        auto text = reader.read<std::string>(context);
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        const auto document = SourceDocument::create(std::move(*origin), std::move(*text));
        if (!document) {
            return reader.fail("invalid source snapshot in semantic artifact");
        }
        return SemanticArtifactAccess::source_snapshot(*id, **document);
    }
};

template<>
struct BinaryCodec<AnalysisFailure> {
    template<typename Context>
    static auto write(BinaryWriter& writer, const AnalysisFailure&, Context&) noexcept -> void {
        writer.fail("failed analysis cannot be published as a semantic artifact");
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context&) noexcept
        -> std::expected<AnalysisFailure, BinaryError> {
        return reader.fail("failed analysis cannot be restored from a semantic artifact");
    }
};

template<>
struct BinaryCodec<AsyncCancellationFacts> {
    static auto write(
        BinaryWriter& writer,
        const AsyncCancellationFacts& value,
        SemanticArtifactContext& context
    ) noexcept -> void {
        writer.write(value.callable_completion, context);
        writer.unsigned_value(value.await_completion.size(), 8);
        for (const auto& [body, occurrences] : value.await_completion) {
            writer.write(body, context);
            writer.unsigned_value(occurrences.size(), 8);
            const auto found = context.awaits.find(body);
            if (found == context.awaits.end()) {
                writer.fail("cancellation facts reference an absent body");
                return;
            }
            for (const auto& [occurrence, cancelled] : occurrences) {
                const auto ordinal = std::ranges::find(found->second, occurrence);
                if (ordinal == found->second.end()) {
                    writer.fail("cancellation facts reference a foreign await");
                    return;
                }
                writer.unsigned_value(static_cast<std::size_t>(ordinal - found->second.begin()), 8);
                writer.write(cancelled, context);
            }
        }
    }

    static auto read(BinaryReader& reader, SemanticArtifactContext& context) noexcept
        -> std::expected<AsyncCancellationFacts, BinaryError> {
        auto callables = reader.read<std::vector<bool>>(context);
        if (!callables) {
            return std::unexpected(std::move(callables.error()));
        }
        if (callables->size()
            != context.counts[static_cast<std::size_t>(SemanticArtifactTable::Callables)]) {
            return reader.fail("cancellation facts disagree with callable table");
        }
        auto count = reader.count(sizeof(BodyID));
        if (!count) {
            return std::unexpected(std::move(count.error()));
        }
        auto result = AsyncCancellationFacts {
            .callable_completion = std::move(*callables),
            .await_completion = {}
        };
        for (auto index = 0uz; index < *count; ++index) {
            auto body = reader.read<BodyID>(context);
            if (!body) {
                return std::unexpected(std::move(body.error()));
            }
            const auto found = context.awaits.find(*body);
            if (found == context.awaits.end()) {
                return reader.fail("cancellation facts reference an absent body");
            }
            auto count = reader.count(sizeof(std::pair<const SemAwait*, bool>));
            if (!count) {
                return std::unexpected(std::move(count.error()));
            }
            auto occurrences = std::map<const SemAwait*, bool>();
            for (auto occurrence = 0uz; occurrence < *count; ++occurrence) {
                auto ordinal = reader.unsigned_value(8);
                if (!ordinal) {
                    return std::unexpected(std::move(ordinal.error()));
                }
                if (*ordinal >= found->second.size()) {
                    return reader.fail("cancellation facts reference an invalid await ordinal");
                }
                auto cancelled = reader.read<bool>(context);
                if (!cancelled) {
                    return std::unexpected(std::move(cancelled.error()));
                }
                if (!occurrences
                         .emplace(found->second[static_cast<std::size_t>(*ordinal)], *cancelled)
                         .second) {
                    return reader.fail("duplicate cancellation fact for an await");
                }
            }
            if (!result.await_completion.emplace(*body, std::move(occurrences)).second) {
                return reader.fail("duplicate body cancellation facts");
            }
        }
        return result;
    }
};

template<typename T>
struct SemanticArtifactNodeCodec {
    static auto write(
        BinaryWriter& writer,
        const T& value,
        SemanticArtifactContext& context
    ) noexcept -> void {
        const auto found =
            context.node_indices.find(static_cast<const void*>(std::addressof(value)));
        if (found == context.node_indices.end()) {
            writer.fail("semantic artifact contains an unindexed operation");
            return;
        }
        writer.unsigned_value(found->second, 8);
    }

    static auto read(BinaryReader& reader, SemanticArtifactContext& context) noexcept
        -> std::expected<T, BinaryError> {
        auto index = reader.unsigned_value(8);
        if (!index) {
            return std::unexpected(std::move(index.error()));
        }
        if (*index >= context.node_values.size()) {
            return reader.fail("invalid semantic operation owner ordinal");
        }
        auto* value =
            std::get_if<std::unique_ptr<T>>(&context.node_values[static_cast<std::size_t>(*index)]);
        if (value == nullptr || *value == nullptr) {
            return reader.fail("invalid or repeated semantic operation ownership");
        }
        auto result = std::move(**value);
        value->reset();
        return result;
    }
};

template<>
struct BinaryCodec<SemanticExpression> final : SemanticArtifactNodeCodec<SemanticExpression> {};

template<>
struct BinaryCodec<SemanticRegion> final : SemanticArtifactNodeCodec<SemanticRegion> {};

template<>
struct BinaryCodec<SemanticStatement> final : SemanticArtifactNodeCodec<SemanticStatement> {};

namespace {

struct SemanticHeaderContext final {};

enum class SemanticArtifactKind : std::uint32_t { Program = 1, Definitions = 2 };

auto write_semantic_nodes(BinaryWriter& writer, SemanticArtifactContext& context) noexcept -> void {
    writer.unsigned_value(context.node_order.size(), 8);
    for (const auto& reference : context.node_order) {
        writer.unsigned_value(reference.index(), 1);
        reference.visit([&](const auto* node) noexcept {
            writer.write(binary_aggregate_fields(*node), context);
        });
    }
}

template<typename T>
auto read_semantic_node(BinaryReader& reader, SemanticArtifactContext& context) noexcept
    -> std::expected<SemanticArtifactNode, BinaryError> {
    using Fields = decltype(binary_aggregate_fields(std::declval<T&>()));
    using OwnedFields = decltype(semantic_artifact_owned_tuple(std::declval<Fields>()));
    auto fields = reader.read<OwnedFields>(context);
    if (!fields) {
        return std::unexpected(std::move(fields.error()));
    }
    return std::apply(
        [](auto&&... values) static noexcept -> SemanticArtifactNode {
            return std::make_unique<T>(T {std::move(values)...});
        },
        *fields
    );
}

auto read_semantic_nodes(BinaryReader& reader, SemanticArtifactContext& context) noexcept
    -> std::expected<void, BinaryError> {
    auto count = reader.count(sizeof(SemanticArtifactNode));
    if (!count) {
        return std::unexpected(std::move(count.error()));
    }
    context.node_values.reserve(*count);
    for (auto index = 0uz; index < *count; ++index) {
        auto kind = reader.unsigned_value(1);
        if (!kind) {
            return std::unexpected(std::move(kind.error()));
        }
        auto node = [&]() noexcept -> std::expected<SemanticArtifactNode, BinaryError> {
            switch (*kind) {
                case 0:  return read_semantic_node<SemanticExpression>(reader, context);
                case 1:  return read_semantic_node<SemanticRegion>(reader, context);
                case 2:  return read_semantic_node<SemanticStatement>(reader, context);
                default: return reader.fail("invalid semantic operation kind");
            }
        }();
        if (!node) {
            return std::unexpected(std::move(node.error()));
        }
        context.node_values.push_back(std::move(*node));
    }
    return {};
}

auto consumed_semantic_nodes(const SemanticArtifactContext& context) noexcept -> bool {
    return std::ranges::all_of(context.node_values, [](const auto& node) static noexcept {
        return node.visit([](const auto& owner) static noexcept { return owner == nullptr; });
    });
}

auto write_semantic_header(
    BinaryWriter& writer,
    SemanticArtifactContext& context,
    SemanticArtifactKind kind
) noexcept -> void {
    auto header = SemanticHeaderContext {};
    writer.unsigned_value(static_cast<std::uint32_t>(kind), 4);
    writer.write(context.counts, header);
    writer.write(context.bodies, header);
    write_semantic_nodes(writer, context);
}

auto read_semantic_header(BinaryReader& reader, SemanticArtifactKind kind) noexcept
    -> std::expected<SemanticArtifactContext, BinaryError> {
    auto stored_kind = reader.unsigned_value(4);
    if (!stored_kind) {
        return std::unexpected(std::move(stored_kind.error()));
    }
    if (*stored_kind != static_cast<std::uint32_t>(kind)) {
        return reader.fail("unsupported semantic artifact header");
    }
    auto result = SemanticArtifactAccess::context();
    auto header = SemanticHeaderContext {};
    auto counts = reader.read<decltype(result.counts)>(header);
    if (!counts) {
        return std::unexpected(std::move(counts.error()));
    }
    for (const auto count : *counts) {
        if (count > std::numeric_limits<std::uint32_t>::max()) {
            return reader.fail("semantic artifact exceeds row identity range");
        }
    }
    result.counts = *counts;
    auto bodies = reader.read<decltype(result.bodies)>(header);
    if (!bodies) {
        return std::unexpected(std::move(bodies.error()));
    }
    for (const auto& [key, counts] : *bodies) {
        if (key.second != BodyIdentityDomain::Body
            && key.second != BodyIdentityDomain::GenericDefinition) {
            return reader.fail("invalid semantic body identity domain");
        }
        const auto bound = result.counts[static_cast<std::size_t>(
            key.second == BodyIdentityDomain::Body ? SemanticArtifactTable::Bodies
                                                   : SemanticArtifactTable::GenericDeclarations
        )];
        if (key.first >= bound
            || std::ranges::any_of(counts, [](std::size_t count) static noexcept {
                   return count > std::numeric_limits<std::uint32_t>::max();
               })) {
            return reader.fail("invalid semantic body table bounds");
        }
    }
    result.bodies = std::move(*bodies);
    auto nodes = read_semantic_nodes(reader, result);
    if (!nodes) {
        return std::unexpected(std::move(nodes.error()));
    }
    return result;
}

auto valid_semantic_bounds(
    const SemanticArtifactContext& expected,
    const SemanticArtifactContext& actual
) noexcept -> bool {
    return expected.counts == actual.counts && expected.bodies == actual.bodies;
}

auto valid_provenance(CompilationProvenanceView provenance) noexcept -> bool {
    for (const auto& module : provenance.module_records()) {
        if (!provenance.contains(module.source_id)) {
            return false;
        }
    }
    for (const auto [index, origin] : std::views::enumerate(provenance.origins())) {
        if (const auto* source = std::get_if<ProgramSourceOrigin>(&origin.value)) {
            if (!provenance.contains(source->source_id)
                || source->span.end() > provenance.source_snapshot(source->source_id).size()) {
                return false;
            }
        } else if (const auto* expansion = std::get_if<ProgramExpansionOrigin>(&origin.value)) {
            if (expansion->parent_origin_id.index() >= index) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

auto capture_checked_definitions(const ProgramDraft& draft, const AnalysisCatalog& catalog) noexcept
    -> std::expected<std::vector<std::byte>, BinaryError> {
    auto context = SemanticArtifactAccess::describe(draft);
    auto writer = BinaryWriter();
    write_semantic_header(writer, context, SemanticArtifactKind::Definitions);
    writer.write(draft.identity(), context);
    writer.write(true, context);
    writer.write(SemanticArtifactAccess::draft_provenance(draft), context);
    writer.write(true, context);
    writer.write(SemanticArtifactAccess::draft_storage(draft), context);
    writer.write(true, context);
    writer.write(catalog, context);
    if (writer.error()) {
        return std::unexpected(*writer.error());
    }
    return std::move(writer).take();
}

auto decode_checked_definitions(std::span<const std::byte> bytes) noexcept
    -> std::expected<CheckedDefinitions, BinaryError> {
    auto reader = BinaryReader(bytes);
    auto context = read_semantic_header(reader, SemanticArtifactKind::Definitions);
    if (!context) {
        return std::unexpected(std::move(context.error()));
    }
    auto result = reader.read<CheckedDefinitions>(*context);
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    auto finish = reader.finish();
    if (!finish) {
        return std::unexpected(std::move(finish.error()));
    }
    if (!consumed_semantic_nodes(*context)) {
        return reader.fail("unclaimed semantic operation ownership");
    }
    if (!valid_semantic_bounds(*context, SemanticArtifactAccess::describe_bounds(*result))) {
        return reader.fail("semantic definition tables disagree with their bounds");
    }
    const auto fields = SemanticArtifactAccess::fields(*result);
    if (!valid_provenance(std::get<1>(fields)->view())) {
        return reader.fail("invalid checked definition source provenance");
    }
    return result;
}

auto encode_semir_program(const SemIRProgram& program) noexcept
    -> std::expected<std::vector<std::byte>, BinaryError> {
    auto context = SemanticArtifactAccess::describe(program);
    SemanticArtifactAccess::record_awaits(context, program.bodies());
    auto writer = BinaryWriter();
    write_semantic_header(writer, context, SemanticArtifactKind::Program);
    writer.write(program, context);
    if (writer.error()) {
        return std::unexpected(*writer.error());
    }
    return std::move(writer).take();
}

auto decode_semir_program(std::span<const std::byte> bytes) noexcept
    -> std::expected<SemIRProgram, BinaryError> {
    auto reader = BinaryReader(bytes);
    auto context = read_semantic_header(reader, SemanticArtifactKind::Program);
    if (!context) {
        return std::unexpected(std::move(context.error()));
    }
    auto result = reader.read<SemIRProgram>(*context);
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    auto finish = reader.finish();
    if (!finish) {
        return std::unexpected(std::move(finish.error()));
    }
    if (!consumed_semantic_nodes(*context)) {
        return reader.fail("unclaimed semantic operation ownership");
    }
    if (!valid_semantic_bounds(*context, SemanticArtifactAccess::describe_bounds(*result))) {
        return reader.fail("semantic program tables disagree with their bounds");
    }
    if (!valid_provenance(result->provenance())) {
        return reader.fail("invalid semantic program source provenance");
    }
    return result;
}
