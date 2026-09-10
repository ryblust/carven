module carven:semantic.semir.decl.impl;

import :semantic.semir.decl;
import :support.invariant;
import std;

namespace {

auto require_owner(ProgramIdentity actual, ProgramIdentity expected, std::string_view fact) noexcept
    -> void {
    if (actual != expected) {
        invariant_violation(fact);
    }
}

auto require_provenance_owner(
    ProvenanceIdentity actual,
    ProvenanceIdentity expected,
    std::string_view fact
) noexcept -> void {
    if (actual != expected) {
        invariant_violation(fact);
    }
}

template<typename ID>
auto require_ids_owner(
    std::span<const ID> ids,
    ProgramIdentity owner,
    std::string_view fact
) noexcept -> void {
    for (const auto id : ids) {
        require_owner(id.owner(), owner, fact);
    }
}

auto validate_construction_type(ConstructionTypeRef type, ProgramIdentity owner) noexcept -> void {
    std::visit(
        [owner](const auto id) noexcept {
            using ID = std::remove_cvref_t<decltype(id)>;
            static_assert(std::same_as<ID, TypeID> || std::same_as<ID, TypeTermID>);
            require_owner(id.owner(), owner, "construction type used a foreign program");
        },
        type
    );
}

auto resolve_construction_type(
    ConstructionTypeRef type,
    ProgramIdentity owner,
    const TypeResolution& resolution
) noexcept -> TypeID {
    require_owner(resolution.owner(), owner, "declaration resolution used a foreign program");
    return resolution.resolve(type);
}

auto validate_callable_contract(
    const ConstructionCallableContract& contract,
    ProgramIdentity owner
) noexcept -> void {
    for (const auto& parameter : contract.parameters) {
        validate_construction_type(parameter.type, owner);
    }
    validate_construction_type(contract.result, owner);
    require_owner(
        contract.failures.owner(),
        owner,
        "callable failure contract used a foreign program"
    );
}

auto implementation_body_id(const CallableImplementation& implementation) noexcept
    -> std::optional<BodyID> {
    return std::visit(
        [](const auto& value) static noexcept -> std::optional<BodyID> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, FunctionBodyImplementation>
                          || std::same_as<Value, ClosureBodyImplementation>) {
                return value.body;
            } else {
                static_assert(std::same_as<Value, CppImportImplementation>);
                return std::nullopt;
            }
        },
        implementation
    );
}

} // namespace

auto callable_body_id(const CallableDeclaration& callable) noexcept -> std::optional<BodyID> {
    return implementation_body_id(callable.implementation);
}

auto cpp_import_form_origin(const CallableDeclaration& callable) noexcept
    -> std::optional<ProgramOriginID> {
    if (const auto* imported = std::get_if<CppImportImplementation>(&callable.implementation)) {
        return imported->form_origin;
    }
    return std::nullopt;
}

DeclarationStore::DeclarationStore(
    ImmutableProgramTable<ModuleDeclaration, ModuleID> modules,
    ImmutableProgramTable<FunctionDeclaration, FunctionID> functions,
    ImmutableProgramTable<StructDeclaration, StructID> structures,
    ImmutableProgramTable<EnumDeclaration, EnumID> enumerations,
    ImmutableProgramTable<EnumCaseDeclaration, EnumCaseID> enum_cases,
    ImmutableProgramTable<ModuleConstantDeclaration, ModuleConstantID> module_constants,
    ImmutableProgramTable<CallableDeclaration, CallableID> callables,
    std::map<BodyID, CallableID> body_callables
) noexcept
    : module_rows(std::move(modules)),
      function_rows(std::move(functions)),
      struct_rows(std::move(structures)),
      enum_rows(std::move(enumerations)),
      enum_case_rows(std::move(enum_cases)),
      module_constant_rows(std::move(module_constants)),
      callable_rows(std::move(callables)),
      body_callables(std::move(body_callables)) {}

auto DeclarationStore::owner() const noexcept -> ProgramIdentity {
    return module_rows.owner();
}

auto DeclarationStore::contains(ModuleID id) const noexcept -> bool {
    return module_rows.contains(id);
}

auto DeclarationStore::contains(FunctionID id) const noexcept -> bool {
    return function_rows.contains(id);
}

auto DeclarationStore::contains(StructID id) const noexcept -> bool {
    return struct_rows.contains(id);
}

auto DeclarationStore::contains(EnumID id) const noexcept -> bool {
    return enum_rows.contains(id);
}

auto DeclarationStore::contains(EnumCaseID id) const noexcept -> bool {
    return enum_case_rows.contains(id);
}

auto DeclarationStore::contains(ModuleConstantID id) const noexcept -> bool {
    return module_constant_rows.contains(id);
}

auto DeclarationStore::contains(CallableID id) const noexcept -> bool {
    return callable_rows.contains(id);
}

auto DeclarationStore::module_decl(ModuleID id) const noexcept -> const ModuleDeclaration& {
    return module_rows.get(id);
}

auto DeclarationStore::function(FunctionID id) const noexcept -> const FunctionDeclaration& {
    return function_rows.get(id);
}

auto DeclarationStore::structure(StructID id) const noexcept -> const StructDeclaration& {
    return struct_rows.get(id);
}

auto DeclarationStore::enumeration(EnumID id) const noexcept -> const EnumDeclaration& {
    return enum_rows.get(id);
}

auto DeclarationStore::enum_case(EnumCaseID id) const noexcept -> const EnumCaseDeclaration& {
    return enum_case_rows.get(id);
}

auto DeclarationStore::module_constant(ModuleConstantID id) const noexcept
    -> const ModuleConstantDeclaration& {
    return module_constant_rows.get(id);
}

auto DeclarationStore::callable(CallableID id) const noexcept -> const CallableDeclaration& {
    return callable_rows.get(id);
}

auto DeclarationStore::body_for_callable(CallableID callable) const noexcept
    -> std::optional<BodyID> {
    return callable_body_id(callable_rows.get(callable));
}

auto DeclarationStore::callable_for_body(BodyID body) const noexcept -> std::optional<CallableID> {
    require_owner(body.owner(), owner(), "callable lookup used a foreign body");
    const auto found = body_callables.find(body);
    return found == body_callables.end() ? std::nullopt : std::optional(found->second);
}

auto DeclarationStore::modules() const noexcept
    -> IDTableEntries<ModuleID, ModuleDeclaration, ProgramIdentity> {
    return module_rows.entries();
}

auto DeclarationStore::functions() const noexcept
    -> IDTableEntries<FunctionID, FunctionDeclaration, ProgramIdentity> {
    return function_rows.entries();
}

auto DeclarationStore::structures() const noexcept
    -> IDTableEntries<StructID, StructDeclaration, ProgramIdentity> {
    return struct_rows.entries();
}

auto DeclarationStore::enumerations() const noexcept
    -> IDTableEntries<EnumID, EnumDeclaration, ProgramIdentity> {
    return enum_rows.entries();
}

auto DeclarationStore::enum_cases() const noexcept
    -> IDTableEntries<EnumCaseID, EnumCaseDeclaration, ProgramIdentity> {
    return enum_case_rows.entries();
}

auto DeclarationStore::module_constants() const noexcept
    -> IDTableEntries<ModuleConstantID, ModuleConstantDeclaration, ProgramIdentity> {
    return module_constant_rows.entries();
}

auto DeclarationStore::callables() const noexcept
    -> IDTableEntries<CallableID, CallableDeclaration, ProgramIdentity> {
    return callable_rows.entries();
}

DeclarationConstructionView::DeclarationConstructionView(const DeclarationBuilder& builder) noexcept
    : declaration_builder(std::addressof(builder)) {}

auto DeclarationConstructionView::owner() const noexcept -> ProgramIdentity {
    return declaration_builder->owner();
}

auto DeclarationConstructionView::module_decl(ModuleID id) const noexcept -> ModuleDeclaration {
    return declaration_builder->modules.copy_defined(id);
}

auto DeclarationConstructionView::function(FunctionID id) const noexcept -> FunctionDeclaration {
    return declaration_builder->functions.copy_defined(id);
}

auto DeclarationConstructionView::structure(StructID id) const noexcept
    -> ConstructionStructDeclaration {
    return declaration_builder->structures.copy_defined(id);
}

auto DeclarationConstructionView::enumeration(EnumID id) const noexcept -> EnumDeclaration {
    return declaration_builder->enumerations.copy_defined(id);
}

auto DeclarationConstructionView::enum_case(EnumCaseID id) const noexcept
    -> ConstructionEnumCaseDeclaration {
    return declaration_builder->enum_cases.copy_defined(id);
}

auto DeclarationConstructionView::module_constant(ModuleConstantID id) const noexcept
    -> ModuleConstantDeclaration {
    return declaration_builder->module_constants.copy_defined(id);
}

auto DeclarationConstructionView::callable_contract(CallableID id) const noexcept
    -> ConstructionCallableContract {
    return declaration_builder->callable_contracts.copy_defined(id);
}

auto DeclarationConstructionView::callable_signature(CallableID id) const noexcept
    -> CallableSignatureID {
    declaration_builder->require_concrete();
    return declaration_builder->callable_signature_ids.copy_defined(id);
}

auto DeclarationConstructionView::callable_implementation(CallableID id) const noexcept
    -> CallableImplementation {
    declaration_builder->require_heads_finished();
    return declaration_builder->callable_implementations.copy_defined(id);
}

auto DeclarationConstructionView::module_count() const noexcept -> std::size_t {
    return declaration_builder->modules.size();
}

auto DeclarationConstructionView::function_count() const noexcept -> std::size_t {
    return declaration_builder->functions.size();
}

auto DeclarationConstructionView::struct_count() const noexcept -> std::size_t {
    return declaration_builder->structures.size();
}

auto DeclarationConstructionView::enum_count() const noexcept -> std::size_t {
    return declaration_builder->enumerations.size();
}

auto DeclarationConstructionView::enum_case_count() const noexcept -> std::size_t {
    return declaration_builder->enum_cases.size();
}

auto DeclarationConstructionView::module_constant_count() const noexcept -> std::size_t {
    return declaration_builder->module_constants.size();
}

auto DeclarationConstructionView::callable_count() const noexcept -> std::size_t {
    return declaration_builder->callable_contracts.size();
}

auto DeclarationConstructionView::module_ids() const noexcept -> std::vector<ModuleID> {
    return declaration_builder->modules.ids();
}

auto DeclarationConstructionView::function_ids() const noexcept -> std::vector<FunctionID> {
    return declaration_builder->functions.ids();
}

auto DeclarationConstructionView::struct_ids() const noexcept -> std::vector<StructID> {
    return declaration_builder->structures.ids();
}

auto DeclarationConstructionView::enum_ids() const noexcept -> std::vector<EnumID> {
    return declaration_builder->enumerations.ids();
}

auto DeclarationConstructionView::enum_case_ids() const noexcept -> std::vector<EnumCaseID> {
    return declaration_builder->enum_cases.ids();
}

auto DeclarationConstructionView::module_constant_ids() const noexcept
    -> std::vector<ModuleConstantID> {
    return declaration_builder->module_constants.ids();
}

auto DeclarationConstructionView::callable_ids() const noexcept -> std::vector<CallableID> {
    return declaration_builder->callable_order;
}

auto DeclarationConstructionView::callable_contract_defined(CallableID id) const noexcept -> bool {
    return declaration_builder->callable_contracts.is_defined(id);
}

auto DeclarationConstructionView::callable_contracts_complete() const noexcept -> bool {
    return declaration_builder->callable_contracts.all_defined();
}

auto DeclarationConstructionView::callable_implementations_complete() const noexcept -> bool {
    return declaration_builder->callable_implementations.all_defined();
}

DeclarationBuilder::DeclarationBuilder(
    ProgramIdentity owner,
    ProvenanceIdentity provenance
) noexcept
    : program_identity(owner),
      provenance_identity(provenance),
      state(State::Reserving),
      modules(owner),
      functions(owner),
      structures(owner),
      enumerations(owner),
      enum_cases(owner),
      module_constants(owner),
      callable_contracts(owner),
      callable_signature_ids(owner),
      callable_implementations(owner),
      callable_order() {}

auto DeclarationBuilder::owner() const noexcept -> ProgramIdentity {
    return program_identity;
}

auto DeclarationBuilder::reserve_module() noexcept -> ModuleID {
    require_reserving();
    return modules.reserve();
}

auto DeclarationBuilder::reserve_function() noexcept -> FunctionID {
    require_reserving();
    return functions.reserve();
}

auto DeclarationBuilder::reserve_struct() noexcept -> StructID {
    require_reserving();
    return structures.reserve();
}

auto DeclarationBuilder::reserve_enum() noexcept -> EnumID {
    require_reserving();
    return enumerations.reserve();
}

auto DeclarationBuilder::reserve_enum_case() noexcept -> EnumCaseID {
    require_reserving();
    return enum_cases.reserve();
}

auto DeclarationBuilder::reserve_module_constant() noexcept -> ModuleConstantID {
    require_reserving();
    return module_constants.reserve();
}

auto DeclarationBuilder::reserve_callable() noexcept -> CallableID {
    require_reserving();
    return reserve_callable_pair();
}

auto DeclarationBuilder::reserve_callable_pair() noexcept -> CallableID {
    const auto contract_id = callable_contracts.reserve();
    const auto signature_id = callable_signature_ids.reserve();
    const auto implementation_id = callable_implementations.reserve();
    if (contract_id != signature_id || contract_id != implementation_id) {
        invariant_violation("callable construction identities diverged");
    }
    callable_order.push_back(contract_id);
    return contract_id;
}

auto DeclarationBuilder::define(ModuleID id, ModuleDeclaration declaration) noexcept -> void {
    require_reserving();
    require_provenance_owner(
        declaration.provenance_module.owner(),
        provenance_identity,
        "module declaration used a foreign provenance module"
    );
    require_provenance_owner(
        declaration.origin.owner(),
        provenance_identity,
        "module declaration used a foreign origin"
    );
    for (const auto& header : declaration.cpp_headers) {
        if (header.namespace_opening) {
            const auto& binding = *header.namespace_opening;
            if (binding.components.empty()) {
                invariant_violation("C++ namespace opening has no name");
            }
            require_provenance_owner(
                binding.origin.owner(),
                provenance_identity,
                "C++ namespace opening used a foreign origin"
            );
            for (const auto component : binding.components) {
                require_provenance_owner(
                    component.owner(),
                    provenance_identity,
                    "C++ namespace opening used a foreign spelling"
                );
            }
        }
        require_provenance_owner(
            header.name.owner(),
            provenance_identity,
            "C++ header dependency used a foreign spelling"
        );
        require_provenance_owner(
            header.origin.owner(),
            provenance_identity,
            "C++ header dependency used a foreign origin"
        );
    }
    for (const auto& fragment : declaration.cpp_source_fragments) {
        require_provenance_owner(
            fragment.payload_origin.owner(),
            provenance_identity,
            "C++ source fragment used a foreign origin"
        );
    }
    for (const auto& item : declaration.items) {
        std::visit(
            [this](const auto item_id) noexcept {
                using ID = std::remove_cvref_t<decltype(item_id)>;
                static_assert(
                    std::same_as<ID, FunctionID>
                    || std::same_as<ID, StructID>
                    || std::same_as<ID, EnumID>
                    || std::same_as<ID, ModuleConstantID>
                    || std::same_as<ID, TestID>
                );
                require_owner(item_id.owner(), program_identity, "module used a foreign item");
            },
            item
        );
    }
    modules.define(id, std::move(declaration));
}

auto DeclarationBuilder::define(FunctionID id, FunctionDeclaration declaration) noexcept -> void {
    require_reserving();
    require_owner(
        declaration.module_id.owner(),
        program_identity,
        "function used a foreign module"
    );
    require_owner(
        declaration.callable.owner(),
        program_identity,
        "function used a foreign callable"
    );
    require_provenance_owner(
        declaration.name.owner(),
        provenance_identity,
        "function used a foreign spelling"
    );
    require_provenance_owner(
        declaration.origin.owner(),
        provenance_identity,
        "function used a foreign origin"
    );
    if (declaration.cpp_export_origin.has_value()) {
        require_provenance_owner(
            declaration.cpp_export_origin->owner(),
            provenance_identity,
            "function C++ export used a foreign origin"
        );
    }
    functions.define(id, declaration);
}

auto DeclarationBuilder::define(StructID id, ConstructionStructDeclaration declaration) noexcept
    -> void {
    require_reserving();
    require_owner(
        declaration.module_id.owner(),
        program_identity,
        "structure used a foreign module"
    );
    require_provenance_owner(
        declaration.name.owner(),
        provenance_identity,
        "structure used a foreign spelling"
    );
    require_provenance_owner(
        declaration.origin.owner(),
        provenance_identity,
        "structure used a foreign origin"
    );
    for (const auto& field : declaration.fields) {
        validate_construction_type(field.type, program_identity);
        require_provenance_owner(
            field.name.owner(),
            provenance_identity,
            "structure field used a foreign spelling"
        );
        require_provenance_owner(
            field.origin.owner(),
            provenance_identity,
            "structure field used a foreign origin"
        );
    }
    structures.define(id, std::move(declaration));
}

auto DeclarationBuilder::define(EnumID id, EnumDeclaration declaration) noexcept -> void {
    require_reserving();
    require_owner(declaration.module_id.owner(), program_identity, "enum used a foreign module");
    require_provenance_owner(
        declaration.name.owner(),
        provenance_identity,
        "enum used a foreign spelling"
    );
    require_provenance_owner(
        declaration.origin.owner(),
        provenance_identity,
        "enum used a foreign origin"
    );
    require_ids_owner(
        std::span<const EnumCaseID>(declaration.cases),
        program_identity,
        "enum used a foreign case"
    );
    std::visit(
        [this](const auto& representation) noexcept {
            using Representation = std::remove_cvref_t<decltype(representation)>;
            if constexpr (std::same_as<Representation, NumericEnumRepresentation>) {
                require_owner(
                    representation.underlying_type.owner(),
                    program_identity,
                    "enum underlying type used a foreign program"
                );
            } else {
                static_assert(std::same_as<Representation, PayloadEnumRepresentation>);
            }
        },
        declaration.representation
    );
    enumerations.define(id, std::move(declaration));
}

auto DeclarationBuilder::define(EnumCaseID id, ConstructionEnumCaseDeclaration declaration) noexcept
    -> void {
    require_reserving();
    require_owner(declaration.owner.owner(), program_identity, "enum case used a foreign enum");
    require_provenance_owner(
        declaration.name.owner(),
        provenance_identity,
        "enum case used a foreign spelling"
    );
    require_provenance_owner(
        declaration.origin.owner(),
        provenance_identity,
        "enum case used a foreign origin"
    );
    for (const auto type : declaration.payload_types) {
        validate_construction_type(type, program_identity);
    }
    if (declaration.constant.has_value()) {
        require_owner(
            declaration.constant->owner(),
            program_identity,
            "enum case used a foreign constant"
        );
    }
    enum_cases.define(id, std::move(declaration));
}

auto DeclarationBuilder::define(ModuleConstantID id, ModuleConstantDeclaration declaration) noexcept
    -> void {
    require_reserving();
    require_owner(
        declaration.module_id.owner(),
        program_identity,
        "module constant used a foreign module"
    );
    require_provenance_owner(
        declaration.name.owner(),
        provenance_identity,
        "module constant used a foreign spelling"
    );
    require_provenance_owner(
        declaration.origin.owner(),
        provenance_identity,
        "module constant used a foreign origin"
    );
    require_owner(
        declaration.value.owner(),
        program_identity,
        "module constant used a foreign constant fact"
    );
    module_constants.define(id, declaration);
}

auto DeclarationBuilder::define_callable_contract(
    CallableID id,
    ConstructionCallableContract contract
) noexcept -> void {
    validate_callable_contract(contract, program_identity);
    callable_contracts.define(id, std::move(contract));
}

auto DeclarationBuilder::finish_heads() noexcept -> DeclarationConstructionView {
    require_reserving();
    require_heads_defined();
    state = State::BuildingCallables;
    return DeclarationConstructionView(*this);
}

auto DeclarationBuilder::construction_view() const noexcept -> DeclarationConstructionView {
    require_heads_finished();
    return DeclarationConstructionView(*this);
}

auto DeclarationBuilder::append_body_callable(ConstructionCallableContract contract) noexcept
    -> CallableID {
    require_building_callables();
    const auto id = reserve_callable_pair();
    define_callable_contract(id, std::move(contract));
    return id;
}

auto DeclarationBuilder::define_callable_signature(
    CallableID id,
    CallableSignatureID signature
) noexcept -> void {
    require_building_callables();
    require_owner(signature.owner(), program_identity, "callable used a foreign signature");
    callable_signature_ids.define(id, signature);
}

auto DeclarationBuilder::finish_callable_signatures() noexcept -> void {
    require_building_callables();
    if (!callable_signature_ids.all_defined()) {
        invariant_violation("callable signature resolution left an undefined callable");
    }
    state = State::Concrete;
}

auto DeclarationBuilder::complete_callable(
    CallableID id,
    CallableImplementation implementation
) noexcept -> void {
    require_heads_finished();
    std::visit(
        [this](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, FunctionBodyImplementation>
                          || std::same_as<Value, ClosureBodyImplementation>) {
                require_owner(value.body.owner(), program_identity, "callable used a foreign body");
            } else {
                static_assert(std::same_as<Value, CppImportImplementation>);
                require_provenance_owner(
                    value.form_origin.owner(),
                    provenance_identity,
                    "C++ import callable used a foreign origin"
                );
            }
        },
        implementation
    );
    callable_implementations.define(id, implementation);
    if (const auto body = implementation_body_id(implementation)) {
        if (!body_callables.emplace(*body, id).second) {
            invariant_violation("body was assigned to more than one callable");
        }
    }
}

auto DeclarationBuilder::seal(const TypeResolution& type_resolution) && noexcept
    -> DeclarationStore {
    require_concrete();
    if (type_resolution.owner() != program_identity) {
        invariant_violation("declaration seal used a foreign type resolution");
    }
    if (!callable_implementations.all_defined()) {
        invariant_violation(
            "declaration store was sealed with incomplete callable implementations"
        );
    }

    auto final_structures = MutableProgramTable<StructDeclaration, StructID>(program_identity);
    const auto construction_structures = std::move(structures).seal();
    for (const auto [id, declaration] : construction_structures.entries()) {
        auto fields = std::vector<StructField>();
        fields.reserve(declaration.fields.size());
        for (const auto& field : declaration.fields) {
            fields.push_back(
                StructField {
                    .name = field.name,
                    .type =
                        resolve_construction_type(field.type, program_identity, type_resolution),
                    .origin = field.origin,
                }
            );
        }
        const auto final_id = final_structures.add(
            StructDeclaration {
                .module_id = declaration.module_id,
                .name = declaration.name,
                .origin = declaration.origin,
                .visibility = declaration.visibility,
                .fields = std::move(fields),
                .capabilities = declaration.capabilities,
            }
        );
        if (final_id != id) {
            invariant_violation("structure declaration identity changed during sealing");
        }
    }

    auto final_enum_cases = MutableProgramTable<EnumCaseDeclaration, EnumCaseID>(program_identity);
    const auto construction_enum_cases = std::move(enum_cases).seal();
    for (const auto [id, declaration] : construction_enum_cases.entries()) {
        auto payload_types = std::vector<TypeID>();
        payload_types.reserve(declaration.payload_types.size());
        for (const auto type : declaration.payload_types) {
            payload_types.push_back(
                resolve_construction_type(type, program_identity, type_resolution)
            );
        }
        const auto final_id = final_enum_cases.add(
            EnumCaseDeclaration {
                .owner = declaration.owner,
                .name = declaration.name,
                .origin = declaration.origin,
                .payload_types = std::move(payload_types),
                .constant = declaration.constant,
            }
        );
        if (final_id != id) {
            invariant_violation("enum-case declaration identity changed during sealing");
        }
    }

    auto final_callables = MutableProgramTable<CallableDeclaration, CallableID>(program_identity);
    const auto signature_rows = std::move(callable_signature_ids).seal();
    const auto implementation_rows = std::move(callable_implementations).seal();
    for (const auto callable_id : callable_order) {
        const auto final_id = final_callables.add(
            CallableDeclaration {
                .signature = signature_rows.get(callable_id),
                .implementation = implementation_rows.get(callable_id),
            }
        );
        if (final_id != callable_id) {
            invariant_violation("callable identity changed during declaration sealing");
        }
    }
    static_cast<void>(std::move(callable_contracts).seal());
    return DeclarationStore(
        std::move(modules).seal(),
        std::move(functions).seal(),
        std::move(final_structures).seal(),
        std::move(enumerations).seal(),
        std::move(final_enum_cases).seal(),
        std::move(module_constants).seal(),
        std::move(final_callables).seal(),
        std::move(body_callables)
    );
}

auto DeclarationBuilder::require_reserving() const noexcept -> void {
    if (state != State::Reserving) {
        invariant_violation("named declaration storage was reopened after resolution");
    }
}

auto DeclarationBuilder::require_building_callables() const noexcept -> void {
    if (state != State::BuildingCallables) {
        invariant_violation("callable construction operation used outside callable construction");
    }
}

auto DeclarationBuilder::require_heads_finished() const noexcept -> void {
    if (state == State::Reserving) {
        invariant_violation("declaration operation used before heads completed");
    }
}

auto DeclarationBuilder::require_concrete() const noexcept -> void {
    if (state != State::Concrete) {
        invariant_violation("concrete declaration operation used before signature resolution");
    }
}

auto DeclarationBuilder::require_heads_defined() const noexcept -> void {
    if (!modules.all_defined()
        || !functions.all_defined()
        || !structures.all_defined()
        || !enumerations.all_defined()
        || !enum_cases.all_defined()
        || !module_constants.all_defined()) {
        invariant_violation("declaration heads completed with an undefined shell");
    }
}
