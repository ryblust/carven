module carven:semantic.analysis.validation.tables.impl;

import :semantic.analysis.validation.context;
import :semantic.hir;
import std;

template<typename Program>
SemanticVerifier<Program>::SemanticVerifier(Program source) noexcept
    : input(source) {}

template<typename Program>
auto SemanticVerifier<Program>::check_tables() noexcept
    -> std::expected<void, SemanticProgramError> {
    if (input.program().expression_controls().size() != input.program().expressions().size()
        || input.program().evaluation_effects().size() != input.program().expressions().size()
        || input.program().place_uses().size() != input.program().expressions().size()
        || input.program().try_facts().size() != input.program().expressions().size()
        || input.program().block_controls().size() != input.program().blocks().size()
        || input.program().callable_flows().size() != input.program().callables().size()
        || input.program().structure_capabilities().size() != input.program().structures().size()
        || input.program().enumeration_capabilities().size()
            != input.program().enumerations().size()
        || input.program().nominal_dependency_sets().size()
            != input.program().structures().size() + input.program().enumerations().size()) {
        static_cast<void>(
            fail(SemanticProgramErrorKind::InvalidContract, "semantic flow facts are incomplete")
        );
    } else {
        static_cast<void>(verify_canonical_tables());
    }
    if (failure.has_value()) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

template<typename Program>
auto SemanticVerifier<Program>::fail(SemanticProgramErrorKind kind, std::string message) noexcept
    -> bool {
    if (!failure.has_value()) {
        failure = SemanticProgramError {.kind = kind, .message = std::move(message)};
    }
    return false;
}

template<typename Program>
auto SemanticVerifier<Program>::type_known(HIRTypeID id) const noexcept -> bool {
    return semantic_id_known(id, input.program().types().size());
}

template<typename Program>
auto SemanticVerifier<Program>::symbol_known(SymbolID id) const noexcept -> bool {
    return semantic_id_known(id, input.program().symbol_count());
}

template<typename Program>
auto SemanticVerifier<Program>::scope_known(SemanticScopeID id) const noexcept -> bool {
    return semantic_id_known(id, input.program().scopes().size());
}

template<typename Program>
auto SemanticVerifier<Program>::scope_contains(
    SemanticScopeID owner,
    SemanticScopeID nested
) const noexcept -> bool {
    if (!scope_known(owner) || !scope_known(nested)) {
        return false;
    }
    auto current = nested;
    while (true) {
        if (current == owner) {
            return true;
        }
        const auto parent = input.program().scope(current).parent;
        if (!parent.has_value()) {
            return false;
        }
        current = *parent;
    }
}

template<typename Program>
auto SemanticVerifier<Program>::failure_members(std::span<const HIRTypeID> values) noexcept
    -> bool {
    const auto original = std::vector(values.begin(), values.end());
    if (normalize_failure_members(original) != original) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "failure set storage is not normalized"
        );
    }
    for (const auto type : values) {
        if (!type_known(type)) {
            return fail(
                SemanticProgramErrorKind::InvalidType,
                "failure contract references an unknown type"
            );
        }
        const auto& value = input.program().type(type).value;
        if (!std::holds_alternative<HIRStructTypeValue>(value)
            && !std::holds_alternative<HIREnumTypeValue>(value)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "failure contract contains a non-nominal or unknown type"
            );
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::failure_set_known(FailureSetID id) noexcept -> bool {
    return semantic_id_known(id, input.program().failure_sets().size())
        || fail(
               SemanticProgramErrorKind::InvalidContract,
               "published fact references an unknown failure set"
        );
}

template<typename Program>
auto SemanticVerifier<Program>::verify_type_value(const HIRTypeValue& value) noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const HIRBuiltinTypeValue&) static noexcept { return true; },
            [&](const HIRStructTypeValue& nominal) noexcept {
                return semantic_id_known(nominal.structure, input.program().structures().size())
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "structure type references an unknown declaration"
                    );
            },
            [&](const HIREnumTypeValue& nominal) noexcept {
                return semantic_id_known(nominal.enumeration, input.program().enumerations().size())
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "enum type references an unknown declaration"
                    );
            },
            [&](const HIRArrayTypeValue& array) noexcept {
                return type_known(array.element_type_id)
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "array type references an unknown element type"
                    );
            },
            [&](const HIRFunctionTypeValue& function) noexcept {
                return semantic_id_known(function.callable, input.program().callables().size())
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "function type references an unknown callable"
                    );
            },
            [&](const HIRFunctionRefTypeValue& function) noexcept {
                return semantic_id_known(
                           function.signature,
                           input.program().callable_signatures().size()
                       )
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "function reference type has an unknown signature"
                    );
            },
            [&](const HIRClosureTypeValue& closure) noexcept {
                return semantic_id_known(closure.callable, input.program().callables().size())
                    || fail(
                           SemanticProgramErrorKind::InvalidType,
                           "closure type references an unknown callable"
                    );
            },
            [&](const HIRErrorTypeValue&) noexcept {
                return fail(
                    SemanticProgramErrorKind::InvalidType,
                    "published semantic program contains an error type"
                );
            },
        },
        value
    );
}

template<typename Program>
template<typename Callable>
auto SemanticVerifier<Program>::verify_callable_shape(const Callable& callable) noexcept -> bool {
    if (!type_known(callable.result)) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "callable contract references an unknown result type"
        );
    }
    for (const auto& parameter : callable.parameters) {
        if (!type_known(parameter.type)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "callable parameter references an unknown type"
            );
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_callable_signature(
    const HIRCallableSignature& callable
) noexcept -> bool {
    return verify_callable_shape(callable)
        && (failure_set_known(callable.failure_set)
            || fail(
                SemanticProgramErrorKind::InvalidContract,
                "callable signature references an unknown failure set"
            ));
}

template<typename Program>
auto SemanticVerifier<Program>::verify_constant(
    std::size_t index,
    const HIRConstantFact& fact
) noexcept -> bool {
    if (!type_known(fact.type)) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "program constant references an unknown type"
        );
    }
    return std::visit(
        Overloaded {
            [&](const HIRNumericEnumConstant& value) noexcept {
                return semantic_id_known(value.enum_case, input.program().enum_cases().size())
                    || fail(
                           SemanticProgramErrorKind::InvalidReference,
                           "enum constant references an unknown case"
                    );
            },
            [&](const HIRPayloadEnumConstant& value) noexcept {
                if (!semantic_id_known(value.enum_case, input.program().enum_cases().size())) {
                    return fail(
                        SemanticProgramErrorKind::InvalidReference,
                        "payload enum constant references an unknown case"
                    );
                }
                for (const auto payload : value.payload) {
                    if (payload.index() >= index) {
                        return fail(
                            SemanticProgramErrorKind::InvalidOwnership,
                            "constant payload is cyclic or not construction-ordered"
                        );
                    }
                }
                return true;
            },
            [](const auto&) static noexcept { return true; },
        },
        fact.value
    );
}

template<typename Program>
auto SemanticVerifier<Program>::verify_scope_table() noexcept -> bool {
    for (auto index = 0uz; index < input.program().scopes().size(); ++index) {
        const auto parent = input.program().scopes()[index].parent;
        if (parent.has_value() && parent->index() >= index) {
            return fail(
                SemanticProgramErrorKind::InvalidScope,
                "lexical scope parent is cyclic or not construction-ordered"
            );
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_symbol_table() noexcept -> bool {
    if (input.program().bindings().size() != input.program().symbol_count()) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "semantic binding facts are not aligned with symbols"
        );
    }
    auto positions = std::flat_set<std::pair<SemanticScopeID, std::uint32_t>>();
    for (auto index = 0uz; index < input.program().symbol_count(); ++index) {
        const auto id = SymbolID::from_index(static_cast<std::uint32_t>(index));
        const auto& symbol = input.program().symbol(id);
        if ((symbol.type.has_value() && !type_known(*symbol.type))
            || (symbol.module_id.has_value()
                && !semantic_id_known(*symbol.module_id, input.program().modules().size()))
            || (symbol.parent.has_value() && !symbol_known(*symbol.parent))) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "program symbol contains an invalid reference"
            );
        }
        const auto& binding = input.program().binding(id);
        if (!binding.has_value()) {
            continue;
        }
        if (!symbol.type.has_value()
            || !scope_known(binding->scope)
            || !positions.emplace(binding->scope, binding->declaration_order).second) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "runtime binding has an invalid type, scope, or declaration order"
            );
        }
        if (binding->storage != SemanticBindingStorage::Owner && binding->capabilities.take) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "borrowed or compiler storage cannot support Take"
            );
        }
        if (binding->storage == SemanticBindingStorage::Owner && !binding->capabilities.take) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "owner storage must support whole-binding Take"
            );
        }
        if (binding->storage == SemanticBindingStorage::Compiler && binding->capabilities.write) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "immutable compiler storage cannot support Write"
            );
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::nominal_index(HIRNominalDeclRef declaration) const noexcept
    -> std::optional<std::size_t> {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept -> std::optional<std::size_t> {
                return semantic_id_known(id, input.program().structures().size())
                    ? std::optional<std::size_t>(id.index())
                    : std::nullopt;
            },
            [&](EnumID id) noexcept -> std::optional<std::size_t> {
                return semantic_id_known(id, input.program().enumerations().size())
                    ? std::optional<std::size_t>(input.program().structures().size() + id.index())
                    : std::nullopt;
            },
        },
        declaration
    );
}

template<typename Program>
auto SemanticVerifier<Program>::verify_nominal_containment() noexcept -> bool {
    auto adjacency = std::vector<std::vector<std::size_t>>();
    adjacency.reserve(input.program().nominal_dependency_sets().size());
    for (const auto& dependencies : input.program().nominal_dependency_sets()) {
        auto targets = std::vector<std::size_t>();
        targets.reserve(dependencies.size());
        for (const auto dependency : dependencies) {
            const auto index = nominal_index(dependency);
            if (!index.has_value()) {
                return fail(
                    SemanticProgramErrorKind::InvalidReference,
                    "nominal containment references an unknown declaration"
                );
            }
            if (std::ranges::contains(targets, *index)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "nominal containment contains a repeated direct dependency"
                );
            }
            targets.push_back(*index);
        }
        adjacency.push_back(std::move(targets));
    }
    auto states = std::vector<std::uint8_t>(adjacency.size());
    const auto acyclic = [&](this const auto& self, std::size_t node) noexcept -> bool {
        if (states[node] == 1) {
            return false;
        }
        if (states[node] == 2) {
            return true;
        }
        states[node] = 1;
        for (const auto dependency : adjacency[node]) {
            if (!self(dependency)) {
                return false;
            }
        }
        states[node] = 2;
        return true;
    };
    for (auto node = 0uz; node < adjacency.size(); ++node) {
        if (!acyclic(node)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "published nominal containment contains a by-value cycle"
            );
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_canonical_tables() noexcept -> bool {
    if (!verify_scope_table() || !verify_symbol_table() || !verify_nominal_containment()) {
        return false;
    }
    for (const auto [index, type] : std::views::enumerate(input.program().types())) {
        if (!verify_type_value(type.value)) {
            return false;
        }
        for (const auto& previous :
             input.program().types().first(static_cast<std::size_t>(index))) {
            if (previous.value == type.value) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "equivalent semantic types have multiple semantic identities"
                );
            }
        }
    }
    for (auto index = 0uz; index < input.program().constants().size(); ++index) {
        if (!verify_constant(index, input.program().constants()[index])) {
            return false;
        }
    }
    for (auto index = 0uz; index < input.program().failure_sets().size(); ++index) {
        const auto& failure_set = input.program().failure_sets()[index];
        if (!failure_members(failure_set.members)) {
            return false;
        }
        for (auto previous = 0uz; previous < index; ++previous) {
            if (input.program().failure_sets()[previous].members == failure_set.members) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "equivalent failure sets have multiple semantic identities"
                );
            }
        }
    }
    for (const auto [index, callable] :
         std::views::enumerate(input.program().callable_signatures())) {
        if (!verify_callable_signature(callable)) {
            return false;
        }
        for (const auto& previous :
             input.program().callable_signatures().first(static_cast<std::size_t>(index))) {
            if (previous == callable) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "equivalent callable signatures have multiple semantic identities"
                );
            }
        }
    }
    for (const auto [index, callable] : std::views::enumerate(input.program().callables())) {
        const auto id = CallableID::from_index(static_cast<std::uint32_t>(index));
        if (!verify_callable_shape(callable)) {
            return false;
        }
        const auto implementation_known = std::visit(
            Overloaded {
                [&](const HIRBodyImplementation& implementation) noexcept {
                    return input.program().has_body(implementation.body);
                },
                [&](const HIRCppImportImplementation& implementation) noexcept {
                    return semantic_id_known(
                        implementation.form_origin,
                        input.program().provenance().origins().size()
                    );
                },
            },
            callable.implementation
        );
        if (!implementation_known) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "callable contract has no published implementation"
            );
        }
        if (!failure_set_known(input.program().callable_flow(id).effective_failure_set)) {
            return false;
        }
    }
    return true;
}

template class SemanticVerifier<SemanticProgramView>;
template class SemanticVerifier<SemanticDraftView>;

auto validate_tables(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).check_tables();
}

auto validate_tables(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).check_tables();
}
