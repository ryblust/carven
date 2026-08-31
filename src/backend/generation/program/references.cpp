module carven:backend.generation.program.references.impl;

import :artifacts;
import :backend.generation.program.construction;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :semantic.visibility;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto visibility(const SemanticProgram& semantic, HIRDeclarationRef declaration) noexcept
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

auto declaration_ref(HIRNominalDeclRef nominal) noexcept -> HIRDeclarationRef {
    return std::visit([](auto id) static noexcept -> HIRDeclarationRef { return id; }, nominal);
}

auto owner_module(const SemanticProgram& semantic, HIRDeclarationRef declaration) noexcept
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

struct TargetReferenceAnalysis final {
    struct SurfaceDeclaration final {
        HIRDeclarationRef declaration;
        std::flat_map<HIRNominalDeclRef, TargetTypeCompleteness> requirements;
    };

    std::vector<std::flat_map<HIRNominalDeclRef, TargetTypeCompleteness>> surface;
    std::vector<std::vector<SurfaceDeclaration>> surface_declarations;
    std::vector<std::flat_set<ProgramModuleID>> implementation;
};

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
              .surface = decltype(result.surface)(surface_declarations.size()),
              .surface_declarations =
                  decltype(result.surface_declarations)(surface_declarations.size()),
              .implementation = decltype(result.implementation)(surface_declarations.size()),
          } {}

    auto collect() noexcept -> TargetReferenceAnalysis {
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
        const auto owner = owner_module(semantic, declaration_ref(nominal));
        if (owner != module_id) {
            result.implementation[module_id.index()].insert(owner);
        }
        if (!completeness.has_value()) {
            return;
        }
        auto& requirements = result.surface[module_id.index()];
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
            result.implementation[module_id.index()].insert(*owner);
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
    TargetReferenceAnalysis result;
    std::flat_map<HIRNominalDeclRef, TargetTypeCompleteness>* active_surface_requirements = nullptr;
    std::flat_set<BodyID> visited_bodies;
};

auto target_nominal_order(const SemanticProgram& semantic) noexcept
    -> std::vector<HIRNominalDeclRef> {
    const auto ordinal = [&](HIRNominalDeclRef nominal) noexcept -> std::size_t {
        return std::visit(
            Overloaded {
                [](StructID id) static noexcept -> std::size_t { return id.index(); },
                [&](EnumID id) noexcept -> std::size_t {
                    return semantic.structures().size() + id.index();
                },
            },
            nominal
        );
    };
    auto source_order = std::vector<HIRNominalDeclRef>();
    source_order.reserve(semantic.structures().size() + semantic.enumerations().size());
    for (auto index = 0uz; index < semantic.structures().size(); ++index) {
        source_order.push_back(StructID::from_index(static_cast<std::uint32_t>(index)));
    }
    for (auto index = 0uz; index < semantic.enumerations().size(); ++index) {
        source_order.push_back(EnumID::from_index(static_cast<std::uint32_t>(index)));
    }
    auto visited = std::flat_set<HIRNominalDeclRef>();
    auto active = std::flat_set<HIRNominalDeclRef>();
    auto result = std::vector<HIRNominalDeclRef>();
    const auto append = [&](this const auto& self, HIRNominalDeclRef nominal) noexcept -> void {
        if (visited.contains(nominal)) {
            return;
        }
        if (!active.insert(nominal).second) {
            invariant_violation("semantic nominal containment contains a cycle");
        }
        auto dependencies = std::vector<HIRNominalDeclRef>(
            semantic.nominal_containment(nominal).begin(),
            semantic.nominal_containment(nominal).end()
        );
        std::ranges::sort(dependencies, {}, ordinal);
        for (const auto dependency : dependencies) {
            self(dependency);
        }
        active.erase(nominal);
        visited.insert(nominal);
        result.push_back(nominal);
    };
    for (const auto nominal : source_order) {
        append(nominal);
    }
    return result;
}
auto include_directive(std::string_view logical_path) noexcept -> TargetDirective {
    return {.bytes = std::format("#include <{}>", logical_path)};
}

} // namespace

auto TargetProgramBuilder::build_artifact_graph() noexcept -> void {
    if (!name_allocation.has_value()) {
        invariant_violation("target artifact graph requires completed target names");
    }

    const auto ordered_nominals = target_nominal_order(semantic);
    auto module_schedules = std::vector<TargetModuleSchedule>();
    auto surface_declarations =
        std::vector<std::vector<HIRDeclarationRef>>(semantic.modules().size());
    module_schedules.reserve(semantic.modules().size());
    for (auto index = 0uz; index < semantic.modules().size(); ++index) {
        module_schedules.push_back({
            .module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index)),
            .implementation_nominal_order = {},
            .cpp_preamble_items = {},
            .private_function_declarations = {},
            .function_definitions = {},
            .entry_point = std::nullopt,
            .emitted_tests = {},
        });
    }
    for (const auto nominal : ordered_nominals) {
        const auto declaration = declaration_ref(nominal);
        const auto module_id = owner_module(semantic, declaration);
        if (visibility(semantic, declaration) != DeclarationVisibility::Module) {
            surface_declarations[module_id.index()].push_back(declaration);
        } else {
            module_schedules[module_id.index()].implementation_nominal_order.push_back(nominal);
        }
    }
    for (auto index = 0uz; index < semantic.modules().size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        for (const auto& [item_index, item] :
             std::views::enumerate(semantic.hir_module(module_id).items)) {
            if (std::holds_alternative<HIRCppRegion>(item)) {
                module_schedules[index].cpp_preamble_items.push_back(
                    static_cast<std::uint32_t>(item_index)
                );
            }
            const auto* function = std::get_if<FunctionID>(&item);
            if (function != nullptr
                && semantic.function(*function).visibility != DeclarationVisibility::Module) {
                surface_declarations[index].push_back(HIRDeclarationRef {*function});
            }
            if (function != nullptr) {
                module_schedules[index].function_definitions.push_back(*function);
                if (semantic.function(*function).visibility == DeclarationVisibility::Module) {
                    module_schedules[index].private_function_declarations.push_back(*function);
                }
                if (semantic.function(*function).entry_point.has_value()) {
                    module_schedules[index].entry_point = *function;
                }
            }
            if (request.tests != TestEmissionMode::None) {
                if (const auto* test = std::get_if<TestID>(&item)) {
                    module_schedules[index].emitted_tests.push_back(*test);
                }
            }
        }
    }

    auto references = TargetReferenceWalker(semantic, surface_declarations).collect();
    auto complete_dependencies =
        std::vector<std::flat_set<ProgramModuleID>>(semantic.modules().size());
    for (auto index = 0uz; index < references.surface.size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        for (const auto& [nominal, requirement] : references.surface[index]) {
            if (requirement != TargetTypeCompleteness::CompleteDefinition) {
                continue;
            }
            const auto dependency = owner_module(semantic, declaration_ref(nominal));
            if (dependency != module_id) {
                complete_dependencies[index].insert(dependency);
            }
        }
    }

    const auto module_path = [&](ProgramModuleID id) noexcept -> std::string_view {
        return semantic.provenance().module_record(id).path.value();
    };
    auto surface_modules = std::vector<ProgramModuleID>();
    for (auto index = 0uz; index < module_schedules.size(); ++index) {
        if (!surface_declarations[index].empty()) {
            surface_modules.push_back(
                ProgramModuleID::from_index(static_cast<std::uint32_t>(index))
            );
        }
    }
    std::ranges::sort(surface_modules, {}, module_path);
    auto surface_index = std::vector<std::optional<std::uint32_t>>(semantic.modules().size());
    for (auto index = 0uz; index < surface_modules.size(); ++index) {
        surface_index[surface_modules[index].index()] = static_cast<std::uint32_t>(index);
    }
    auto adjacency = std::vector<std::vector<std::uint32_t>>(surface_modules.size());
    for (auto index = 0uz; index < surface_modules.size(); ++index) {
        for (const auto dependency : complete_dependencies[surface_modules[index].index()]) {
            if (!surface_index[dependency.index()].has_value()) {
                invariant_violation("complete interface dependency has no published surface");
            }
            adjacency[index].push_back(*surface_index[dependency.index()]);
        }
    }

    const auto strong_components = strongly_connected_components(std::move(adjacency));
    auto component_members = std::vector<std::vector<ProgramModuleID>>();
    component_members.reserve(strong_components.dependency_first.size());
    auto module_component = std::vector<std::optional<std::size_t>>(semantic.modules().size());
    for (auto component_index = 0uz; component_index < strong_components.dependency_first.size();
         ++component_index) {
        auto members = std::vector<ProgramModuleID>();
        for (const auto member : strong_components.dependency_first[component_index]) {
            const auto module_id = surface_modules[member];
            members.push_back(module_id);
            module_component[module_id.index()] = component_index;
        }
        component_members.push_back(std::move(members));
    }

    auto component_predecessors = std::vector<std::flat_set<std::size_t>>(component_members.size());
    for (auto component_index = 0uz; component_index < component_members.size();
         ++component_index) {
        for (const auto member : component_members[component_index]) {
            for (const auto dependency : complete_dependencies[member.index()]) {
                if (!module_component[dependency.index()].has_value()) {
                    invariant_violation("complete interface dependency has no published surface");
                }
                if (*module_component[dependency.index()] != component_index) {
                    component_predecessors[component_index].insert(
                        *module_component[dependency.index()]
                    );
                }
            }
        }
    }

    auto nominal_ordinal = std::flat_map<HIRNominalDeclRef, std::size_t>();
    for (auto index = 0uz; index < ordered_nominals.size(); ++index) {
        nominal_ordinal.emplace(ordered_nominals[index], index);
    }

    artifacts.reserve(component_members.size() + module_schedules.size() + 1uz);
    for (auto component_index = 0uz; component_index < component_members.size();
         ++component_index) {
        const auto& members = component_members[component_index];
        auto included_components = std::vector<bool>(component_members.size());
        const auto include_predecessors = [&](this const auto& self,
                                              std::size_t component) noexcept -> void {
            for (const auto predecessor : component_predecessors[component]) {
                if (included_components[predecessor]) {
                    continue;
                }
                included_components[predecessor] = true;
                self(predecessor);
            }
        };
        include_predecessors(component_index);

        auto forward_nominals = std::flat_set<HIRNominalDeclRef>();
        for (const auto member : members) {
            for (const auto& declaration : references.surface_declarations[member.index()]) {
                const auto current = std::visit(
                    Overloaded {
                        [](FunctionID) static noexcept -> std::optional<HIRNominalDeclRef> {
                            return std::nullopt;
                        },
                        [](StructID id) static noexcept -> std::optional<HIRNominalDeclRef> {
                            return HIRNominalDeclRef {id};
                        },
                        [](EnumID id) static noexcept -> std::optional<HIRNominalDeclRef> {
                            return HIRNominalDeclRef {id};
                        },
                    },
                    declaration.declaration
                );
                if (!current.has_value()) {
                    continue;
                }
                for (const auto& [nominal, requirement] : declaration.requirements) {
                    const auto owner = owner_module(semantic, declaration_ref(nominal));
                    if (!module_component[owner.index()].has_value()
                        || *module_component[owner.index()] != component_index
                        || nominal_ordinal.at(nominal) <= nominal_ordinal.at(*current)) {
                        continue;
                    }
                    if (requirement == TargetTypeCompleteness::CompleteDefinition) {
                        invariant_violation(
                            "target nominal order violates a complete-definition dependency"
                        );
                    }
                    forward_nominals.insert(nominal);
                }
            }
            for (const auto& [nominal, requirement] : references.surface[member.index()]) {
                const auto owner = owner_module(semantic, declaration_ref(nominal));
                if (!module_component[owner.index()].has_value()) {
                    invariant_violation("interface dependency has no published surface");
                }
                const auto owner_component = *module_component[owner.index()];
                if (owner_component == component_index || included_components[owner_component]) {
                    continue;
                }
                if (requirement == TargetTypeCompleteness::CompleteDefinition) {
                    invariant_violation("complete interface dependency was not included");
                }
                forward_nominals.insert(nominal);
            }
        }

        auto ordered_forwards =
            std::vector<HIRNominalDeclRef>(forward_nominals.begin(), forward_nominals.end());
        std::ranges::sort(
            ordered_forwards,
            [&](HIRNominalDeclRef left, HIRNominalDeclRef right) noexcept {
                const auto left_module = owner_module(semantic, declaration_ref(left));
                const auto right_module = owner_module(semantic, declaration_ref(right));
                if (module_path(left_module) != module_path(right_module)) {
                    return module_path(left_module) < module_path(right_module);
                }
                return nominal_ordinal.at(left) < nominal_ordinal.at(right);
            }
        );
        auto forward_declarations = std::vector<TargetInterfaceForwardDeclaration>();
        forward_declarations.reserve(ordered_forwards.size());
        for (const auto nominal : ordered_forwards) {
            forward_declarations.push_back({
                .module_id = owner_module(semantic, declaration_ref(nominal)),
                .declaration = nominal,
            });
        }

        auto declarations = std::vector<TargetInterfaceDeclaration>();
        for (const auto nominal : ordered_nominals) {
            const auto declaration = declaration_ref(nominal);
            const auto owner = owner_module(semantic, declaration);
            if (module_component[owner.index()] == component_index
                && visibility(semantic, declaration) != DeclarationVisibility::Module) {
                declarations.push_back({.module_id = owner, .declaration = declaration});
            }
        }
        for (const auto member : members) {
            for (const auto& item : semantic.hir_module(member).items) {
                const auto* function = std::get_if<FunctionID>(&item);
                if (function != nullptr
                    && semantic.function(*function).visibility != DeclarationVisibility::Module) {
                    declarations.push_back({
                        .module_id = member,
                        .declaration = HIRDeclarationRef {*function},
                    });
                }
            }
        }

        auto directive_groups = std::vector<TargetArtifactDirectiveGroup> {
            {.directives = {TargetDirective {.bytes = "#pragma once"}}},
            {.directives = {include_directive("carven/runtime/runtime.hpp")}},
        };
        directive_groups.reserve(
            directive_groups.size() + component_predecessors[component_index].size()
        );
        for (const auto predecessor : component_predecessors[component_index]) {
            if (predecessor >= artifacts.size()) {
                invariant_violation("interface dependency was not built dependency-first");
            }
            const auto dependency =
                TargetArtifactID::from_index(static_cast<std::uint32_t>(predecessor));
            directive_groups.push_back({
                .directives = {TargetArtifactIncludeDirective {.artifact = dependency}},
            });
        }
        const auto& anchor_path = semantic.provenance().module_record(members.front()).path;
        artifacts.push_back({
            .logical_path = interface_component_logical_path(anchor_path.components()),
            .role = GeneratedArtifactRole::Interface,
            .source_mapping = ArtifactSourceMappingPolicy::StableInterface,
            .directive_groups = std::move(directive_groups),
            .schedule = TargetInterfaceSchedule {
                .component_members = members,
                .forward_declarations = std::move(forward_declarations),
                .declarations = std::move(declarations),
            },
        });
    }

    for (auto index = 0uz; index < module_schedules.size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        auto used_components = std::flat_set<std::size_t>();
        if (module_component[index].has_value()) {
            used_components.insert(*module_component[index]);
        }
        for (const auto dependency : references.implementation[index]) {
            if (module_component[dependency.index()].has_value()) {
                used_components.insert(*module_component[dependency.index()]);
            }
        }
        auto directives = std::vector<TargetArtifactDirective> {
            include_directive("carven/runtime/runtime.hpp"),
        };
        directives.reserve(directives.size() + used_components.size() + 1uz);
        for (const auto component : used_components) {
            const auto dependency =
                TargetArtifactID::from_index(static_cast<std::uint32_t>(component));
            directives.push_back(TargetArtifactIncludeDirective {.artifact = dependency});
        }
        if (!module_schedules[index].emitted_tests.empty()) {
            directives.push_back(include_directive("carven/std/testing/testing.hpp"));
        }
        const auto& path = semantic.provenance().module_record(module_id).path;
        artifacts.push_back({
            .logical_path = module_implementation_logical_path(path.components()),
            .role = GeneratedArtifactRole::ModuleImplementation,
            .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
            .directive_groups = {{.directives = std::move(directives)}},
            .schedule = std::move(module_schedules[index]),
        });
    }

    if (request.tests == TestEmissionMode::DefaultRunner) {
        artifacts.push_back({
            .logical_path = "carven-test-main.cpp",
            .role = GeneratedArtifactRole::TestEntry,
            .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
            .directive_groups = {{
                .directives = {include_directive("carven/std/testing/testing.hpp")},
            }},
            .schedule = TargetTestEntrySchedule {},
        });
    }
}
