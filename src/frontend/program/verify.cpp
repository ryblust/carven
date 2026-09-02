module carven:frontend.program.verify.impl;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.pattern;
import :frontend.ast.interop;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.type;
import :frontend.program;
import :frontend.program.verify;
import :source.provenance.ids;
import :source.provenance.verify;
import :support.visit;
import std;

namespace {

auto verification_error(SyntaxProgramErrorKind kind, std::string message) noexcept
    -> std::unexpected<SyntaxProgramError> {
    return std::unexpected(
        SyntaxProgramError {
            .kind = kind,
            .message = std::move(message),
        }
    );
}

class SyntaxTreeVerifier final {
public:
    SyntaxTreeVerifier(
        ASTView syntax_value,
        std::size_t source_size_value,
        std::string_view module_path_value
    ) noexcept
        : syntax(syntax_value),
          source_size(source_size_value),
          module_path(module_path_value) {}

    auto verify() noexcept -> std::expected<void, SyntaxProgramError> {
        verify_module();
        verify_module_imports();
        verify_items();
        verify_types();
        verify_patterns();
        verify_expressions();
        verify_statements();
        verify_blocks();
        verify_branch_blocks();
        if (error.has_value()) {
            return std::unexpected(std::move(*error));
        }
        return {};
    }

private:
    auto check_span(Span span, std::string_view entity) noexcept -> void {
        if (!error.has_value() && span.end() > source_size) {
            error = SyntaxProgramError {
                .kind = SyntaxProgramErrorKind::SpanOutOfBounds,
                .message = std::format(
                    "{} in syntax tree for module '{}' extends beyond its source snapshot",
                    entity,
                    module_path
                ),
            };
        }
    }

    template<typename ID>
    auto check_id(ID id, std::size_t bound, std::string_view entity) noexcept -> void {
        if (!error.has_value() && id.index() >= bound) {
            error = SyntaxProgramError {
                .kind = SyntaxProgramErrorKind::ChildIDOutOfBounds,
                .message = std::format(
                    "{} in syntax tree for module '{}' references child @{} outside a table of size {}",
                    entity,
                    module_path,
                    id.index(),
                    bound
                ),
            };
        }
    }

    auto expression(ASTExprID id, std::string_view entity) noexcept -> void {
        check_id(id, syntax.expressions().size(), entity);
    }

    auto type(ASTTypeID id, std::string_view entity) noexcept -> void {
        check_id(id, syntax.types().size(), entity);
    }

    auto statement(ASTStmtID id, std::string_view entity) noexcept -> void {
        check_id(id, syntax.statements().size(), entity);
    }

    auto pattern(ASTPatternID id, std::string_view entity) noexcept -> void {
        check_id(id, syntax.patterns().size(), entity);
    }

    auto block(ASTBlockID id, std::string_view entity) noexcept -> void {
        check_id(id, syntax.blocks().size(), entity);
    }

    auto branch_block(ASTBranchBlockID id, std::string_view entity) noexcept -> void {
        check_id(id, syntax.branch_blocks().size(), entity);
    }

    auto verify_literal(const ASTLiteral& value) noexcept -> void {
        check_span(value.span, "literal span");
    }

    auto verify_binding_target(const ASTBindingTarget& target) noexcept -> void {
        std::visit(
            [&](const auto& value) noexcept {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, ASTNamedBindingTarget>) {
                    check_span(value.name_span, "binding name");
                } else {
                    check_span(value.underscore_span, "discard binding");
                }
            },
            target
        );
    }

    auto verify_access(const ASTAccessSyntax& access) noexcept -> void {
        if (access.marker.has_value()) {
            check_span(*access.marker, "access marker");
        }
    }

    auto verify_throw_clause(const ASTThrowClause& clause) noexcept -> void {
        check_span(clause.span, "throw clause");
        check_span(clause.keyword_span, "throw keyword");
        for (const auto failure : clause.failures) {
            type(failure, "throw clause");
        }
        for (const auto span : clause.plus_spans) {
            check_span(span, "throw-clause separator");
        }
    }

    auto verify_parameter(const ASTFunctionParameter& parameter) noexcept -> void {
        check_span(parameter.span, "function parameter");
        verify_access(parameter.access);
        verify_binding_target(parameter.target);
        if (parameter.type.has_value()) {
            type(*parameter.type, "function parameter");
        }
    }

    auto verify_named_type(const ASTNamedType& named) noexcept -> void {
        for (const auto& component : named.components) {
            check_span(component.name_span, "type-name component");
        }
    }

    auto verify_function_type(const ASTFunctionType& function) noexcept -> void {
        for (const auto& parameter : function.parameters) {
            check_span(parameter.span, "callable-view parameter");
            verify_access(parameter.access);
            type(parameter.type, "callable-view parameter");
        }
        type(function.result_type, "callable-view result");
        if (function.throw_clause.has_value()) {
            verify_throw_clause(*function.throw_clause);
        }
    }

    auto verify_array_type(const ASTArrayType& array) noexcept -> void {
        type(array.element_type, "array element type");
        expression(array.extent, "array extent");
    }

    auto verify_construction_type(const ASTConstructionType& value) noexcept -> void {
        check_span(value.span, "construction type");
        std::visit(
            Overloaded {
                [&](const ASTNamedType& named) noexcept { verify_named_type(named); },
                [&](const ASTFunctionType& function) noexcept { verify_function_type(function); },
            },
            value.value
        );
    }

    auto verify_control_transfer(const ASTControlTransfer& transfer) noexcept -> void {
        check_span(transfer.span, "control transfer");
        check_span(transfer.keyword_span, "control-transfer keyword");
        if (transfer.value.has_value()) {
            expression(*transfer.value, "control-transfer value");
        }
    }

    auto verify_branch_body(const ASTMatchArmBody& body_value) noexcept -> void {
        check_span(body_value.span, "match-arm body");
        std::visit(
            Overloaded {
                [&](ASTExprID value) noexcept { expression(value, "match-arm expression"); },
                [&](const ASTControlTransfer& value) noexcept { verify_control_transfer(value); },
                [&](ASTBranchBlockID value) noexcept { branch_block(value, "match-arm block"); },
            },
            body_value.value
        );
    }

    auto verify_guard(const ASTGuard& guard) noexcept -> void {
        check_span(guard.keyword_span, "guard keyword");
        expression(guard.expression, "guard expression");
    }

    auto verify_if(const ASTIfForm& conditional) noexcept -> void {
        check_span(conditional.span, "if form");
        for (const auto& branch : conditional.branches) {
            check_span(branch.keyword_span, "if-branch keyword");
            expression(branch.condition, "if-branch condition");
            branch_block(branch.body, "if-branch body");
        }
        if (conditional.else_branch.has_value()) {
            branch_block(*conditional.else_branch, "else branch");
        }
    }

    auto verify_match(const ASTMatchForm& match) noexcept -> void {
        check_span(match.span, "match form");
        check_span(match.keyword_span, "match keyword");
        expression(match.subject, "match subject");
        for (const auto& arm : match.arms) {
            check_span(arm.span, "match arm");
            pattern(arm.pattern, "match-arm pattern");
            if (arm.guard.has_value()) {
                verify_guard(*arm.guard);
            }
            check_span(arm.arrow_span, "match-arm arrow");
            verify_branch_body(arm.body);
        }
    }

    auto verify_try(const ASTTryForm& attempt) noexcept -> void {
        check_span(attempt.span, "try form");
        check_span(attempt.try_span, "try keyword");
        branch_block(attempt.body, "try body");
        check_span(attempt.catch_span, "catch keyword");
        for (const auto& arm : attempt.arms) {
            check_span(arm.span, "catch arm");
            check_span(arm.pattern.span, "catch pattern");
            for (const auto& alternative : arm.pattern.alternatives) {
                check_span(alternative.span, "catch-pattern alternative");
                std::visit(
                    Overloaded {
                        [&](const ASTCatchWildcardPattern& wildcard) noexcept {
                            check_span(wildcard.underscore_span, "catch wildcard");
                        },
                        [&](const ASTCatchTypedPattern& typed) noexcept {
                            type(typed.type, "catch failure type");
                            check_span(
                                typed.left_parenthesis_span,
                                "catch-pattern left parenthesis"
                            );
                            pattern(typed.inner, "catch inner pattern");
                            check_span(
                                typed.right_parenthesis_span,
                                "catch-pattern right parenthesis"
                            );
                        },
                    },
                    alternative.value
                );
            }
            for (const auto pipe : arm.pattern.pipe_spans) {
                check_span(pipe, "catch-pattern separator");
            }
            if (arm.guard.has_value()) {
                verify_guard(*arm.guard);
            }
            check_span(arm.arrow_span, "catch-arm arrow");
            verify_branch_body(arm.body);
        }
    }

    auto verify_module_reference(const ASTModuleReference& reference) noexcept -> void {
        check_span(reference.span, "module reference");
        std::visit(
            Overloaded {
                [&](const ASTDomainRootModuleReference& value) noexcept {
                    for (const auto component : value.components) {
                        check_span(component, "module-path component");
                    }
                },
                [&](const ASTParentRelativeModuleReference& value) noexcept {
                    check_span(value.prefix_span, "parent-relative module prefix");
                    for (const auto component : value.components) {
                        check_span(component, "module-path component");
                    }
                },
                [&](const ASTCraftQualifiedModuleReference& value) noexcept {
                    check_span(value.name_span, "craft name");
                    check_span(value.separator_span, "craft module separator");
                    for (const auto component : value.components) {
                        check_span(component, "module-path component");
                    }
                },
            },
            reference.value
        );
    }

    auto verify_module() noexcept -> void {
        const auto& ast_module = syntax.ast_module();
        check_span(ast_module.span, "syntax root");
        for (const auto import_id : ast_module.module_imports) {
            check_id(import_id, syntax.module_imports().size(), "module import");
        }
        for (const auto& header : ast_module.cpp_header_imports) {
            check_span(header.span, "C++ header import");
            check_span(header.name_span, "C++ header name");
        }
        for (const auto& fragment : ast_module.cpp_source_fragments) {
            check_span(fragment.form_span, "C++ source fragment form");
            check_span(fragment.payload_span, "C++ source fragment payload");
        }
        for (const auto item_id : ast_module.items) {
            check_id(item_id, syntax.items().size(), "module item");
        }
    }

    auto verify_module_imports() noexcept -> void {
        for (const auto& declaration : syntax.module_imports()) {
            check_span(declaration.span, "import declaration");
            verify_module_reference(declaration.module_reference);
            check_span(declaration.selection.span, "import selection");
            std::visit(
                Overloaded {
                    [&](const ASTSingleImport& value) noexcept {
                        check_span(value.name_span, "imported name");
                    },
                    [](const ASTWildcardImport&) static noexcept {},
                    [&](const ASTImportList& value) noexcept {
                        for (const auto name : value.names) {
                            check_span(name, "imported name");
                        }
                    },
                },
                declaration.selection.value
            );
        }
    }

    auto verify_items() noexcept -> void {
        for (const auto& item : syntax.items()) {
            check_span(item.span, "module item");
            std::visit(
                Overloaded {
                    [&](const ASTEnumDecl& value) noexcept {
                        check_span(value.name_span, "enum name");
                        if (value.underlying_type.has_value()) {
                            type(*value.underlying_type, "enum underlying type");
                        }
                        for (const auto& enum_case : value.cases) {
                            check_span(enum_case.span, "enum case");
                            check_span(enum_case.name_span, "enum-case name");
                            for (const auto payload : enum_case.payload_types) {
                                type(payload, "enum-case payload");
                            }
                            if (enum_case.initializer.has_value()) {
                                expression(*enum_case.initializer, "enum-case initializer");
                            }
                        }
                    },
                    [&](const ASTStructDecl& value) noexcept {
                        check_span(value.name_span, "struct name");
                        for (const auto& field : value.fields) {
                            check_span(field.span, "struct field");
                            check_span(field.name_span, "struct-field name");
                            type(field.type, "struct-field type");
                        }
                    },
                    [&](const ASTFunctionDecl& value) noexcept {
                        if (value.cpp_export.has_value()) {
                            check_span(value.cpp_export->span, "export(cpp) form");
                        }
                        check_span(value.name_span, "function name");
                        for (const auto& parameter : value.parameters) {
                            verify_parameter(parameter);
                        }
                        if (value.result_type.has_value()) {
                            type(*value.result_type, "function result type");
                        }
                        if (value.throw_clause.has_value()) {
                            verify_throw_clause(*value.throw_clause);
                        }
                        std::visit(
                            Overloaded {
                                [&](const ASTFunctionBody& implementation) noexcept {
                                    block(implementation.body, "function body");
                                },
                                [&](const ASTCppImportForm& implementation) noexcept {
                                    check_span(implementation.span, "import(cpp) form");
                                },
                            },
                            value.implementation
                        );
                    },
                    [&](const ASTConstantDecl& value) noexcept {
                        check_span(value.name_span, "constant name");
                        if (value.type.has_value()) {
                            type(*value.type, "constant type");
                        }
                        expression(value.initializer, "constant initializer");
                    },
                    [&](const ASTTestDecl& value) noexcept {
                        check_span(value.keyword_span, "test keyword");
                        check_span(value.name_span, "test name");
                        block(value.body, "test body");
                    },
                },
                item.value
            );
        }
    }

    auto verify_types() noexcept -> void {
        for (const auto& type_value : syntax.types()) {
            check_span(type_value.span, "type");
            std::visit(
                Overloaded {
                    [&](const ASTNamedType& value) noexcept { verify_named_type(value); },
                    [&](const ASTArrayType& value) noexcept { verify_array_type(value); },
                    [&](const ASTFunctionType& value) noexcept { verify_function_type(value); },
                },
                type_value.value
            );
        }
    }

    auto verify_patterns() noexcept -> void {
        for (const auto& pattern_value : syntax.patterns()) {
            check_span(pattern_value.span, "pattern");
            std::visit(
                Overloaded {
                    [&](const ASTWildcardPattern& value) noexcept {
                        check_span(value.underscore_span, "wildcard pattern");
                    },
                    [&](const ASTLiteral& value) noexcept { verify_literal(value); },
                    [&](const ASTNegativeNumberPattern& value) noexcept {
                        check_span(value.minus_span, "negative-pattern sign");
                        check_span(value.number_span, "negative-pattern number");
                    },
                    [&](const ASTBindingPattern& value) noexcept {
                        check_span(value.name_span, "pattern binding");
                    },
                    [&](const ASTConstraintPattern& value) noexcept {
                        check_span(value.is_span, "type-pattern keyword");
                        check_span(value.operand.span, "type-pattern operand");
                        std::visit(
                            Overloaded {
                                [&](const ASTQualifiedName& name) noexcept {
                                    check_span(name.span, "qualified type name");
                                    for (const auto component : name.components) {
                                        check_span(component, "qualified-name component");
                                    }
                                },
                                [&](const ASTArrayType& array) noexcept {
                                    verify_array_type(array);
                                },
                            },
                            value.operand.value
                        );
                    },
                    [&](const ASTCasePattern& value) noexcept {
                        std::visit(
                            Overloaded {
                                [&](const ASTContextualCaseQualifier& qualifier) noexcept {
                                    check_span(qualifier.dot_span, "contextual-case qualifier");
                                },
                                [&](const ASTQualifiedCaseQualifier& qualifier) noexcept {
                                    check_span(qualifier.span, "qualified-case qualifier");
                                    for (const auto component : qualifier.components) {
                                        check_span(component, "qualified-case component");
                                    }
                                    check_span(
                                        qualifier.separator_span,
                                        "qualified-case separator"
                                    );
                                },
                            },
                            value.qualifier
                        );
                        check_span(value.name_span, "case-pattern name");
                        if (value.payload.has_value()) {
                            check_span(
                                value.payload->left_parenthesis_span,
                                "case-pattern left parenthesis"
                            );
                            for (const auto child : value.payload->patterns) {
                                pattern(child, "case payload pattern");
                            }
                            check_span(
                                value.payload->right_parenthesis_span,
                                "case-pattern right parenthesis"
                            );
                        }
                    },
                    [&](const ASTOrPattern& value) noexcept {
                        for (const auto alternative : value.alternatives) {
                            pattern(alternative, "or-pattern alternative");
                        }
                        for (const auto pipe : value.pipe_spans) {
                            check_span(pipe, "or-pattern separator");
                        }
                    },
                },
                pattern_value.value
            );
        }
    }

    auto verify_expressions() noexcept -> void {
        for (const auto& expression_value : syntax.expressions()) {
            check_span(expression_value.span, "expression");
            std::visit(
                Overloaded {
                    [&](const ASTLiteral& value) noexcept { verify_literal(value); },
                    [&](const ASTNameExpr& value) noexcept {
                        check_span(value.name_span, "name expression");
                    },
                    [&](const ASTContextualCaseExpr& value) noexcept {
                        check_span(value.dot_span, "contextual-case dot");
                        check_span(value.name_span, "contextual-case name");
                    },
                    [&](const ASTGroupExpr& value) noexcept {
                        expression(value.expression, "grouped expression");
                    },
                    [&](const ASTArrayExpr& value) noexcept {
                        for (const auto element : value.element_ids) {
                            expression(element, "array element");
                        }
                    },
                    [&](const ASTConstructionExpr& value) noexcept {
                        verify_construction_type(value.type);
                        std::visit(
                            Overloaded {
                                [](const std::monostate&) static noexcept {},
                                [&](const ASTPositionalInitializerList& list) noexcept {
                                    check_span(list.span, "positional initializer");
                                    for (const auto element : list.values) {
                                        expression(element, "positional initializer value");
                                    }
                                },
                                [&](const ASTFieldInitializerList& list) noexcept {
                                    check_span(list.span, "field initializer list");
                                    for (const auto& field : list.fields) {
                                        check_span(field.span, "field initializer");
                                        check_span(field.name_span, "field initializer name");
                                        expression(field.value, "field initializer value");
                                    }
                                },
                            },
                            value.initializer.value
                        );
                    },
                    [&](const ASTPrefixExpr& value) noexcept {
                        check_span(value.operator_span, "prefix operator");
                        expression(value.operand_id, "prefix operand");
                    },
                    [&](const ASTAccessExpr& value) noexcept {
                        check_span(value.marker_span, "access marker");
                        expression(value.operand_id, "access operand");
                    },
                    [&](const ASTBinaryExpr& value) noexcept {
                        expression(value.left, "binary left operand");
                        check_span(value.operator_span, "binary operator");
                        expression(value.right, "binary right operand");
                    },
                    [&](const ASTCastExpr& value) noexcept {
                        expression(value.operand_id, "cast operand");
                        check_span(value.operator_span, "cast operator");
                        type(value.target_type, "cast target type");
                    },
                    [&](const ASTCallExpr& value) noexcept {
                        expression(value.callee, "call callee");
                        for (const auto& argument : value.arguments) {
                            expression(argument.expression, "call argument");
                        }
                    },
                    [&](const ASTIndexExpr& value) noexcept {
                        expression(value.operand_id, "index receiver");
                        expression(value.index, "index operand");
                    },
                    [&](const ASTMemberExpr& value) noexcept {
                        expression(value.operand_id, "member receiver");
                        check_span(value.operator_span, "member operator");
                        check_span(value.name_span, "member name");
                    },
                    [&](const ASTLambdaExpr& value) noexcept {
                        for (const auto& capture : value.captures) {
                            check_span(capture.span, "lambda capture");
                            if (capture.write_marker.has_value()) {
                                check_span(*capture.write_marker, "lambda capture marker");
                            }
                            check_span(capture.name_span, "lambda capture name");
                        }
                        for (const auto& parameter : value.parameters) {
                            verify_parameter(parameter);
                        }
                        if (value.result_type.has_value()) {
                            type(*value.result_type, "lambda result type");
                        }
                        if (value.throw_clause.has_value()) {
                            verify_throw_clause(*value.throw_clause);
                        }
                        block(value.body, "lambda body");
                    },
                    [&](const ASTPropagationExpr& value) noexcept {
                        expression(value.operand_id, "propagation operand");
                        check_span(value.operator_span, "propagation operator");
                    },
                    [&](const ASTIfForm& value) noexcept { verify_if(value); },
                    [&](const ASTMatchForm& value) noexcept { verify_match(value); },
                    [&](const ASTTryForm& value) noexcept { verify_try(value); },
                },
                expression_value.value
            );
        }
    }

    auto verify_variable(const ASTVariableDecl& value) noexcept -> void {
        check_span(value.span, "variable declaration");
        check_span(value.keyword_span, "binding keyword");
        verify_binding_target(value.target);
        if (value.type.has_value()) {
            type(*value.type, "binding type");
        }
        if (value.initializer.has_value()) {
            expression(*value.initializer, "binding initializer");
        }
    }

    auto verify_assignment(const ASTAssignment& value) noexcept -> void {
        check_span(value.span, "assignment");
        expression(value.target, "assignment target");
        check_span(value.operator_span, "assignment operator");
        expression(value.value, "assignment value");
    }

    auto verify_update(const ASTUpdate& value) noexcept -> void {
        check_span(value.span, "update");
        check_span(value.operator_span, "update operator");
        expression(value.target, "update target");
    }

    auto verify_for_header(const ASTForHeader& header) noexcept -> void {
        check_span(header.span, "for header");
        std::visit(
            Overloaded {
                [&](const ASTRangeForHeader& value) noexcept {
                    if (value.write_marker.has_value()) {
                        check_span(*value.write_marker, "range binding marker");
                    }
                    verify_binding_target(value.target);
                    if (value.type.has_value()) {
                        type(*value.type, "range binding type");
                    }
                    std::visit(
                        Overloaded {
                            [&](ASTExprID iterable) noexcept {
                                expression(iterable, "range iterable");
                            },
                            [&](const ASTHalfOpenRange& range) noexcept {
                                expression(range.begin, "range begin");
                                check_span(range.operator_span, "range operator");
                                expression(range.end, "range end");
                            },
                        },
                        value.iterable
                    );
                },
                [&](const ASTCStyleForHeader& value) noexcept {
                    check_span(value.initializer.span, "for initializer");
                    std::visit(
                        Overloaded {
                            [](const std::monostate&) static noexcept {},
                            [&](const ASTVariableDecl& variable) noexcept {
                                verify_variable(variable);
                            },
                            [&](const ASTAssignment& assignment) noexcept {
                                verify_assignment(assignment);
                            },
                            [&](ASTExprID expression_id) noexcept {
                                expression(expression_id, "for initializer expression");
                            },
                        },
                        value.initializer.value
                    );
                    if (value.condition.has_value()) {
                        expression(*value.condition, "for condition");
                    }
                    for (const auto& step : value.steps) {
                        check_span(step.span, "for step");
                        std::visit(
                            Overloaded {
                                [&](const ASTAssignment& assignment) noexcept {
                                    verify_assignment(assignment);
                                },
                                [&](const ASTUpdate& update) noexcept { verify_update(update); },
                                [&](ASTExprID expression_id) noexcept {
                                    expression(expression_id, "for-step expression");
                                },
                            },
                            step.value
                        );
                    }
                },
            },
            header.value
        );
    }

    auto verify_statements() noexcept -> void {
        for (const auto& statement_value : syntax.statements()) {
            check_span(statement_value.span, "statement");
            std::visit(
                Overloaded {
                    [&](const ASTVariableDecl& value) noexcept { verify_variable(value); },
                    [&](const ASTAssignment& value) noexcept { verify_assignment(value); },
                    [&](const ASTUpdate& value) noexcept { verify_update(value); },
                    [&](const ASTExprStatement& value) noexcept {
                        expression(value.expression, "expression statement");
                    },
                    [&](const ASTTestOperationStmt& value) noexcept {
                        check_span(value.keyword_span, "test-operation keyword");
                        for (const auto argument : value.arguments) {
                            expression(argument, "test-operation argument");
                        }
                    },
                    [&](const ASTControlTransfer& value) noexcept {
                        verify_control_transfer(value);
                    },
                    [&](const ASTWhileStmt& value) noexcept {
                        check_span(value.keyword_span, "while keyword");
                        expression(value.condition, "while condition");
                        block(value.body, "while body");
                    },
                    [&](const ASTForStmt& value) noexcept {
                        check_span(value.keyword_span, "for keyword");
                        verify_for_header(value.header);
                        block(value.body, "for body");
                    },
                    [&](const ASTIfForm& value) noexcept { verify_if(value); },
                    [&](const ASTMatchForm& value) noexcept { verify_match(value); },
                    [&](const ASTTryForm& value) noexcept { verify_try(value); },
                },
                statement_value.value
            );
        }
    }

    auto verify_blocks() noexcept -> void {
        for (const auto& block_value : syntax.blocks()) {
            check_span(block_value.span, "block");
            for (const auto statement_id : block_value.statements) {
                statement(statement_id, "block statement");
            }
        }
    }

    auto verify_branch_blocks() noexcept -> void {
        for (const auto& block_value : syntax.branch_blocks()) {
            check_span(block_value.span, "branch block");
            for (const auto statement_id : block_value.statements) {
                statement(statement_id, "branch-block statement");
            }
            if (block_value.result.has_value()) {
                expression(*block_value.result, "branch-block result");
            }
        }
    }

    ASTView syntax;
    std::size_t source_size;
    std::string_view module_path;
    std::optional<SyntaxProgramError> error;
};

auto verify_program(const SyntaxProgram& program) noexcept
    -> std::expected<void, SyntaxProgramError> {
    const auto provenance = program.provenance();
    const auto provenance_verification = verify_compilation_provenance(provenance);
    if (!provenance_verification.has_value()) {
        return verification_error(
            SyntaxProgramErrorKind::InvalidProvenance,
            std::format(
                "syntax program provenance is invalid: {}",
                provenance_verification.error().message
            )
        );
    }

    const auto module_count = provenance.module_records().size();
    if (program.syntax_trees().size() != module_count) {
        return verification_error(
            SyntaxProgramErrorKind::SyntaxTreeCountMismatch,
            std::format(
                "syntax program contains {} modules but {} syntax trees",
                module_count,
                program.syntax_trees().size()
            )
        );
    }

    for (auto index = 0uz; index < module_count; ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        const auto& module_record = provenance.module_record(module_id);
        const auto syntax = program.syntax_tree(module_id).view();
        const auto& source = provenance.source_snapshot(module_record.source_id);
        if (syntax.source_id() != source.manager_source_id()) {
            return verification_error(
                SyntaxProgramErrorKind::SyntaxSourceMismatch,
                std::format(
                    "syntax tree for module '{}' has source identity {}, expected {}",
                    module_record.path.value(),
                    syntax.source_id().index(),
                    source.manager_source_id().index()
                )
            );
        }
        if (syntax.ast_module().span.end() > source.size()) {
            return verification_error(
                SyntaxProgramErrorKind::RootSpanOutOfBounds,
                std::format(
                    "syntax root for module '{}' extends beyond its source snapshot",
                    module_record.path.value()
                )
            );
        }
        const auto verification =
            SyntaxTreeVerifier(syntax, source.size(), module_record.path.value()).verify();
        if (!verification.has_value()) {
            return verification;
        }
    }
    return {};
}

} // namespace

auto verify_syntax_program(const SyntaxProgram& program) noexcept
    -> std::expected<void, SyntaxProgramError> {
    return verify_program(program);
}
