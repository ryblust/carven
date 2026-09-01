module carven:semantic.analysis.session.decl.impl;

import :semantic.analysis.session;
import :support.invariant;
import std;

SemanticEntityReservations::SemanticEntityReservations(SemanticDraft& source) noexcept
    : construction(std::addressof(source)) {}

auto SemanticEntityReservations::reserve_symbol() noexcept -> SymbolID {
    return construction->reserve_symbol();
}
auto SemanticEntityReservations::reserve_function() noexcept -> FunctionID {
    return construction->reserve_function();
}

auto SemanticEntityReservations::reserve_struct() noexcept -> StructID {
    return construction->reserve_struct();
}

auto SemanticEntityReservations::reserve_enum() noexcept -> EnumID {
    return construction->reserve_enum();
}

auto SemanticEntityReservations::reserve_enum_case() noexcept -> EnumCaseID {
    return construction->reserve_enum_case();
}

SemanticDeclarationCapabilities::SemanticDeclarationCapabilities(SemanticDraft& draft) noexcept
    : semantic_draft(std::addressof(draft)) {}

auto SemanticDeclarationCapabilities::define(FunctionID id, HIRFunctionDecl declaration) noexcept
    -> void {
    semantic_draft->define_function_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::define(StructID id, HIRStructDecl declaration) noexcept
    -> void {
    semantic_draft->define_struct_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::define(EnumID id, HIREnumDecl declaration) noexcept -> void {
    semantic_draft->define_enum_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::define(
    EnumCaseID id,
    SemanticEnumCaseContract declaration
) noexcept -> void {
    semantic_draft->define_enum_case_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::function(FunctionID id) const noexcept
    -> const HIRFunctionDecl& {
    return semantic_draft->function(id);
}

auto SemanticDeclarationCapabilities::structure(StructID id) const noexcept
    -> const HIRStructDecl& {
    return semantic_draft->structure(id);
}

auto SemanticDeclarationCapabilities::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return semantic_draft->enumeration(id);
}

auto SemanticDeclarationCapabilities::enum_case(EnumCaseID id) const noexcept
    -> SemanticEnumCaseView {
    return semantic_draft->enum_case_contract_view(id);
}

auto SemanticDeclarationCapabilities::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return semantic_draft->symbol(id);
}

auto SemanticDeclarationCapabilities::type(HIRTypeID id) const noexcept -> const HIRType& {
    return semantic_draft->type(id);
}

auto SemanticDeclarationCapabilities::complete_enum_case(
    EnumCaseID id,
    std::optional<HIRConstantID> constant
) noexcept -> void {
    semantic_draft->complete_enum_case_contract(id, constant);
}

auto SemanticDeclarationCapabilities::seal_declarations(
    std::vector<HIRNominalCapabilities> structures,
    std::vector<HIRNominalCapabilities> enumerations
) noexcept -> void {
    semantic_draft->freeze_nominal_capabilities(std::move(structures), std::move(enumerations));
    semantic_draft->freeze_declaration_contracts();
}

auto SemanticDraft::reserve_symbol() noexcept -> SymbolID {
    if (reserved_symbol_count == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic symbols exhausted their 32-bit identity space");
    }
    const auto id = SymbolID::from_index(static_cast<std::uint32_t>(reserved_symbol_count));
    ++reserved_symbol_count;
    return id;
}

auto SemanticDraft::reserve_function() noexcept -> FunctionID {
    if (function_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic functions exhausted their 32-bit identity space");
    }
    const auto id = FunctionID::from_index(static_cast<std::uint32_t>(function_slots.size()));
    function_slots.emplace_back();
    return id;
}

auto SemanticDraft::reserve_struct() noexcept -> StructID {
    if (struct_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic structures exhausted their 32-bit identity space");
    }
    const auto id = StructID::from_index(static_cast<std::uint32_t>(struct_slots.size()));
    struct_slots.emplace_back();
    return id;
}

auto SemanticDraft::reserve_enum() noexcept -> EnumID {
    if (enum_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic enumerations exhausted their 32-bit identity space");
    }
    const auto id = EnumID::from_index(static_cast<std::uint32_t>(enum_slots.size()));
    enum_slots.emplace_back();
    return id;
}

auto SemanticDraft::reserve_enum_case() noexcept -> EnumCaseID {
    if (enum_case_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic enum cases exhausted their 32-bit identity space");
    }
    const auto id = EnumCaseID::from_index(static_cast<std::uint32_t>(enum_case_slots.size()));
    enum_case_slots.emplace_back();
    return id;
}

auto SemanticDraft::define_function_contract(FunctionID id, HIRFunctionDecl declaration) noexcept
    -> void {
    if (id.index() >= function_slots.size() || function_slots[id.index()].has_value()) {
        invariant_violation("function contract was not defined exactly once in its reserved slot");
    }
    function_slots[id.index()] = std::move(declaration);
}

auto SemanticDraft::define_struct_contract(StructID id, HIRStructDecl declaration) noexcept
    -> void {
    if (id.index() >= struct_slots.size() || struct_slots[id.index()].has_value()) {
        invariant_violation("struct contract was not defined exactly once in its reserved slot");
    }
    struct_slots[id.index()] = std::move(declaration);
}

auto SemanticDraft::define_enum_contract(EnumID id, HIREnumDecl declaration) noexcept -> void {
    if (id.index() >= enum_slots.size() || enum_slots[id.index()].has_value()) {
        invariant_violation("enum contract was not defined exactly once in its reserved slot");
    }
    enum_slots[id.index()] = std::move(declaration);
}

auto SemanticDraft::define_enum_case_contract(
    EnumCaseID id,
    SemanticEnumCaseContract declaration
) noexcept -> void {
    if (id.index() >= enum_case_slots.size() || enum_case_slots[id.index()].has_value()) {
        invariant_violation("enum-case contract was not defined exactly once in its reserved slot");
    }
    enum_case_slots[id.index()] = EnumCaseSlot {
        .contract = std::move(declaration),
        .constant = std::nullopt,
    };
}

auto SemanticDraft::complete_enum_case_contract(
    EnumCaseID id,
    std::optional<HIRConstantID> constant
) noexcept -> void {
    if (id.index() >= enum_case_slots.size() || !enum_case_slots[id.index()].has_value()) {
        invariant_violation("enum-case constant completion used an unresolved contract");
    }
    auto& completion = enum_case_slots[id.index()]->constant;
    if (completion.has_value()) {
        invariant_violation("enum-case constant was completed more than once");
    }
    completion = constant;
}

auto SemanticDraft::enum_case_contract_view(EnumCaseID id) const noexcept -> SemanticEnumCaseView {
    if (storage.enum_cases.contains(id)) {
        const auto& declaration = storage.enum_cases.get(id);
        return {
            .owner = declaration.owner,
            .name = declaration.name,
            .payload_types = declaration.payload_types,
            .symbol = declaration.symbol,
            .origin = declaration.origin,
        };
    }
    if (id.index() >= enum_case_slots.size() || !enum_case_slots[id.index()].has_value()) {
        invariant_violation("enum-case contract lookup used an unresolved reserved identity");
    }
    const auto& declaration = enum_case_slots[id.index()]->contract;
    return {
        .owner = declaration.owner,
        .name = declaration.name,
        .payload_types = declaration.payload_types,
        .symbol = declaration.symbol,
        .origin = declaration.origin,
    };
}

auto SemanticDraft::freeze_nominal_capabilities(
    std::vector<HIRNominalCapabilities> structures,
    std::vector<HIRNominalCapabilities> enumerations
) noexcept -> void {
    if (!storage.struct_capabilities.empty()
        || !storage.enum_capabilities.empty()
        || structures.size() != struct_slots.size()
        || enumerations.size() != enum_slots.size()) {
        invariant_violation("nominal capabilities were not frozen exactly once and completely");
    }
    for (auto& capabilities : structures) {
        storage.struct_capabilities.add(std::move(capabilities));
    }
    for (auto& capabilities : enumerations) {
        storage.enum_capabilities.add(std::move(capabilities));
    }
}

auto SemanticDraft::freeze_declaration_contracts() noexcept -> void {
    if (!storage.functions.empty()
        || !storage.structures.empty()
        || !storage.enumerations.empty()
        || !storage.enum_cases.empty()) {
        invariant_violation("declaration contracts were frozen more than once");
    }
    const auto move_slots = [](auto& slots, auto& destination) static noexcept {
        for (auto& slot : slots) {
            if (!slot.has_value()) {
                invariant_violation("required declaration contract slot is unresolved");
            }
            destination.add(std::move(*slot));
        }
        slots.clear();
    };
    move_slots(function_slots, storage.functions);
    move_slots(struct_slots, storage.structures);
    move_slots(enum_slots, storage.enumerations);
    for (auto& slot : enum_case_slots) {
        if (!slot.has_value() || !slot->constant.has_value()) {
            invariant_violation("required enum-case contract slot is incomplete");
        }
        auto& contract = slot->contract;
        storage.enum_cases.add({
            .owner = contract.owner,
            .name = contract.name,
            .payload_types = std::move(contract.payload_types),
            .constant = std::move(*slot->constant),
            .symbol = contract.symbol,
            .origin = contract.origin,
        });
    }
    enum_case_slots.clear();
}

auto SemanticDraft::define_callable_body(
    CallableID callable,
    std::vector<HIRParameter> parameters,
    SemanticScopeID scope,
    HIRBlockID root
) noexcept -> void {
    if (!storage.callables.contains(callable)) {
        invariant_violation("callable body definition references an unknown callable");
    }
    const auto body_id = storage.callables.get(callable).body;
    auto& slot = body_slots[body_id.index()];
    if (slot.has_value()) {
        invariant_violation("semantic body was defined more than once");
    }
    slot = HIRBody {
        .parameters = std::move(parameters),
        .scope = scope,
        .root = root,
    };
}

auto SemanticDraft::append_test(
    ProgramOriginID origin,
    ProgramSpellingID name,
    SemanticScopeID scope,
    HIRBlockID root
) noexcept -> TestID {
    if (body_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic bodies exhausted their 32-bit identity space");
    }
    const auto body_id = BodyID::from_index(static_cast<std::uint32_t>(body_slots.size()));
    body_slots.emplace_back();
    const auto test_id = storage.tests.add({
        .origin = origin,
        .name = name,
        .body = body_id,
    });
    body_slots[body_id.index()] = HIRBody {
        .parameters = {},
        .scope = scope,
        .root = root,
    };
    return test_id;
}
