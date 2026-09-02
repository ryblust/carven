module carven:semantic.analysis.validation.relations.impl;

import :semantic.analysis.validation.context;
import :semantic.hir;
import std;

template<typename Program>
auto SemanticVerifier<Program>::check_relations() noexcept
    -> std::expected<void, SemanticProgramError> {
    static_cast<void>(visit_modules());
    if (failure.has_value()) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

template<typename Program>
auto SemanticVerifier<Program>::binding_place(
    const HIRBindingTarget& target,
    HIRTypeID type,
    SemanticScopeID scope
) noexcept -> bool {
    const auto* named = std::get_if<HIRNamedBindingTarget>(&target);
    if (named == nullptr) {
        return true;
    }
    if (!symbol_known(named->symbol)) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "binding target references an unknown symbol"
        );
    }
    const auto& symbol = input.program().symbol(named->symbol);
    const auto& binding = input.program().binding(named->symbol);
    if (symbol.type != type || !binding.has_value()) {
        return fail(
            SemanticProgramErrorKind::InvalidPlace,
            "binding target symbol has no matching runtime binding facts"
        );
    }
    return binding->scope == scope
        || fail(
               SemanticProgramErrorKind::InvalidScope,
               "binding is owned by the wrong lexical scope"
        );
}

template<typename Program>
auto SemanticVerifier<Program>::projected_type(const SemanticPlaceUse& use) noexcept
    -> std::optional<HIRTypeID> {
    if (!symbol_known(use.root) || !input.program().binding(use.root).has_value()) {
        return std::nullopt;
    }
    const auto root_type = input.program().symbol(use.root).type;
    if (!root_type.has_value()) {
        return std::nullopt;
    }
    auto current = *root_type;
    for (const auto& projection : use.projections) {
        const auto next = std::visit(
            Overloaded {
                [&](const SemanticFieldProjection& field) noexcept -> std::optional<HIRTypeID> {
                    const auto* nominal =
                        std::get_if<HIRStructTypeValue>(&input.program().type(current).value);
                    if (nominal == nullptr
                        || nominal->structure != field.owner
                        || !semantic_id_known(field.owner, input.program().structures().size())) {
                        return std::nullopt;
                    }
                    const auto& structure = input.program().structure(field.owner);
                    if (field.field_index >= structure.fields.size()) {
                        return std::nullopt;
                    }
                    return structure.fields[field.field_index].type;
                },
                [&](const SemanticIndexProjection&) noexcept -> std::optional<HIRTypeID> {
                    const auto* array =
                        std::get_if<HIRArrayTypeValue>(&input.program().type(current).value);
                    return array != nullptr ? std::optional(array->element_type_id) : std::nullopt;
                },
            },
            projection
        );
        if (!next.has_value()) {
            return std::nullopt;
        }
        current = *next;
    }
    return current;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_place_use(
    const HIRExpr& expression,
    const std::optional<SemanticPlaceUse>& place_use,
    SemanticScopeID scope
) noexcept -> bool {
    if (!place_use.has_value()) {
        return true;
    }
    const auto& use = *place_use;
    if (!symbol_known(use.root) || !input.program().binding(use.root).has_value()) {
        return fail(
            SemanticProgramErrorKind::InvalidPlace,
            "expression references an unknown runtime binding"
        );
    }
    const auto& binding = *input.program().binding(use.root);
    if (!scope_contains(binding.scope, scope)) {
        return fail(
            SemanticProgramErrorKind::InvalidScope,
            "expression accesses a binding outside its lexical scope"
        );
    }
    const auto writes =
        use.access == SemanticPlaceAccess::Write || use.access == SemanticPlaceAccess::ReadWrite;
    if (writes && !binding.capabilities.write) {
        return fail(
            SemanticProgramErrorKind::InvalidPlace,
            "expression uses Write without binding capability"
        );
    }
    if (use.access == SemanticPlaceAccess::Take
        && (!binding.capabilities.take || !use.projections.empty())) {
        return fail(
            SemanticProgramErrorKind::InvalidPlace,
            "expression uses Take on a non-owner or projected place"
        );
    }
    const auto result = projected_type(use);
    return result == expression.type
        || fail(
               SemanticProgramErrorKind::InvalidPlace,
               "place projection result does not match expression type"
        );
}

template<typename Program>
auto SemanticVerifier<Program>::visit_pattern(
    HIRPatternID id,
    SemanticScopeID scope,
    HIRTypeID expected
) noexcept -> bool {
    if (!semantic_id_known(id, input.program().patterns().size())) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "program pattern occurrence has an invalid identity"
        );
    }
    const auto& pattern = input.program().pattern(id);
    const auto valid = std::visit(
        Overloaded {
            [](const HIRWildcardPattern&) static noexcept { return true; },
            [&](const HIRLiteralPattern& literal) noexcept { return literal.type == expected; },
            [&](const HIROrPattern& alternatives) noexcept {
                if (alternatives.type != expected || alternatives.alternatives.empty()) {
                    return false;
                }
                for (const auto child : alternatives.alternatives) {
                    if (!visit_pattern(child, scope, expected)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const HIRTypeConstraintPattern& constraint) noexcept {
                return type_known(constraint.type);
            },
            [&](const HIRBindingPattern& binding) noexcept {
                return binding.type == expected
                    && binding_place(binding.target, binding.type, scope);
            },
            [&](const HIRCasePattern& enum_case) noexcept {
                if (!semantic_id_known(enum_case.enum_case, input.program().enum_cases().size())) {
                    return false;
                }
                const auto& declared_case = input.program().enum_case(enum_case.enum_case);
                const auto* nominal =
                    std::get_if<HIREnumTypeValue>(&input.program().type(expected).value);
                if (nominal == nullptr || nominal->enumeration != declared_case.owner) {
                    return false;
                }
                if (declared_case.payload_types.size() != enum_case.payload.size()) {
                    return false;
                }
                for (auto index = 0uz; index < enum_case.payload.size(); ++index) {
                    if (!visit_pattern(
                            enum_case.payload[index],
                            scope,
                            declared_case.payload_types[index]
                        )) {
                        return false;
                    }
                }
                return true;
            },
        },
        pattern.value
    );
    if (!valid) {
        return failure.has_value() ? false
                                   : fail(
                                         SemanticProgramErrorKind::InvalidType,
                                         "typed program pattern is inconsistent with its subject"
                                     );
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::visit_match_arm(
    const HIRMatchArm& arm,
    HIRTypeID subject,
    SemanticScopeID owner_scope
) noexcept -> bool {
    if (!scope_known(arm.scope)
        || !scope_contains(owner_scope, arm.scope)
        || !visit_pattern(arm.pattern, arm.scope, subject)) {
        return false;
    }
    return (!arm.guard.has_value() || visit_expression(*arm.guard, arm.scope))
        && visit_block(arm.body, arm.scope);
}

template<typename Program>
auto SemanticVerifier<Program>::visit_catch_arm(
    const HIRCatchArm& arm,
    const HIRCatchFacts& facts,
    std::span<const HIRTypeID> protected_failures,
    SemanticScopeID owner_scope
) noexcept -> bool {
    if (!scope_known(arm.scope)
        || !scope_contains(owner_scope, arm.scope)
        || arm.origin.index() >= input.program().provenance().origins().size()
        || !failure_set_known(facts.accepted_failure_set)) {
        return false;
    }
    const auto& accepted = input.program().failure_set(facts.accepted_failure_set).members;
    for (const auto failure : accepted) {
        if (!std::ranges::contains(protected_failures, failure)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "catch arm accepts a failure absent from its protected body"
            );
        }
    }
    for (auto index = 0uz; index < arm.alternatives.size(); ++index) {
        const auto& alternative = arm.alternatives[index];
        if (alternative.origin.index() >= input.program().provenance().origins().size()
            || (alternative.type.has_value() && !type_known(*alternative.type))) {
            return false;
        }
        if (alternative.inner.has_value()
            && (!alternative.type.has_value()
                || !visit_pattern(*alternative.inner, arm.scope, *alternative.type))) {
            return false;
        }
    }
    if (!std::ranges::is_sorted(facts.reachable_alternative_indices)
        || std::ranges::adjacent_find(facts.reachable_alternative_indices)
            != facts.reachable_alternative_indices.end()) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "catch reachable alternatives are not sorted and unique"
        );
    }
    for (const auto index : facts.reachable_alternative_indices) {
        if (index >= arm.alternatives.size()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "catch reachable alternative is out of range"
            );
        }
        const auto& alternative = arm.alternatives[index];
        if (alternative.type.has_value() && !std::ranges::contains(accepted, *alternative.type)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "catch reachable alternative does not accept its failure type"
            );
        }
    }
    for (const auto failure : accepted) {
        const auto represented = std::ranges::any_of(
            facts.reachable_alternative_indices,
            [&](std::uint32_t index) noexcept {
                const auto& alternative = arm.alternatives[index];
                return !alternative.type.has_value() || *alternative.type == failure;
            }
        );
        if (!represented) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "catch accepted failure has no reachable alternative"
            );
        }
    }
    if (accepted.empty() && !facts.reachable_alternative_indices.empty()) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "empty catch arm acceptance has reachable alternatives"
        );
    }
    return (!arm.guard.has_value() || visit_expression(*arm.guard, arm.scope))
        && visit_block(arm.body, arm.scope);
}

template<typename Program>
auto SemanticVerifier<Program>::expression_callable(HIRTypeID type) const noexcept
    -> std::optional<SemanticCallableContract> {
    const auto& value = input.program().type(type).value;
    if (const auto* function = std::get_if<HIRFunctionTypeValue>(&value)) {
        const auto& callable = input.program().callable(function->callable);
        return SemanticCallableContract {
            .parameters = callable.parameters,
            .result = callable.result,
            .failure_set = input.program().callable_flow(function->callable).effective_failure_set,
        };
    }
    if (const auto* reference = std::get_if<HIRFunctionRefTypeValue>(&value)) {
        const auto& callable = input.program().callable_signature(reference->signature);
        return SemanticCallableContract {
            .parameters = callable.parameters,
            .result = callable.result,
            .failure_set = callable.failure_set,
        };
    }
    if (const auto* closure = std::get_if<HIRClosureTypeValue>(&value)) {
        const auto& callable = input.program().callable(closure->callable);
        return SemanticCallableContract {
            .parameters = callable.parameters,
            .result = callable.result,
            .failure_set = input.program().callable_flow(closure->callable).effective_failure_set,
        };
    }
    return std::nullopt;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_call(
    const HIRExpr& expression,
    const HIRExpressionControl& control,
    const HIRCallExpr& call,
    SemanticScopeID scope
) noexcept -> bool {
    if (!visit_expression(call.callee, scope)) {
        return false;
    }
    const auto contract = expression_callable(input.program().expression(call.callee).type);
    if (contract.has_value()) {
        if (contract->result != expression.type
            || contract->parameters.size() != call.arguments.size()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "call expression does not match its resolved callable shape"
            );
        }
        const auto arguments_match = std::ranges::equal(
            call.arguments,
            contract->parameters,
            [&](const HIRCallArgument& argument,
                const HIRFunctionParameterType& parameter) noexcept {
                return argument.access == parameter.access
                    && semantic_id_known(argument.expression, input.program().expressions().size())
                    && input.program().expression(argument.expression).type == parameter.type;
            }
        );
        if (!arguments_match) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "call argument does not match its resolved parameter"
            );
        }
        const auto& evaluation =
            input.program().failure_set(control.evaluation_failure_set).members;
        for (const auto failure : input.program().failure_set(contract->failure_set).members) {
            if (!std::ranges::contains(evaluation, failure)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "call failure contract is absent from its evaluation summary"
                );
            }
        }
    }
    for (const auto& argument : call.arguments) {
        if (!visit_expression(argument.expression, scope)) {
            return false;
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::verify_body(
    BodyID id,
    std::optional<CallableID> callable_id,
    SemanticScopeID enclosing
) noexcept -> bool {
    const auto& body = input.program().body(id);
    const auto expected_parameter_count =
        callable_id.has_value() ? input.program().callable(*callable_id).parameters.size() : 0uz;
    if (!scope_known(body.scope)
        || !scope_contains(enclosing, body.scope)
        || body.parameters.size() != expected_parameter_count) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "program body does not match its callable or lexical owner"
        );
    }
    if (!callable_id.has_value()) {
        return visit_block(body.root, body.scope);
    }
    const auto& contract = input.program().callable(*callable_id);
    for (auto index = 0uz; index < body.parameters.size(); ++index) {
        const auto& parameter = body.parameters[index];
        if (parameter.access != contract.parameters[index].access
            || parameter.type != contract.parameters[index].type
            || !binding_place(parameter.target, parameter.type, body.scope)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "program body parameter does not match its callable contract"
            );
        }
    }
    return visit_block(body.root, body.scope);
}

template<typename Program>
auto SemanticVerifier<Program>::visit_function(FunctionID id, ProgramModuleID owner) noexcept
    -> bool {
    const auto& function = input.program().function(id);
    if (!semantic_id_known(function.callable, input.program().callables().size())) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "closed function contract has no completed body"
        );
    }
    const auto& callable = input.program().callable(function.callable);
    if (!symbol_known(function.symbol)
        || input.program().symbol(function.symbol).module_id != owner
        || !input.program().symbol(function.symbol).type.has_value()
        || !type_known(function.result)
        || function.parameter_origins.size() != callable.parameters.size()
        || callable.result != function.result) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "function declaration has an inconsistent symbol or contract"
        );
    }
    const auto& symbol_type = input.program().type(*input.program().symbol(function.symbol).type);
    const auto* function_type = std::get_if<HIRFunctionTypeValue>(&symbol_type.value);
    if (function_type == nullptr || function_type->callable != function.callable) {
        return fail(
            SemanticProgramErrorKind::InvalidContract,
            "function symbol type does not identify its callable contract"
        );
    }
    if (function.cpp_export_form_origin.has_value()) {
        if (!semantic_id_known(
                *function.cpp_export_form_origin,
                input.program().provenance().origins().size()
            )) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "export(cpp) form references an unknown origin"
            );
        }
        if (input.program().provenance().origin(*function.cpp_export_form_origin).source_id
            != input.program().provenance().module_record(owner).source_id) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                "export(cpp) origin does not belong to its function module"
            );
        }
        if (std::holds_alternative<HIRCppImportImplementation>(callable.implementation)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "export(cpp) function has an import(cpp) implementation"
            );
        }
    }
    return std::visit(
        Overloaded {
            [&](const HIRBodyImplementation& implementation) noexcept {
                if (!input.program().has_body(implementation.body)) {
                    return false;
                }
                const auto& body = input.program().body(implementation.body);
                return scope_known(body.scope)
                    && !input.program().scope(body.scope).parent.has_value()
                    && verify_body(implementation.body, function.callable, body.scope);
            },
            [&](const HIRCppImportImplementation& implementation) noexcept {
                return semantic_id_known(
                           implementation.form_origin,
                           input.program().provenance().origins().size()
                       )
                    && input.program().provenance().origin(implementation.form_origin).source_id
                    == input.program().provenance().module_record(owner).source_id;
            },
        },
        callable.implementation
    );
}

template<typename Program>
auto SemanticVerifier<Program>::visit_structure(StructID id, ProgramModuleID owner) noexcept
    -> bool {
    const auto& structure = input.program().structure(id);
    return symbol_known(structure.symbol)
        && input.program().symbol(structure.symbol).module_id == owner
        && std::ranges::all_of(structure.fields, [&](const auto& field) noexcept {
               return type_known(field.type);
           });
}

template<typename Program>
auto SemanticVerifier<Program>::visit_enumeration(EnumID id, ProgramModuleID owner) noexcept
    -> bool {
    const auto& enumeration = input.program().enumeration(id);
    if (!symbol_known(enumeration.symbol)
        || input.program().symbol(enumeration.symbol).module_id != owner
        || (enumeration.underlying_type.has_value() && !type_known(*enumeration.underlying_type))) {
        return false;
    }
    for (const auto case_id : enumeration.cases) {
        const auto& enum_case = input.program().enum_case(case_id);
        if (enum_case.owner != id
            || !symbol_known(enum_case.symbol)
            || input.program().symbol(enum_case.symbol).parent != enumeration.symbol
            || !std::ranges::all_of(enum_case.payload_types, [&](HIRTypeID type) noexcept {
                   return type_known(type);
               })) {
            return false;
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::visit_modules() noexcept -> bool {
    if (input.program().modules().size() != input.program().provenance().module_records().size()) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "program modules are not aligned with provenance"
        );
    }
    for (auto index = 0uz; index < input.program().modules().size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        const auto& hir_module = input.program().modules()[index];
        const auto module_source = input.program().provenance().module_record(module_id).source_id;
        for (const auto& dependency : hir_module.cpp_header_dependencies) {
            if (!semantic_id_known(
                    dependency.name,
                    input.program().provenance().spellings().size()
                )) {
                return fail(
                    SemanticProgramErrorKind::InvalidReference,
                    "C++ header dependency references an unknown spelling"
                );
            }
        }
        for (const auto origin : hir_module.cpp_source_payload_origins) {
            if (!semantic_id_known(origin, input.program().provenance().origins().size())) {
                return fail(
                    SemanticProgramErrorKind::InvalidReference,
                    "C++ source fragment references an unknown origin"
                );
            }
            if (input.program().provenance().origin(origin).source_id != module_source) {
                return fail(
                    SemanticProgramErrorKind::InvalidOwnership,
                    "C++ source fragment origin does not belong to its module"
                );
            }
        }
        for (const auto& item : hir_module.items) {
            const auto valid = std::visit(
                Overloaded {
                    [&](FunctionID function) noexcept {
                        return visit_function(function, module_id);
                    },
                    [&](StructID structure) noexcept {
                        return visit_structure(structure, module_id);
                    },
                    [&](EnumID enumeration) noexcept {
                        return visit_enumeration(enumeration, module_id);
                    },
                    [&](TestID id) noexcept {
                        const auto& test = input.program().test(id);
                        if (!input.program().has_body(test.body)) {
                            return false;
                        }
                        const auto& body = input.program().body(test.body);
                        const auto scope = body.scope;
                        return scope_known(scope)
                            && !input.program().scope(scope).parent.has_value()
                            && verify_body(test.body, std::nullopt, scope);
                    },
                },
                item
            );
            if (!valid) {
                return failure.has_value() ? false
                                           : fail(
                                                 SemanticProgramErrorKind::InvalidContract,
                                                 "program declaration violates its module contract"
                                             );
            }
        }
    }
    return true;
}

template<typename Program>
auto SemanticVerifier<Program>::visit_expression(HIRExprID id, SemanticScopeID scope) noexcept
    -> bool {
    if (!semantic_id_known(id, input.program().expressions().size())) {
        return fail(
            SemanticProgramErrorKind::InvalidReference,
            "program expression occurrence has an invalid identity"
        );
    }
    const auto& expression = input.program().expression(id);
    const auto& control = input.program().expression_control(id);
    const auto& place_use = input.program().place_use(id);
    const auto& try_facts = input.program().try_facts(id);
    if (!type_known(expression.type)
        || (expression.constant.has_value()
            && (!semantic_id_known(*expression.constant, input.program().constants().size())
                || input.program().constant(*expression.constant).type != expression.type))
        || !verify_place_use(expression, place_use, scope)) {
        return failure.has_value() ? false
                                   : fail(
                                         SemanticProgramErrorKind::InvalidType,
                                         "program expression has inconsistent published facts"
                                     );
    }
    const auto valid = std::visit(
        Overloaded {
            [](const HIRLiteralExpr&) static noexcept { return true; },
            [&](const HIRNameExpr& name) noexcept {
                return symbol_known(name.symbol)
                    && input.program().symbol(name.symbol).type == expression.type;
            },
            [&](const HIRArrayExpr& array) noexcept {
                const auto* type =
                    std::get_if<HIRArrayTypeValue>(&input.program().type(expression.type).value);
                if (type == nullptr || type->extent != array.element_ids.size()) {
                    return false;
                }
                for (const auto child : array.element_ids) {
                    if (!semantic_id_known(child, input.program().expressions().size())
                        || input.program().expression(child).type != type->element_type_id
                        || !visit_expression(child, scope)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const HIRConstructionExpr& construction) noexcept {
                const auto* nominal =
                    std::get_if<HIRStructTypeValue>(&input.program().type(expression.type).value);
                if (nominal == nullptr
                    || !semantic_id_known(
                        nominal->structure,
                        input.program().structures().size()
                    )) {
                    return false;
                }
                const auto& structure = input.program().structure(nominal->structure);
                if (construction.fields.size() != structure.fields.size()) {
                    return false;
                }
                auto initialized = std::flat_set<std::uint32_t>();
                for (const auto& field : construction.fields) {
                    if (field.declaration_index >= structure.fields.size()
                        || !initialized.insert(field.declaration_index).second
                        || !semantic_id_known(field.value, input.program().expressions().size())
                        || input.program().expression(field.value).type
                            != structure.fields[field.declaration_index].type
                        || !visit_expression(field.value, scope)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const HIRCaseConstructionExpr& construction) noexcept {
                if (!semantic_id_known(
                        construction.enum_case,
                        input.program().enum_cases().size()
                    )) {
                    return false;
                }
                const auto& enum_case = input.program().enum_case(construction.enum_case);
                if (construction.payload.size() != enum_case.payload_types.size()) {
                    return false;
                }
                for (auto index = 0uz; index < construction.payload.size(); ++index) {
                    const auto child = construction.payload[index];
                    if (!semantic_id_known(child, input.program().expressions().size())
                        || input.program().expression(child).type != enum_case.payload_types[index]
                        || !visit_expression(child, scope)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const HIRUnaryExpr& unary) noexcept {
                return visit_expression(unary.operand_id, scope);
            },
            [&](const HIRBinaryExpr& binary) noexcept {
                return visit_expression(binary.left, scope)
                    && visit_expression(binary.right, scope);
            },
            [&](const HIRCastExpr& cast) noexcept {
                return visit_expression(cast.operand_id, scope);
            },
            [&](const HIRCallExpr& call) noexcept {
                return verify_call(expression, control, call, scope);
            },
            [&](const HIRClosureExpr& closure) noexcept {
                if (!semantic_id_known(closure.callable, input.program().callables().size())) {
                    return false;
                }
                const auto& callable = input.program().callable(closure.callable);
                const auto body_id = callable_body_id(callable);
                if (!body_id.has_value() || !input.program().has_body(*body_id)) {
                    return false;
                }
                const auto& body = input.program().body(*body_id);
                if (!scope_contains(scope, body.scope) || callable.result != closure.result) {
                    return false;
                }
                for (const auto& capture : closure.captures) {
                    if (!symbol_known(capture.source) || !symbol_known(capture.local)) {
                        return false;
                    }
                    const auto& source_binding = input.program().binding(capture.source);
                    const auto& local_binding = input.program().binding(capture.local);
                    if (!source_binding.has_value()
                        || !local_binding.has_value()
                        || !scope_contains(source_binding->scope, scope)
                        || local_binding->scope != body.scope) {
                        return false;
                    }
                }
                const auto* closure_type =
                    std::get_if<HIRClosureTypeValue>(&input.program().type(expression.type).value);
                return closure_type != nullptr
                    && closure_type->callable == closure.callable
                    && closure_type->capturing == !closure.captures.empty()
                    && verify_body(*body_id, closure.callable, scope);
            },
            [&](const HIRCallableViewExpr& view) noexcept {
                return visit_expression(view.source, scope);
            },
            [&](const HIRPropagationExpr& propagation) noexcept {
                return visit_expression(propagation.operand_id, scope);
            },
            [&](const HIRTakeExpr& take) noexcept {
                return visit_expression(take.operand_id, scope)
                    && (!place_use.has_value() || place_use->access == SemanticPlaceAccess::Take);
            },
            [&](const HIRTextIntrinsicExpr& intrinsic) noexcept {
                return visit_expression(intrinsic.operand_id, scope);
            },
            [&](const HIRIndexExpr& index) noexcept {
                if (!semantic_id_known(index.operand_id, input.program().expressions().size())) {
                    return false;
                }
                const auto& operand_type =
                    input.program().type(input.program().expression(index.operand_id).type).value;
                const auto* array = std::get_if<HIRArrayTypeValue>(&operand_type);
                const auto valid_result =
                    array != nullptr && array->element_type_id == expression.type;
                return valid_result
                    && visit_expression(index.operand_id, scope)
                    && visit_expression(index.index, scope);
            },
            [&](const HIRMemberExpr& member) noexcept {
                return visit_expression(member.operand_id, scope);
            },
            [&](const HIRIfExpr& conditional) noexcept {
                for (const auto& branch : conditional.branches) {
                    if (!visit_expression(branch.condition, scope)
                        || !visit_block(branch.body, scope)) {
                        return false;
                    }
                }
                return !conditional.else_branch.has_value()
                    || visit_block(*conditional.else_branch, scope);
            },
            [&](const HIRMatchExpr& match) noexcept {
                if (!visit_expression(match.subject, scope)) {
                    return false;
                }
                const auto subject = input.program().expression(match.subject).type;
                for (const auto& arm : match.arms) {
                    if (!visit_match_arm(arm, subject, scope)) {
                        return false;
                    }
                }
                return true;
            },
            [&](const HIRTryExpr& attempt) noexcept {
                if (!visit_block(attempt.body, scope)) {
                    return false;
                }
                if (!try_facts.has_value() || try_facts->arms.size() != attempt.arms.size()) {
                    return false;
                }
                const auto outward_failure_set =
                    input.program().block_control(attempt.body).outward_failure_set;
                if (!failure_set_known(outward_failure_set)
                    || !failure_set_known(try_facts->unhandled_failure_set)) {
                    return false;
                }
                const auto& failures = input.program().failure_set(outward_failure_set).members;
                for (const auto failure :
                     input.program().failure_set(try_facts->unhandled_failure_set).members) {
                    if (!std::ranges::contains(failures, failure)) {
                        return fail(
                            SemanticProgramErrorKind::InvalidContract,
                            "try exposes an unhandled failure absent from its protected body"
                        );
                    }
                }
                for (auto index = 0uz; index < attempt.arms.size(); ++index) {
                    if (!visit_catch_arm(
                            attempt.arms[index],
                            try_facts->arms[index],
                            failures,
                            scope
                        )) {
                        return false;
                    }
                }
                return true;
            },
        },
        expression.value
    );
    if (!valid) {
        return failure.has_value() ? false
                                   : fail(
                                         SemanticProgramErrorKind::InvalidType,
                                         "program expression form violates its typed shape"
                                     );
    }
    return true;
}

template class SemanticVerifier<SemanticProgramView>;
template class SemanticVerifier<SemanticDraftView>;

auto validate_relations(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).check_relations();
}

auto validate_relations(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).check_relations();
}
