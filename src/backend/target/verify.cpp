module carven:backend.target.verify.impl;

import :artifacts;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import :support.visit;
import std;

namespace {

enum class VisitState {
    Unseen,
    Visiting,
    Visited,
};

class TargetUnitStructureVerifier final {
public:
    explicit TargetUnitStructureVerifier(TargetUnitValidationView unit) noexcept
        : unit(unit),
          type_states(unit.types.size(), VisitState::Unseen),
          expression_states(unit.expressions.size(), VisitState::Unseen),
          statement_states(unit.statements.size(), VisitState::Unseen),
          item_states(unit.items.size(), VisitState::Unseen) {}

    auto run() noexcept -> std::expected<void, TargetUnitViolation> {
        if (!visit_root() || !verify_all_reached()) {
            return std::unexpected(std::move(*failure));
        }
        return {};
    }

private:
    auto fail(TargetUnitViolationKind kind, std::string message) noexcept -> bool {
        failure = TargetUnitViolation {.kind = kind, .message = std::move(message)};
        return false;
    }

    template<typename ID>
    auto verify_bounds(ID value_id, std::size_t size, std::string_view category) noexcept -> bool {
        return value_id.index() < size
            || fail(
                   TargetUnitViolationKind::InvalidReference,
                   std::format("target {} ID {} is out of range", category, value_id.index())
            );
    }

    auto verify_attribution(
        const TargetAttribution& attribution,
        std::string_view category,
        std::size_t index
    ) noexcept -> bool {
        switch (attribution.kind) {
            case TargetAttributionKind::SourceOwned:
            case TargetAttributionKind::RawSource:
                if (!attribution.origin.has_value()) {
                    return fail(
                        TargetUnitViolationKind::InvalidAttribution,
                        std::format("target {} {} requires a source origin", category, index)
                    );
                }
                if (attribution.reason.has_value()) {
                    return fail(
                        TargetUnitViolationKind::InvalidAttribution,
                        std::format(
                            "target {} {} has both source ownership and a synthetic reason",
                            category,
                            index
                        )
                    );
                }
                return true;
            case TargetAttributionKind::SourceExpansion:
                return attribution.origin.has_value()
                    || attribution.reason.has_value()
                    || fail(
                           TargetUnitViolationKind::InvalidAttribution,
                           std::format(
                               "target source expansion {} {} has no origin or synthetic reason",
                               category,
                               index
                           )
                    );
            case TargetAttributionKind::CompilerOwned:
                if (!attribution.reason.has_value()) {
                    return fail(
                        TargetUnitViolationKind::InvalidAttribution,
                        std::format("target {} {} requires a synthetic reason", category, index)
                    );
                }
                return !attribution.origin.has_value()
                    || fail(
                        TargetUnitViolationKind::InvalidAttribution,
                        std::format(
                            "compiler-owned target {} {} unexpectedly has a source origin",
                            category,
                            index
                        )
                    );
        }
        std::unreachable();
    }

    auto visit_type_arguments(std::span<const TargetTypeID> type_argument_ids) noexcept -> bool {
        return std::ranges::all_of(type_argument_ids, [&](TargetTypeID argument_type_id) noexcept {
            return visit_type(argument_type_id);
        });
    }

    auto visit_type(TargetTypeID type_id) noexcept -> bool {
        if (!verify_bounds(type_id, unit.types.size(), "type")) {
            return false;
        }
        auto& state = type_states[type_id.index()];
        if (state == VisitState::Visiting) {
            return fail(
                TargetUnitViolationKind::InvalidCycle,
                std::format("target type graph contains a cycle at ID {}", type_id.index())
            );
        }
        if (state == VisitState::Visited) {
            return true;
        }
        state = VisitState::Visiting;
        if (!std::visit(
                Overloaded {
                    [&](const TargetNamedType& value) noexcept {
                        if (!visit_type_arguments(value.type_argument_ids)) {
                            return false;
                        }
                        for (const auto& segment : value.nested) {
                            if (!visit_type_arguments(segment.type_argument_ids)) {
                                return false;
                            }
                        }
                        return true;
                    },
                    [&](const TargetIntrinsicType& value) noexcept {
                        return visit_type_arguments(value.type_argument_ids);
                    },
                    [&](const TargetArrayType& value) noexcept {
                        return visit_type(value.element_type_id);
                    },
                    [&](const TargetFunctionType& value) noexcept {
                        for (const auto parameter_type_id : value.parameters) {
                            if (!visit_type(parameter_type_id)) {
                                return false;
                            }
                        }
                        return visit_type(value.result);
                    },
                    [&](const TargetPointerType& value) noexcept {
                        return visit_type(value.pointee);
                    },
                    [&](const TargetReferenceType& value) noexcept {
                        return visit_type(value.referent);
                    },
                },
                unit.types[type_id.index()].value
            )) {
            return false;
        }
        state = VisitState::Visited;
        return true;
    }

    auto visit_expressions(std::span<const TargetExprID> expression_ids) noexcept -> bool {
        return std::ranges::all_of(expression_ids, [&](TargetExprID expression_id) noexcept {
            return visit_expression(expression_id);
        });
    }

    auto visit_expression(TargetExprID expression_id) noexcept -> bool {
        if (!verify_bounds(expression_id, unit.expressions.size(), "expression")) {
            return false;
        }
        auto& state = expression_states[expression_id.index()];
        if (state == VisitState::Visiting) {
            return fail(
                TargetUnitViolationKind::InvalidCycle,
                std::format(
                    "target expression graph contains a cycle at ID {}",
                    expression_id.index()
                )
            );
        }
        if (state == VisitState::Visited) {
            return fail(
                TargetUnitViolationKind::InvalidOwnership,
                std::format("target expression ID {} has multiple owners", expression_id.index())
            );
        }
        state = VisitState::Visiting;
        if (!visit_expression_value(unit.expressions[expression_id.index()].value)) {
            return false;
        }
        state = VisitState::Visited;
        return true;
    }

    auto visit_expression_value(const TargetExprValue& expression) noexcept -> bool {
        return std::visit(
            Overloaded {
                [](const TargetNameExpr&) static noexcept { return true; },
                [](const TargetIntrinsicNameExpr&) static noexcept { return true; },
                [](const TargetLiteralExpr&) static noexcept { return true; },
                [&](const TargetPrefixExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const TargetBinaryExpr& value) noexcept {
                    return visit_expression(value.left) && visit_expression(value.right);
                },
                [&](const TargetCallExpr& value) noexcept {
                    return visit_expression(value.callee)
                        && visit_type_arguments(value.template_argument_type_ids)
                        && visit_expressions(value.arguments);
                },
                [&](const TargetArrayExpr& value) noexcept {
                    return visit_type(value.element_type_id)
                        && visit_expression(value.extent)
                        && visit_expressions(value.element_ids);
                },
                [&](const TargetConstructionExpr& value) noexcept {
                    if (!visit_type(value.type)) {
                        return false;
                    }
                    return std::visit(
                        Overloaded {
                            [](const std::monostate&) static noexcept { return true; },
                            [&](const std::vector<TargetExprID>& value_ids) noexcept {
                                return visit_expressions(value_ids);
                            },
                            [&](const std::vector<TargetFieldInitializer>& fields) noexcept {
                                return std::ranges::all_of(
                                    fields,
                                    [&](const TargetFieldInitializer& field) noexcept {
                                        return visit_expression(field.value);
                                    }
                                );
                            },
                        },
                        value.initializer
                    );
                },
                [&](const TargetIndexExpr& value) noexcept {
                    return visit_expression(value.operand_id) && visit_expression(value.index);
                },
                [&](const TargetMemberExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const TargetScopeMemberExpr& value) noexcept {
                    const auto* operand_id = std::get_if<TargetExprID>(&value.operand);
                    return operand_id == nullptr || visit_expression(*operand_id);
                },
                [&](const TargetStaticMemberExpr& value) noexcept {
                    return visit_type(value.owner);
                },
                [](const TargetForwardExpr&) static noexcept { return true; },
                [&](const TargetStaticCastExpr& value) noexcept {
                    return visit_type(value.type) && visit_expression(value.operand_id);
                },
                [](const TargetRawFragment&) static noexcept { return true; },
                [&](const TargetLambdaExpr& value) noexcept {
                    return visit_statements(value.body);
                },
                [&](const TargetClosureExpr& value) noexcept {
                    for (const auto& parameter : value.parameters) {
                        if (!visit_type(parameter.type)) {
                            return false;
                        }
                    }
                    return visit_type(value.result) && visit_statements(value.body);
                },
            },
            expression
        );
    }

    auto visit_statements(std::span<const TargetStmtID> statement_ids) noexcept -> bool {
        return std::ranges::all_of(statement_ids, [&](TargetStmtID statement_id) noexcept {
            return visit_statement(statement_id);
        });
    }

    auto visit_for_initializer(const TargetForInitializer& initializer) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetExprStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const TargetDiscardStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const TargetVariableStmt& value) noexcept {
                    return visit_type(value.type) && visit_expression(value.initializer);
                },
                [&](const TargetAssignmentStmt& value) noexcept {
                    return visit_expression(value.target) && visit_expression(value.value);
                },
                [&](const TargetUpdateStmt& value) noexcept {
                    return visit_expression(value.target);
                },
            },
            initializer.value
        );
    }

    auto visit_for_step(const TargetForStep& step) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetExprStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const TargetDiscardStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const TargetAssignmentStmt& value) noexcept {
                    return visit_expression(value.target) && visit_expression(value.value);
                },
                [&](const TargetUpdateStmt& value) noexcept {
                    return visit_expression(value.target);
                },
            },
            step.value
        );
    }

    auto visit_statement(TargetStmtID statement_id) noexcept -> bool {
        if (!verify_bounds(statement_id, unit.statements.size(), "statement")) {
            return false;
        }
        auto& state = statement_states[statement_id.index()];
        if (state == VisitState::Visiting) {
            return fail(
                TargetUnitViolationKind::InvalidCycle,
                std::format(
                    "target statement graph contains a cycle at ID {}",
                    statement_id.index()
                )
            );
        }
        if (state == VisitState::Visited) {
            return fail(
                TargetUnitViolationKind::InvalidOwnership,
                std::format("target statement ID {} has multiple owners", statement_id.index())
            );
        }
        state = VisitState::Visiting;
        const auto& statement = unit.statements[statement_id.index()];
        if (!verify_attribution(statement.attribution, "statement", statement_id.index())
            || !visit_statement_value(statement.value)) {
            return false;
        }
        state = VisitState::Visited;
        return true;
    }

    auto visit_statement_value(const TargetStmtValue& statement) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetExprStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const TargetDiscardStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const TargetReturnStmt& value) noexcept {
                    return !value.expression.has_value() || visit_expression(*value.expression);
                },
                [&](const TargetVariableStmt& value) noexcept {
                    return visit_type(value.type) && visit_expression(value.initializer);
                },
                [&](const TargetBlockStmt& value) noexcept {
                    return visit_statements(value.statements);
                },
                [&](const TargetAssignmentStmt& value) noexcept {
                    return visit_expression(value.target) && visit_expression(value.value);
                },
                [&](const TargetUpdateStmt& value) noexcept {
                    return visit_expression(value.target);
                },
                [](const TargetBreakStmt&) static noexcept { return true; },
                [](const TargetContinueStmt&) static noexcept { return true; },
                [](const TargetGotoStmt&) static noexcept { return true; },
                [](const TargetLabelStmt&) static noexcept { return true; },
                [&](const TargetIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        if (!visit_expression(branch.condition) || !visit_statements(branch.body)) {
                            return false;
                        }
                    }
                    return !value.else_body.has_value() || visit_statements(*value.else_body);
                },
                [&](const TargetWhileStmt& value) noexcept {
                    return visit_expression(value.condition) && visit_statements(value.body);
                },
                [&](const TargetForStmt& value) noexcept {
                    if (value.initializer.has_value()
                        && !visit_for_initializer(*value.initializer)) {
                        return false;
                    }
                    if (value.condition.has_value() && !visit_expression(*value.condition)) {
                        return false;
                    }
                    for (auto index = 0uz; index < value.steps.size(); ++index) {
                        if (!visit_for_step(value.steps[index])) {
                            return false;
                        }
                    }
                    return visit_statements(value.body);
                },
                [&](const TargetRangeForStmt& value) noexcept {
                    return visit_type(value.type)
                        && visit_expression(value.iterable)
                        && visit_statements(value.body);
                },
                [](const TargetRawFragment&) static noexcept { return true; },
            },
            statement
        );
    }

    auto visit_parameters(std::span<const TargetParameter> parameters) noexcept -> bool {
        return std::ranges::all_of(parameters, [&](const TargetParameter& parameter) noexcept {
            return visit_type(parameter.type);
        });
    }

    auto visit_member_function(const TargetMemberFunctionDecl& function) noexcept -> bool {
        if (function.declaration_only && function.defaulted) {
            return fail(
                TargetUnitViolationKind::InvalidStructure,
                "target member function cannot be both declaration-only and defaulted"
            );
        }
        if ((function.declaration_only || function.defaulted) && !function.body.empty()) {
            return fail(
                TargetUnitViolationKind::InvalidStructure,
                "target member function has a body hidden by its declaration form"
            );
        }
        return visit_parameters(function.parameters)
            && visit_type(function.result)
            && (function.declaration_only || function.defaulted || visit_statements(function.body));
    }

    auto visit_record_member(const TargetRecordMember& member) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetStructField& value) noexcept { return visit_type(value.type); },
                [&](const TargetMemberFunctionDecl& value) noexcept {
                    return visit_member_function(value);
                },
            },
            member
        );
    }

    auto visit_record_members(std::span<const TargetRecordMember> members) noexcept -> bool {
        return std::ranges::all_of(members, [&](const TargetRecordMember& member) noexcept {
            return visit_record_member(member);
        });
    }

    auto visit_class_member(const TargetClassMember& member) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetMemberVariable& value) noexcept { return visit_type(value.type); },
                [&](const TargetNestedRecord& value) noexcept {
                    return visit_record_members(value.members);
                },
                [&](const TargetTypeAlias& value) noexcept { return visit_type(value.type); },
                [&](const TargetConstructorDecl& value) noexcept {
                    if (!visit_parameters(value.parameters)) {
                        return false;
                    }
                    return std::ranges::all_of(
                        value.initializers,
                        [&](const TargetMemberInitializer& initializer) noexcept {
                            return visit_expression(initializer.value);
                        }
                    );
                },
                [&](const TargetMemberFunctionDecl& value) noexcept {
                    return visit_member_function(value);
                },
            },
            member
        );
    }

    auto visit_declaration(const TargetDecl& declaration) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetFunctionDecl& value) noexcept {
                    if (value.declaration_only && !value.body.empty()) {
                        return fail(
                            TargetUnitViolationKind::InvalidStructure,
                            "target declaration-only function has a renderer-invisible body"
                        );
                    }
                    return visit_parameters(value.parameters)
                        && visit_type(value.result)
                        && (value.declaration_only || visit_statements(value.body));
                },
                [&](const TargetStructDecl& value) noexcept {
                    return visit_record_members(value.members);
                },
                [](const TargetStructForwardDecl&) static noexcept { return true; },
                [&](const TargetEnumDecl& value) noexcept {
                    if (!visit_type(value.underlying_type)) {
                        return false;
                    }
                    return std::ranges::all_of(
                        value.cases,
                        [&](const TargetEnumCase& enum_case) noexcept {
                            return visit_expression(enum_case.value);
                        }
                    );
                },
                [&](const TargetEnumForwardDecl& value) noexcept {
                    return visit_type(value.underlying_type);
                },
                [&](const TargetClassDecl& value) noexcept {
                    for (const auto& section : value.sections) {
                        for (const auto& member : section.members) {
                            if (!visit_class_member(member)) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                [](const TargetClassForwardDecl&) static noexcept { return true; },
                [&](const TargetVariableDecl& value) noexcept {
                    return visit_type(value.type) && visit_expression(value.initializer);
                },
            },
            declaration
        );
    }

    auto visit_items(std::span<const TargetItemID> item_ids) noexcept -> bool {
        return std::ranges::all_of(item_ids, [&](TargetItemID item_id) noexcept {
            return visit_item(item_id);
        });
    }

    auto visit_item(TargetItemID item_id) noexcept -> bool {
        if (!verify_bounds(item_id, unit.items.size(), "item")) {
            return false;
        }
        auto& state = item_states[item_id.index()];
        if (state == VisitState::Visiting) {
            return fail(
                TargetUnitViolationKind::InvalidCycle,
                std::format("target item graph contains a cycle at ID {}", item_id.index())
            );
        }
        if (state == VisitState::Visited) {
            return fail(
                TargetUnitViolationKind::InvalidOwnership,
                std::format("target item ID {} has multiple owners", item_id.index())
            );
        }
        state = VisitState::Visiting;
        const auto& item = unit.items[item_id.index()];
        if (!verify_attribution(item.attribution, "item", item_id.index())
            || !std::visit(
                Overloaded {
                    [&](const TargetDecl& value) noexcept { return visit_declaration(value); },
                    [&](const TargetNamespace& value) noexcept { return visit_items(value.items); },
                    [](const TargetRawFragment&) static noexcept { return true; },
                    [&](const TargetItemGroup& value) noexcept { return visit_items(value.items); },
                },
                item.value
            )) {
            return false;
        }
        state = VisitState::Visited;
        return true;
    }

    auto visit_sections(const TargetUnitSections& sections) noexcept -> bool {
        return visit_items(sections.preamble)
            && visit_items(sections.body)
            && visit_items(sections.epilogue);
    }

    auto visit_root() noexcept -> bool {
        if (unit.root.logical_path.empty()) {
            return fail(TargetUnitViolationKind::InvalidStructure, "target unit path is empty");
        }
        const auto stable_interface = unit.root.role == GeneratedArtifactRole::Interface;
        if (stable_interface
            != (unit.root.source_mapping == ArtifactSourceMappingPolicy::StableInterface)) {
            return fail(
                TargetUnitViolationKind::InvalidStructure,
                "target unit role and source-mapping policy disagree"
            );
        }
        if (std::ranges::any_of(
                unit.root.directive_groups,
                [](const TargetDirectiveGroup& group) static noexcept {
                    return group.directives.empty()
                        || std::ranges::any_of(
                               group.directives,
                               [](const TargetDirective& directive) static noexcept {
                                   return directive.bytes.empty()
                                       || !directive.bytes.starts_with('#');
                               }
                        );
                }
            )) {
            return fail(
                TargetUnitViolationKind::InvalidStructure,
                "target unit contains an invalid directive"
            );
        }
        const auto has_pragma_once = std::ranges::any_of(
            unit.root.directive_groups,
            [](const TargetDirectiveGroup& group) static noexcept {
                return std::ranges::any_of(
                    group.directives,
                    [](const TargetDirective& directive) static noexcept {
                        return directive.bytes == "#pragma once";
                    }
                );
            }
        );
        if (has_pragma_once != stable_interface) {
            return fail(
                TargetUnitViolationKind::InvalidStructure,
                "target unit role and directive policy disagree"
            );
        }
        return visit_sections(unit.root.sections);
    }

    auto verify_no_orphans(std::span<const VisitState> states, std::string_view category) noexcept
        -> bool {
        for (const auto& [index, state] : std::views::enumerate(states)) {
            if (state == VisitState::Unseen) {
                return fail(
                    TargetUnitViolationKind::OrphanNode,
                    std::format(
                        "target {} ID {} is not reachable from the unit root",
                        category,
                        index
                    )
                );
            }
        }
        return true;
    }

    auto verify_all_reached() noexcept -> bool {
        return verify_no_orphans(item_states, "item")
            && verify_no_orphans(statement_states, "statement")
            && verify_no_orphans(expression_states, "expression")
            && verify_no_orphans(type_states, "type");
    }

    TargetUnitValidationView unit;
    std::vector<VisitState> type_states;
    std::vector<VisitState> expression_states;
    std::vector<VisitState> statement_states;
    std::vector<VisitState> item_states;
    std::optional<TargetUnitViolation> failure;
};

struct JumpSite final {
    std::string label;
    TargetSyntheticControlKind kind;
    std::vector<std::size_t> barriers;
    std::size_t order;
};

struct LabelSite final {
    TargetSyntheticControlKind kind;
    std::vector<std::size_t> barriers;
    std::size_t order;
};

class SyntheticControlVerifier final {
public:
    explicit SyntheticControlVerifier(TargetUnitValidationView unit) noexcept
        : unit(unit) {}

    auto run() noexcept -> std::expected<void, TargetUnitViolation> {
        for (const auto& item : unit.items) {
            if (const auto* declaration = std::get_if<TargetDecl>(&item.value);
                declaration != nullptr && !visit_declaration(*declaration)) {
                return std::unexpected(std::move(*failure));
            }
        }
        for (const auto& expression : unit.expressions) {
            if (const auto* lambda = std::get_if<TargetLambdaExpr>(&expression.value)) {
                if (!verify_body(lambda->body)) {
                    return std::unexpected(std::move(*failure));
                }
            } else if (const auto* closure = std::get_if<TargetClosureExpr>(&expression.value)) {
                if (!verify_body(closure->body)) {
                    return std::unexpected(std::move(*failure));
                }
            }
        }
        return {};
    }

private:
    auto fail(std::string message) noexcept -> bool {
        failure = TargetUnitViolation {
            .kind = TargetUnitViolationKind::InvalidControl,
            .message = std::move(message),
        };
        return false;
    }

    auto statement(TargetStmtID statement_id) const noexcept -> const TargetStmt& {
        return unit.statements[statement_id.index()];
    }

    auto verify_body(std::span<const TargetStmtID> body_ids) noexcept -> bool {
        labels.clear();
        jumps.clear();
        next_barrier = 0;
        next_statement_order = 0;
        auto barriers = std::vector<std::size_t>();
        if (!walk_sequence(body_ids, barriers, 0)) {
            return false;
        }
        for (const auto& jump : jumps) {
            const auto found = labels.find(jump.label);
            if (found == labels.end()) {
                return fail(
                    std::format("synthetic target jump references missing label '{}'", jump.label)
                );
            }
            if (jump.kind != found->second.kind) {
                return fail(
                    std::format(
                        "synthetic target jump '{}' and its label have different roles",
                        jump.label
                    )
                );
            }
            if (jump.order >= found->second.order) {
                return fail(std::format("synthetic target jump '{}' is not forward", jump.label));
            }
            if (!std::ranges::starts_with(jump.barriers, found->second.barriers)) {
                return fail(
                    std::format(
                        "synthetic target jump '{}' enters a scope or crosses an initialization",
                        jump.label
                    )
                );
            }
        }
        return true;
    }

    auto scoped(std::vector<std::size_t> barriers) noexcept -> std::vector<std::size_t> {
        barriers.push_back(next_barrier++);
        return barriers;
    }

    auto walk_sequence(
        std::span<const TargetStmtID> statement_ids,
        std::vector<std::size_t>& barriers,
        std::size_t loop_depth
    ) noexcept -> bool {
        for (const auto statement_id : statement_ids) {
            if (!walk_statement(statement(statement_id), barriers, loop_depth)) {
                return false;
            }
        }
        return true;
    }

    auto walk_statement(
        const TargetStmt& current,
        std::vector<std::size_t>& barriers,
        std::size_t loop_depth
    ) noexcept -> bool {
        const auto statement_order = next_statement_order++;
        return std::visit(
            Overloaded {
                [](const TargetExprStmt&) static noexcept { return true; },
                [](const TargetDiscardStmt&) static noexcept { return true; },
                [](const TargetReturnStmt&) static noexcept { return true; },
                [&](const TargetVariableStmt&) noexcept {
                    barriers.push_back(next_barrier++);
                    return true;
                },
                [&](const TargetBlockStmt& value) noexcept {
                    auto nested = value.scoped ? scoped(barriers) : barriers;
                    return walk_sequence(value.statements, nested, loop_depth);
                },
                [](const TargetAssignmentStmt&) static noexcept { return true; },
                [](const TargetUpdateStmt&) static noexcept { return true; },
                [&](const TargetBreakStmt&) noexcept {
                    return loop_depth != 0 || fail("target break is outside a loop");
                },
                [&](const TargetContinueStmt&) noexcept {
                    return loop_depth != 0 || fail("target continue is outside a loop");
                },
                [&](const TargetGotoStmt& value) noexcept {
                    if (value.kind == TargetSyntheticControlKind::NormalizedForContinue
                        && loop_depth == 0) {
                        return fail("normalized-for continue jump is outside a loop");
                    }
                    jumps.push_back({
                        .label = std::string(value.label.spelling()),
                        .kind = value.kind,
                        .barriers = barriers,
                        .order = statement_order,
                    });
                    return true;
                },
                [&](const TargetLabelStmt& value) noexcept {
                    if (value.kind == TargetSyntheticControlKind::NormalizedForContinue
                        && loop_depth == 0) {
                        return fail("normalized-for continue label is outside a loop");
                    }
                    const auto label = std::string(value.label.spelling());
                    return labels
                               .emplace(
                                   label,
                                   LabelSite {
                                       .kind = value.kind,
                                       .barriers = barriers,
                                       .order = statement_order,
                                   }
                               )
                               .second
                        || fail(std::format("synthetic target label '{}' is repeated", label));
                },
                [&](const TargetIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        auto nested = scoped(barriers);
                        if (!walk_sequence(branch.body, nested, loop_depth)) {
                            return false;
                        }
                    }
                    if (value.else_body.has_value()) {
                        auto nested = scoped(barriers);
                        if (!walk_sequence(*value.else_body, nested, loop_depth)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const TargetWhileStmt& value) noexcept {
                    auto nested = scoped(barriers);
                    return walk_sequence(value.body, nested, loop_depth + 1);
                },
                [&](const TargetForStmt& value) noexcept {
                    auto nested = scoped(barriers);
                    if (value.initializer.has_value()
                        && std::holds_alternative<TargetVariableStmt>(value.initializer->value)) {
                        nested.push_back(next_barrier++);
                    }
                    return walk_sequence(value.body, nested, loop_depth + 1);
                },
                [&](const TargetRangeForStmt& value) noexcept {
                    auto nested = scoped(barriers);
                    nested.push_back(next_barrier++);
                    return walk_sequence(value.body, nested, loop_depth + 1);
                },
                [](const TargetRawFragment&) static noexcept { return true; },
            },
            current.value
        );
    }

    auto visit_member(const TargetRecordMember& member) noexcept -> bool {
        const auto* function = std::get_if<TargetMemberFunctionDecl>(&member);
        return function == nullptr
            || function->declaration_only
            || function->defaulted
            || verify_body(function->body);
    }

    auto visit_declaration(const TargetDecl& declaration) noexcept -> bool {
        return std::visit(
            Overloaded {
                [&](const TargetFunctionDecl& value) noexcept {
                    return value.declaration_only || verify_body(value.body);
                },
                [&](const TargetStructDecl& value) noexcept {
                    return std::ranges::all_of(value.members, [&](const auto& member) noexcept {
                        return visit_member(member);
                    });
                },
                [](const TargetStructForwardDecl&) static noexcept { return true; },
                [](const TargetEnumDecl&) static noexcept { return true; },
                [](const TargetEnumForwardDecl&) static noexcept { return true; },
                [&](const TargetClassDecl& value) noexcept {
                    for (const auto& section : value.sections) {
                        for (const auto& member : section.members) {
                            const auto valid = std::visit(
                                Overloaded {
                                    [&](const TargetNestedRecord& record) noexcept {
                                        return std::ranges::all_of(
                                            record.members,
                                            [&](const auto& nested) noexcept {
                                                return visit_member(nested);
                                            }
                                        );
                                    },
                                    [&](const TargetMemberFunctionDecl& function) noexcept {
                                        return function.declaration_only
                                            || function.defaulted
                                            || verify_body(function.body);
                                    },
                                    [](const auto&) static noexcept { return true; },
                                },
                                member
                            );
                            if (!valid) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                [](const TargetClassForwardDecl&) static noexcept { return true; },
                [](const TargetVariableDecl&) static noexcept { return true; },
            },
            declaration
        );
    }

    TargetUnitValidationView unit;
    std::flat_map<std::string, LabelSite> labels;
    std::vector<JumpSite> jumps;
    std::size_t next_barrier = 0;
    std::size_t next_statement_order = 0;
    std::optional<TargetUnitViolation> failure;
};

} // namespace

auto validate_target_unit(TargetUnitValidationView unit) noexcept
    -> std::expected<void, TargetUnitViolation> {
    const auto structure = TargetUnitStructureVerifier(unit).run();
    if (!structure.has_value()) {
        return structure;
    }
    return SyntheticControlVerifier(unit).run();
}
