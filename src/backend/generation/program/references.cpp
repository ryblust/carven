module carven:backend.generation.program.references.impl;

import :backend.generation.program.references;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

auto target_visibility(const SemanticProgram& semantic, HIRDeclarationRef declaration) noexcept
    -> DeclarationVisibility {
    return std::visit(
        Overloaded {
            [&](FunctionID function) noexcept { return semantic.function(function).visibility; },
            [&](StructID structure) noexcept { return semantic.structure(structure).visibility; },
            [&](EnumID enumeration) noexcept {
                return semantic.enumeration(enumeration).visibility;
            },
        },
        declaration
    );
}
auto target_declaration_ref(HIRNominalDeclRef nominal) noexcept -> HIRDeclarationRef {
    return std::visit([](auto id) static noexcept -> HIRDeclarationRef { return id; }, nominal);
}

auto target_owner_module(const SemanticProgram& semantic, HIRDeclarationRef declaration) noexcept
    -> ProgramModuleID {
    const auto symbol = std::visit(
        Overloaded {
            [&](FunctionID function) noexcept { return semantic.function(function).symbol; },
            [&](StructID structure) noexcept { return semantic.structure(structure).symbol; },
            [&](EnumID enumeration) noexcept { return semantic.enumeration(enumeration).symbol; },
        },
        declaration
    );
    if (!semantic.symbol(symbol).module_id.has_value()) {
        invariant_violation("target declaration has no owning module");
    }
    return *semantic.symbol(symbol).module_id;
}

namespace {

class TargetReferenceWalker final {
    struct RecursionGuard final {
        std::flat_set<HIRTypeID> types;
        std::flat_set<CallableID> callables;
        std::flat_set<CallableSignatureID> signatures;
    };

public:
    TargetReferenceWalker(
        const SemanticProgram& semantic,
        std::span<const std::vector<HIRDeclarationRef>> surface_declarations
    ) noexcept
        : semantic(semantic),
          surface_declarations(surface_declarations),
          result {
              .surface_requirements =
                  decltype(result.surface_requirements)(surface_declarations.size()),
              .surface_declarations =
                  decltype(result.surface_declarations)(surface_declarations.size()),
              .implementation_dependencies =
                  decltype(result.implementation_dependencies)(surface_declarations.size()),
          } {}

    auto collect() noexcept -> TargetReferenceFacts {
        for (auto index = 0uz; index < surface_declarations.size(); ++index) {
            const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
            for (const auto declaration : surface_declarations[index]) {
                result.surface_declarations[index].push_back({
                    .declaration = declaration,
                    .requirements = {},
                });
                active_surface_requirements =
                    &result.surface_declarations[index].back().requirements;
                collect_surface_declaration(module_id, declaration);
                active_surface_requirements = nullptr;
            }
            for (const auto& item : semantic.hir_module(module_id).items) {
                collect_implementation_item(module_id, item);
            }
        }
        return std::move(result);
    }

private:
    auto require_nominal(
        ProgramModuleID module_id,
        HIRNominalDeclRef nominal,
        std::optional<TargetTypeCompleteness> completeness
    ) noexcept -> void {
        const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
        if (owner != module_id) {
            result.implementation_dependencies[module_id.index()].insert(owner);
        }
        if (!completeness.has_value()) {
            return;
        }
        auto& requirements = result.surface_requirements[module_id.index()];
        const auto merge_requirement = [&](auto& target) noexcept {
            const auto found = target.find(nominal);
            if (found == target.end()) {
                target.emplace(nominal, *completeness);
            } else if (*completeness == TargetTypeCompleteness::CompleteDefinition) {
                found->second = TargetTypeCompleteness::CompleteDefinition;
            }
        };
        merge_requirement(requirements);
        if (active_surface_requirements != nullptr) {
            merge_requirement(*active_surface_requirements);
        }
    }

    auto collect_symbol(ProgramModuleID module_id, SymbolID symbol) noexcept -> void {
        const auto owner = semantic.symbol(symbol).module_id;
        if (owner.has_value() && *owner != module_id) {
            result.implementation_dependencies[module_id.index()].insert(*owner);
        }
    }

    auto collect_callable(
        ProgramModuleID module_id,
        CallableID callable_id,
        std::optional<TargetTypeCompleteness> completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        if (!guard.callables.insert(callable_id).second) {
            return;
        }
        const auto& callable = semantic.callable(callable_id);
        const auto contract_requirement = completeness.has_value()
            ? std::optional(TargetTypeCompleteness::Declaration)
            : std::nullopt;
        for (const auto& parameter : callable.parameters) {
            collect_type(module_id, parameter.type, contract_requirement, guard);
        }
        collect_type(module_id, callable.result, contract_requirement, guard);
        const auto failure_set = semantic.callable_flow(callable_id).effective_failure_set;
        for (const auto failure : semantic.failure_set(failure_set).members) {
            collect_type(module_id, failure, contract_requirement, guard);
        }
        guard.callables.erase(callable_id);
    }

    auto collect_signature(
        ProgramModuleID module_id,
        CallableSignatureID signature_id,
        std::optional<TargetTypeCompleteness> completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        if (!guard.signatures.insert(signature_id).second) {
            return;
        }
        const auto& signature = semantic.callable_signature(signature_id);
        const auto contract_requirement = completeness.has_value()
            ? std::optional(TargetTypeCompleteness::Declaration)
            : std::nullopt;
        for (const auto& parameter : signature.parameters) {
            collect_type(module_id, parameter.type, contract_requirement, guard);
        }
        collect_type(module_id, signature.result, contract_requirement, guard);
        for (const auto failure : semantic.failure_set(signature.failure_set).members) {
            collect_type(module_id, failure, contract_requirement, guard);
        }
        guard.signatures.erase(signature_id);
    }

    auto collect_type(
        ProgramModuleID module_id,
        HIRTypeID type_id,
        std::optional<TargetTypeCompleteness> completeness,
        RecursionGuard& guard
    ) noexcept -> void {
        if (!guard.types.insert(type_id).second) {
            return;
        }
        std::visit(
            Overloaded {
                [](const HIRBuiltinTypeValue&) static noexcept {},
                [&](const HIRStructTypeValue& value) noexcept {
                    require_nominal(module_id, HIRNominalDeclRef {value.structure}, completeness);
                },
                [&](const HIREnumTypeValue& value) noexcept {
                    require_nominal(module_id, HIRNominalDeclRef {value.enumeration}, completeness);
                },
                [&](const HIRArrayTypeValue& value) noexcept {
                    const auto element_requirement = completeness.has_value()
                        ? std::optional(TargetTypeCompleteness::CompleteDefinition)
                        : std::nullopt;
                    collect_type(module_id, value.element_type_id, element_requirement, guard);
                },
                [&](const HIRFunctionTypeValue& value) noexcept {
                    collect_callable(module_id, value.callable, completeness, guard);
                },
                [&](const HIRFunctionRefTypeValue& value) noexcept {
                    collect_signature(module_id, value.signature, completeness, guard);
                },
                [&](const HIRClosureTypeValue& value) noexcept {
                    collect_callable(module_id, value.callable, completeness, guard);
                },
                [](const HIRForeignTypeValue&) static noexcept {},
                [](const HIRErrorTypeValue&) static noexcept {},
            },
            semantic.type(type_id).value
        );
        guard.types.erase(type_id);
    }

    auto collect_type(
        ProgramModuleID module_id,
        HIRTypeID type_id,
        std::optional<TargetTypeCompleteness> completeness = std::nullopt
    ) noexcept -> void {
        auto guard = RecursionGuard();
        collect_type(module_id, type_id, completeness, guard);
    }

    auto collect_surface_declaration(
        ProgramModuleID module_id,
        HIRDeclarationRef declaration
    ) noexcept -> void {
        auto guard = RecursionGuard();
        std::visit(
            Overloaded {
                [&](FunctionID id) noexcept {
                    collect_callable(
                        module_id,
                        semantic.function(id).callable,
                        TargetTypeCompleteness::Declaration,
                        guard
                    );
                },
                [&](StructID id) noexcept {
                    for (const auto& field : semantic.structure(id).fields) {
                        collect_type(
                            module_id,
                            field.type,
                            TargetTypeCompleteness::CompleteDefinition,
                            guard
                        );
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& enumeration = semantic.enumeration(id);
                    if (enumeration.underlying_type.has_value()) {
                        collect_type(
                            module_id,
                            *enumeration.underlying_type,
                            TargetTypeCompleteness::CompleteDefinition,
                            guard
                        );
                    }
                    for (const auto case_id : enumeration.cases) {
                        for (const auto type : semantic.enum_case(case_id).payload_types) {
                            collect_type(
                                module_id,
                                type,
                                TargetTypeCompleteness::CompleteDefinition,
                                guard
                            );
                        }
                    }
                },
            },
            declaration
        );
    }

    auto collect_callable_contract(ProgramModuleID module_id, CallableID callable) noexcept
        -> void {
        auto guard = RecursionGuard();
        collect_callable(module_id, callable, std::nullopt, guard);
    }

    auto collect_implementation_item(ProgramModuleID module_id, const HIRModuleItem& item) noexcept
        -> void {
        std::visit(
            Overloaded {
                [&](FunctionID id) noexcept {
                    const auto callable = semantic.function(id).callable;
                    collect_callable_contract(module_id, callable);
                    collect_body(module_id, semantic.callable(callable).body);
                },
                [&](StructID id) noexcept {
                    for (const auto& field : semantic.structure(id).fields) {
                        collect_type(module_id, field.type);
                    }
                },
                [&](EnumID id) noexcept {
                    const auto& enumeration = semantic.enumeration(id);
                    if (enumeration.underlying_type.has_value()) {
                        collect_type(module_id, *enumeration.underlying_type);
                    }
                    for (const auto case_id : enumeration.cases) {
                        for (const auto type : semantic.enum_case(case_id).payload_types) {
                            collect_type(module_id, type);
                        }
                    }
                },
                [&](TestID id) noexcept { collect_body(module_id, semantic.test(id).body); },
                [](const HIRCppRegion&) static noexcept {},
            },
            item
        );
    }

    auto collect_expression_metadata(ProgramModuleID module_id, const HIRExpr& expression) noexcept
        -> void {
        collect_type(module_id, expression.type);
        std::visit(
            Overloaded {
                [&](const HIRNameExpr& value) noexcept { collect_symbol(module_id, value.symbol); },
                [&](const HIRCaseConstructionExpr& value) noexcept {
                    const auto owner = semantic.enum_case(value.enum_case).owner;
                    collect_symbol(module_id, semantic.enumeration(owner).symbol);
                },
                [&](const HIRClosureExpr& value) noexcept {
                    collect_callable_contract(module_id, value.callable);
                    for (const auto& capture : value.captures) {
                        collect_type(module_id, capture.type);
                    }
                },
                [&](const HIRMemberExpr& value) noexcept {
                    std::visit(
                        Overloaded {
                            [](const HIRUnresolvedMemberTarget&) static noexcept {},
                            [&](const HIRStructFieldTarget& target) noexcept {
                                collect_symbol(module_id, semantic.structure(target.owner).symbol);
                            },
                            [&](const HIREnumCaseTarget& target) noexcept {
                                const auto owner = semantic.enum_case(target.enum_case).owner;
                                collect_symbol(module_id, semantic.enumeration(owner).symbol);
                            },
                        },
                        value.target
                    );
                },
                [&](const HIRTryExpr& value) noexcept {
                    for (const auto& arm : value.arms) {
                        for (const auto& alternative : arm.alternatives) {
                            if (alternative.type.has_value()) {
                                collect_type(module_id, *alternative.type);
                            }
                        }
                    }
                },
                [](const auto&) static noexcept {},
            },
            expression.value
        );
    }

    auto collect_pattern_metadata(ProgramModuleID module_id, const HIRPattern& pattern) noexcept
        -> void {
        std::visit(
            Overloaded {
                [](const HIRWildcardPattern&) static noexcept {},
                [&](const auto& value) noexcept {
                    if constexpr (requires { value.type; }) {
                        collect_type(module_id, value.type);
                    }
                },
                [&](const HIRCasePattern& value) noexcept {
                    const auto owner = semantic.enum_case(value.enum_case).owner;
                    collect_symbol(module_id, semantic.enumeration(owner).symbol);
                },
            },
            pattern.value
        );
    }

    auto collect_statement_metadata(ProgramModuleID module_id, const HIRStmt& statement) noexcept
        -> void {
        std::visit(
            Overloaded {
                [&](const HIRThrowStmt& value) noexcept {
                    collect_type(module_id, value.failure_type);
                },
                [&](const HIRBindingStmt& value) noexcept { collect_type(module_id, value.type); },
                [&](const HIRRangeForStmt& value) noexcept { collect_type(module_id, value.type); },
                [](const auto&) static noexcept {},
            },
            statement.value
        );
    }

    auto collect_body(ProgramModuleID module_id, BodyID id) noexcept -> void {
        if (!visited_bodies.insert(id).second) {
            return;
        }
        const auto& body = semantic.body(id);
        for (const auto& parameter : body.parameters) {
            collect_type(module_id, parameter.type);
        }
        collect_block(module_id, body.root);
    }

    auto collect_block(ProgramModuleID module_id, HIRBlockID id) noexcept -> void {
        const auto& block = semantic.block(id);
        for (const auto statement : block.statements) {
            collect_statement(module_id, statement);
        }
        if (block.result.has_value()) {
            collect_expression(module_id, *block.result);
        }
    }

    auto collect_pattern(ProgramModuleID module_id, HIRPatternID id) noexcept -> void {
        const auto& pattern = semantic.pattern(id);
        collect_pattern_metadata(module_id, pattern);
        std::visit(
            Overloaded {
                [&](const HIROrPattern& value) noexcept {
                    for (const auto child : value.alternatives) {
                        collect_pattern(module_id, child);
                    }
                },
                [&](const HIRCasePattern& value) noexcept {
                    for (const auto child : value.payload) {
                        collect_pattern(module_id, child);
                    }
                },
                [](const auto&) static noexcept {},
            },
            pattern.value
        );
    }

    auto collect_match_arm(ProgramModuleID module_id, const HIRMatchArm& arm) noexcept -> void {
        collect_pattern(module_id, arm.pattern);
        if (arm.guard.has_value()) {
            collect_expression(module_id, *arm.guard);
        }
        collect_block(module_id, arm.body);
    }

    auto collect_match(
        ProgramModuleID module_id,
        HIRExprID subject_id,
        std::span<const HIRMatchArm> arms,
        const HIRMatchCoverageFacts& coverage
    ) noexcept -> void {
        if (arms.size() != coverage.arm_states.size()) {
            invariant_violation("match reference facts do not align with source arms");
        }
        collect_expression(module_id, subject_id);
        for (auto index = 0uz; index < arms.size(); ++index) {
            if (coverage.arm_states[index] == HIRMatchArmState::Reachable) {
                collect_match_arm(module_id, arms[index]);
            }
        }
    }

    auto collect_expression(ProgramModuleID module_id, HIRExprID id) noexcept -> void {
        const auto& expression = semantic.expression(id);
        collect_expression_metadata(module_id, expression);
        std::visit(
            Overloaded {
                [](const HIRLiteralExpr&) static noexcept {},
                [](const HIRNameExpr&) static noexcept {},
                [&](const HIRArrayExpr& value) noexcept {
                    for (const auto child : value.element_ids) {
                        collect_expression(module_id, child);
                    }
                },
                [&](const HIRConstructionExpr& value) noexcept {
                    for (const auto& field : value.fields) {
                        collect_expression(module_id, field.value);
                    }
                },
                [&](const HIRCaseConstructionExpr& value) noexcept {
                    for (const auto child : value.payload) {
                        collect_expression(module_id, child);
                    }
                },
                [&](const HIRUnaryExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                },
                [&](const HIRBinaryExpr& value) noexcept {
                    collect_expression(module_id, value.left);
                    collect_expression(module_id, value.right);
                },
                [&](const HIRCastExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                },
                [&](const HIRCallExpr& value) noexcept {
                    collect_expression(module_id, value.callee);
                    for (const auto& argument : value.arguments) {
                        collect_expression(module_id, argument.expression);
                    }
                },
                [&](const HIRClosureExpr& value) noexcept {
                    collect_body(module_id, semantic.callable(value.callable).body);
                },
                [&](const HIRCallableViewExpr& value) noexcept {
                    collect_expression(module_id, value.source);
                },
                [&](const HIRPropagationExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                },
                [&](const HIRTakeExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                },
                [&](const HIRTextIntrinsicExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                },
                [&](const HIRIndexExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                    collect_expression(module_id, value.index);
                },
                [&](const HIRMemberExpr& value) noexcept {
                    collect_expression(module_id, value.operand_id);
                },
                [&](const HIRIfExpr& value) noexcept {
                    for (const auto& branch : value.branches) {
                        collect_expression(module_id, branch.condition);
                        collect_block(module_id, branch.body);
                    }
                    if (value.else_branch.has_value()) {
                        collect_block(module_id, *value.else_branch);
                    }
                },
                [&](const HIRMatchExpr& value) noexcept {
                    collect_match(module_id, value.subject, value.arms, value.coverage);
                },
                [&](const HIRTryExpr& value) noexcept {
                    collect_block(module_id, value.body);
                    for (const auto& arm : value.arms) {
                        for (const auto& alternative : arm.alternatives) {
                            if (alternative.inner.has_value()) {
                                collect_pattern(module_id, *alternative.inner);
                            }
                        }
                        if (arm.guard.has_value()) {
                            collect_expression(module_id, *arm.guard);
                        }
                        collect_block(module_id, arm.body);
                    }
                },
                [](const HIRCppExpr&) static noexcept {},
            },
            expression.value
        );
    }

    auto collect_statement(ProgramModuleID module_id, HIRStmtID id) noexcept -> void {
        const auto& statement = semantic.statement(id);
        collect_statement_metadata(module_id, statement);
        std::visit(
            Overloaded {
                [&](const HIRReturnStmt& value) noexcept {
                    if (value.value.has_value()) {
                        collect_expression(module_id, *value.value);
                    }
                },
                [](const HIRBreakStmt&) static noexcept {},
                [](const HIRContinueStmt&) static noexcept {},
                [&](const HIRThrowStmt& value) noexcept {
                    collect_expression(module_id, value.value);
                },
                [](const HIRRethrowStmt&) static noexcept {},
                [&](const HIRExprStmt& value) noexcept {
                    collect_expression(module_id, value.expression);
                },
                [&](const HIRBindingStmt& value) noexcept {
                    collect_expression(module_id, value.initializer);
                },
                [&](const HIRAssignmentStmt& value) noexcept {
                    collect_expression(module_id, value.target);
                    collect_expression(module_id, value.value);
                },
                [&](const HIRUpdateStmt& value) noexcept {
                    collect_expression(module_id, value.target);
                },
                [&](const HIRIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        collect_expression(module_id, branch.condition);
                        collect_block(module_id, branch.body);
                    }
                    if (value.else_branch.has_value()) {
                        collect_block(module_id, *value.else_branch);
                    }
                },
                [&](const HIRMatchStmt& value) noexcept {
                    collect_match(module_id, value.subject, value.arms, value.coverage);
                },
                [&](const HIRWhileStmt& value) noexcept {
                    collect_expression(module_id, value.condition);
                    collect_block(module_id, value.body);
                },
                [&](const HIRCStyleForStmt& value) noexcept {
                    if (value.initializer.has_value()) {
                        collect_statement(module_id, *value.initializer);
                    }
                    if (value.condition.has_value()) {
                        collect_expression(module_id, *value.condition);
                    }
                    collect_block(module_id, value.body);
                    for (const auto step : value.steps) {
                        collect_statement(module_id, step);
                    }
                },
                [&](const HIRRangeForStmt& value) noexcept {
                    std::visit(
                        Overloaded {
                            [&](HIRExprID expression) noexcept {
                                collect_expression(module_id, expression);
                            },
                            [&](const HIRHalfOpenRange& range) noexcept {
                                collect_expression(module_id, range.begin);
                                collect_expression(module_id, range.end);
                            },
                        },
                        value.iterable
                    );
                    collect_block(module_id, value.body);
                },
                [&](const HIRTestCheckStmt& value) noexcept {
                    collect_expression(module_id, value.condition);
                    if (value.message.has_value()) {
                        collect_expression(module_id, *value.message);
                    }
                },
                [&](const HIRTestRequireStmt& value) noexcept {
                    collect_expression(module_id, value.condition);
                    if (value.message.has_value()) {
                        collect_expression(module_id, *value.message);
                    }
                },
                [&](const HIRTestFailStmt& value) noexcept {
                    if (value.message.has_value()) {
                        collect_expression(module_id, *value.message);
                    }
                },
                [](const HIRCppStmt&) static noexcept {},
            },
            statement.value
        );
    }

    const SemanticProgram& semantic;
    std::span<const std::vector<HIRDeclarationRef>> surface_declarations;
    TargetReferenceFacts result;
    std::flat_map<HIRNominalDeclRef, TargetTypeCompleteness>* active_surface_requirements = nullptr;
    std::flat_set<BodyID> visited_bodies;
};

} // namespace

auto collect_target_references(
    const SemanticProgram& semantic,
    std::span<const std::vector<HIRDeclarationRef>> surface_declarations
) noexcept -> TargetReferenceFacts {
    return TargetReferenceWalker(semantic, surface_declarations).collect();
}
