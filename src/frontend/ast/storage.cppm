module carven:frontend.ast.storage;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.type;
import :source.text;
import :support.id_table;
import std;

class ASTBuilder;
struct ASTModule;
class SyntaxTree;

class ASTStorage final {
public:
    ASTStorage(const ASTStorage&) = delete;
    ASTStorage(ASTStorage&&) = default;

    auto operator=(const ASTStorage&) -> ASTStorage& = delete;
    auto operator=(ASTStorage&&) -> ASTStorage& = default;

private:
    using ExprTable = IDTable<ASTExpr, ASTExprID>;
    using TypeTable = IDTable<ASTType, ASTTypeID>;
    using StatementTable = IDTable<ASTStmt, ASTStmtID>;
    using PatternTable = IDTable<ASTPattern, ASTPatternID>;
    using BlockTable = IDTable<ASTBlock, ASTBlockID>;
    using BranchBlockTable = IDTable<ASTBranchBlock, ASTBranchBlockID>;
    using ItemTable = IDTable<ASTItem, ASTItemID>;
    using ModuleImportTable = IDTable<ASTModuleImport, ASTModuleImportID>;

    ASTStorage() = default;

    ExprTable expression_table;
    TypeTable type_table;
    StatementTable statement_table;
    PatternTable pattern_table;
    BlockTable block_table;
    BranchBlockTable branch_block_table;
    ItemTable item_table;
    ModuleImportTable module_import_table;

    friend class ASTBuilder;
    friend class ASTView;
};

class ASTView final {
public:
    auto expression(ASTExprID id) const noexcept -> const ASTExpr&;
    auto type(ASTTypeID id) const noexcept -> const ASTType&;
    auto statement(ASTStmtID id) const noexcept -> const ASTStmt&;
    auto pattern(ASTPatternID id) const noexcept -> const ASTPattern&;
    auto block(ASTBlockID id) const noexcept -> const ASTBlock&;
    auto branch_block(ASTBranchBlockID id) const noexcept -> const ASTBranchBlock&;
    auto item(ASTItemID id) const noexcept -> const ASTItem&;
    auto module_import(ASTModuleImportID id) const noexcept -> const ASTModuleImport&;
    auto ast_module() const noexcept -> const ASTModule&;
    auto expressions() const noexcept -> std::span<const ASTExpr>;
    auto types() const noexcept -> std::span<const ASTType>;
    auto statements() const noexcept -> std::span<const ASTStmt>;
    auto patterns() const noexcept -> std::span<const ASTPattern>;
    auto blocks() const noexcept -> std::span<const ASTBlock>;
    auto branch_blocks() const noexcept -> std::span<const ASTBranchBlock>;
    auto items() const noexcept -> std::span<const ASTItem>;
    auto module_imports() const noexcept -> std::span<const ASTModuleImport>;
    auto source_id() const noexcept -> SourceID;

private:
    ASTView(const ASTStorage& storage, const ASTModule& ast_module, SourceID source) noexcept;

    const ASTStorage* ast_storage;
    const ASTModule* source_module;
    SourceID source_identity;

    friend class SyntaxTree;
};

template<typename Visitor>
class ASTTopologyWalker final {
public:
    explicit ASTTopologyWalker(Visitor& visitor_value) noexcept
        : visitor(std::addressof(visitor_value)) {}

    template<typename Node>
    auto run(const Node& node) noexcept -> void {
        visit(node);
    }

private:
    template<typename... Values>
    auto visit_fields(const Values&... values) noexcept -> void {
        (visit(values), ...);
    }

    auto visit(Span value) noexcept -> void { (*visitor)(value); }

    template<typename ID>
        requires (
            std::same_as<ID, ASTExprID>
            || std::same_as<ID, ASTTypeID>
            || std::same_as<ID, ASTStmtID>
            || std::same_as<ID, ASTPatternID>
            || std::same_as<ID, ASTBlockID>
            || std::same_as<ID, ASTBranchBlockID>
            || std::same_as<ID, ASTItemID>
            || std::same_as<ID, ASTModuleImportID>
        )
    auto visit(ID value) noexcept -> void {
        (*visitor)(value);
    }

    auto visit(const std::monostate&) noexcept -> void {}

    template<typename Value>
    auto visit(const std::optional<Value>& value) noexcept -> void {
        if (value.has_value()) {
            visit(*value);
        }
    }

    template<typename Value>
    auto visit(const std::vector<Value>& values) noexcept -> void {
        for (const auto& value : values) {
            visit(value);
        }
    }

    template<typename... Values>
    auto visit(const std::variant<Values...>& value) noexcept -> void {
        std::visit([&](const auto& alternative) noexcept { visit(alternative); }, value);
    }

    auto visit(const ASTCppUsing& value) noexcept -> void {
        visit_fields(value.span, value.prefix, value.selection);
    }

    auto visit(const ASTCppSingleSelection& value) noexcept -> void { visit(value.name); }

    auto visit(const ASTCppListSelection& value) noexcept -> void { visit(value.names); }

    auto visit(const ASTCppNamespaceSelection& value) noexcept -> void { visit(value.star); }

    auto visit(const ASTCppHeaderImport& value) noexcept -> void {
        visit_fields(value.span, value.name_span, value.using_clause);
    }

    auto visit(const ASTCppExportForm& value) noexcept -> void { visit(value.span); }

    auto visit(const ASTCppImportForm& value) noexcept -> void { visit(value.span); }

    auto visit(const ASTCppSourceFragment& value) noexcept -> void {
        visit_fields(value.form_span, value.payload_span);
    }

    auto visit(const ASTAccessSyntax& value) noexcept -> void { visit(value.marker); }

    auto visit(const ASTTypeNameComponent& value) noexcept -> void { visit(value.name_span); }

    auto visit(const ASTNamedType& value) noexcept -> void {
        visit(value.global_root);
        visit_fields(value.components, value.arguments);
    }

    auto visit(const ASTArrayType& value) noexcept -> void {
        visit_fields(value.element_type, value.extent);
    }

    auto visit(const ASTFunctionTypeParameter& value) noexcept -> void {
        visit_fields(value.span, value.access, value.type);
    }

    auto visit(const ASTThrowClause& value) noexcept -> void {
        visit_fields(value.span, value.keyword_span, value.failures, value.plus_spans);
    }

    auto visit(const ASTFunctionType& value) noexcept -> void {
        visit_fields(value.parameters, value.result_type, value.throw_clause);
    }

    auto visit(const ASTType& value) noexcept -> void { visit_fields(value.span, value.value); }

    auto visit(const ASTConstructionType& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTLiteral& value) noexcept -> void { visit(value.span); }

    auto visit(const ASTControlTransfer& value) noexcept -> void {
        visit_fields(value.span, value.keyword_span, value.value);
    }

    auto visit(const ASTIfForm::Branch& value) noexcept -> void {
        visit_fields(value.keyword_span, value.condition, value.body);
    }

    auto visit(const ASTIfForm& value) noexcept -> void {
        visit_fields(value.span, value.branches, value.else_branch);
    }

    auto visit(const ASTMatchArmBody& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTGuard& value) noexcept -> void {
        visit_fields(value.keyword_span, value.expression);
    }

    auto visit(const ASTMatchArm& value) noexcept -> void {
        visit_fields(value.span, value.pattern, value.guard, value.arrow_span, value.body);
    }

    auto visit(const ASTMatchForm& value) noexcept -> void {
        visit_fields(value.span, value.keyword_span, value.subject, value.arms);
    }

    auto visit(const ASTCatchWildcardPattern& value) noexcept -> void {
        visit(value.underscore_span);
    }

    auto visit(const ASTCatchTypedPattern& value) noexcept -> void {
        visit_fields(
            value.type,
            value.left_parenthesis_span,
            value.inner,
            value.right_parenthesis_span
        );
    }

    auto visit(const ASTCatchPatternAtom& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTCatchPattern& value) noexcept -> void {
        visit_fields(value.span, value.alternatives, value.pipe_spans);
    }

    auto visit(const ASTCatchArm& value) noexcept -> void {
        visit_fields(value.span, value.pattern, value.guard, value.arrow_span, value.body);
    }

    auto visit(const ASTTryForm& value) noexcept -> void {
        visit_fields(value.span, value.try_span, value.body, value.catch_span, value.arms);
    }

    auto visit(const ASTWildcardPattern& value) noexcept -> void { visit(value.underscore_span); }

    auto visit(const ASTNegativeNumberPattern& value) noexcept -> void {
        visit_fields(value.minus_span, value.number_span);
    }

    auto visit(const ASTBindingPattern& value) noexcept -> void { visit(value.name_span); }

    auto visit(const ASTQualifiedName& value) noexcept -> void {
        visit_fields(value.span, value.components);
    }

    auto visit(const ASTConstraintOperand& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTConstraintPattern& value) noexcept -> void {
        visit_fields(value.is_span, value.operand);
    }

    auto visit(const ASTContextualCaseQualifier& value) noexcept -> void { visit(value.dot_span); }

    auto visit(const ASTQualifiedCaseQualifier& value) noexcept -> void {
        visit_fields(value.span, value.components, value.separator_span);
    }

    auto visit(const ASTCasePayload& value) noexcept -> void {
        visit_fields(value.left_parenthesis_span, value.patterns, value.right_parenthesis_span);
    }

    auto visit(const ASTCasePattern& value) noexcept -> void {
        visit_fields(value.qualifier, value.name_span, value.payload);
    }

    auto visit(const ASTOrPattern& value) noexcept -> void {
        visit_fields(value.alternatives, value.pipe_spans);
    }

    auto visit(const ASTPattern& value) noexcept -> void { visit_fields(value.span, value.value); }

    auto visit(const ASTNamedBindingTarget& value) noexcept -> void { visit(value.name_span); }

    auto visit(const ASTDiscardBindingTarget& value) noexcept -> void {
        visit(value.underscore_span);
    }

    auto visit(const ASTSingleImport& value) noexcept -> void { visit(value.name_span); }

    auto visit(const ASTWildcardImport&) noexcept -> void {}

    auto visit(const ASTImportList& value) noexcept -> void { visit(value.names); }

    auto visit(const ASTImportSelection& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTDomainRootModuleReference& value) noexcept -> void {
        visit(value.components);
    }

    auto visit(const ASTParentRelativeModuleReference& value) noexcept -> void {
        visit_fields(value.prefix_span, value.components);
    }

    auto visit(const ASTCraftQualifiedModuleReference& value) noexcept -> void {
        visit_fields(value.name_span, value.separator_span, value.components);
    }

    auto visit(const ASTModuleReference& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTModuleImport& value) noexcept -> void {
        visit_fields(value.span, value.module_reference, value.selection);
    }

    auto visit(const ASTPrivateDeclarationVisibility& value) noexcept -> void {
        visit(value.keyword_span);
    }

    auto visit(const ASTBareDeclarationVisibility&) noexcept -> void {}

    auto visit(const ASTExportDeclarationVisibility& value) noexcept -> void {
        visit(value.keyword_span);
    }

    auto visit(const ASTEnumCase& value) noexcept -> void {
        visit_fields(value.span, value.name_span, value.payload_types, value.initializer);
    }

    auto visit(const ASTEnumDecl& value) noexcept -> void {
        visit_fields(value.visibility, value.name_span, value.underlying_type, value.cases);
    }

    auto visit(const ASTStructField& value) noexcept -> void {
        visit_fields(value.span, value.name_span, value.type);
    }

    auto visit(const ASTStructDecl& value) noexcept -> void {
        visit_fields(value.visibility, value.name_span, value.fields);
    }

    auto visit(const ASTFunctionParameter& value) noexcept -> void {
        visit_fields(value.span, value.access, value.target, value.type);
    }

    auto visit(const ASTFunctionBody& value) noexcept -> void { visit(value.body); }

    auto visit(const ASTFunctionDecl& value) noexcept -> void {
        visit_fields(
            value.visibility,
            value.cpp_export,
            value.name_span,
            value.parameters,
            value.result_type,
            value.throw_clause,
            value.implementation
        );
    }

    auto visit(const ASTConstantDecl& value) noexcept -> void {
        visit_fields(value.visibility, value.name_span, value.type, value.initializer);
    }

    auto visit(const ASTTestDecl& value) noexcept -> void {
        visit_fields(value.keyword_span, value.name_span, value.body);
    }

    auto visit(const ASTItem& value) noexcept -> void { visit_fields(value.span, value.value); }

    auto visit(const ASTCppNameExpr& value) noexcept -> void {
        visit(value.global_root);
        for (const auto component : value.components) {
            visit(component);
        }
    }

    auto visit(const ASTNameExpr& value) noexcept -> void { visit(value.name_span); }

    auto visit(const ASTContextualCaseExpr& value) noexcept -> void {
        visit_fields(value.dot_span, value.name_span);
    }

    auto visit(const ASTGroupExpr& value) noexcept -> void { visit(value.expression); }

    auto visit(const ASTArrayExpr& value) noexcept -> void { visit(value.element_ids); }

    auto visit(const ASTPositionalInitializerList& value) noexcept -> void {
        visit_fields(value.span, value.values);
    }

    auto visit(const ASTFieldInitializer& value) noexcept -> void {
        visit_fields(value.span, value.name_span, value.value);
    }

    auto visit(const ASTFieldInitializerList& value) noexcept -> void {
        visit_fields(value.span, value.fields);
    }

    auto visit(const ASTConstructionInitializer& value) noexcept -> void { visit(value.value); }

    auto visit(const ASTConstructionExpr& value) noexcept -> void {
        visit_fields(value.type, value.initializer);
    }

    auto visit(const ASTPrefixExpr& value) noexcept -> void {
        visit_fields(value.operator_span, value.operand_id);
    }

    auto visit(const ASTAccessExpr& value) noexcept -> void {
        visit_fields(value.marker_span, value.operand_id);
    }

    auto visit(const ASTBinaryExpr& value) noexcept -> void {
        visit_fields(value.left, value.operator_span, value.right);
    }

    auto visit(const ASTCastExpr& value) noexcept -> void {
        visit_fields(value.operand_id, value.operator_span, value.target_type);
    }

    auto visit(const ASTCallArgument& value) noexcept -> void { visit(value.expression); }

    auto visit(const ASTCallExpr& value) noexcept -> void {
        visit_fields(value.callee, value.arguments);
    }

    auto visit(const ASTIndexExpr& value) noexcept -> void {
        visit_fields(value.operand_id, value.index);
    }

    auto visit(const ASTMemberExpr& value) noexcept -> void {
        visit_fields(value.operand_id, value.operator_span, value.name_span);
    }

    auto visit(const ASTLambdaCapture& value) noexcept -> void {
        visit_fields(value.span, value.write_marker, value.name_span);
    }

    auto visit(const ASTLambdaExpr& value) noexcept -> void {
        visit_fields(
            value.captures,
            value.parameters,
            value.result_type,
            value.throw_clause,
            value.body
        );
    }

    auto visit(const ASTPropagationExpr& value) noexcept -> void {
        visit_fields(value.operand_id, value.operator_span);
    }

    auto visit(const ASTExpr& value) noexcept -> void { visit_fields(value.span, value.value); }

    auto visit(const ASTVariableDecl& value) noexcept -> void {
        visit_fields(value.span, value.keyword_span, value.target, value.type, value.initializer);
    }

    auto visit(const ASTAssignment& value) noexcept -> void {
        visit_fields(value.span, value.target, value.operator_span, value.value);
    }

    auto visit(const ASTUpdate& value) noexcept -> void {
        visit_fields(value.span, value.operator_span, value.target);
    }

    auto visit(const ASTExprStatement& value) noexcept -> void { visit(value.expression); }

    auto visit(const ASTTestOperationStmt& value) noexcept -> void {
        visit_fields(value.keyword_span, value.arguments);
    }

    auto visit(const ASTForInitializer& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTForStep& value) noexcept -> void { visit_fields(value.span, value.value); }

    auto visit(const ASTHalfOpenRange& value) noexcept -> void {
        visit_fields(value.begin, value.operator_span, value.end);
    }

    auto visit(const ASTRangeForHeader& value) noexcept -> void {
        visit_fields(value.write_marker, value.target, value.type, value.iterable);
    }

    auto visit(const ASTCStyleForHeader& value) noexcept -> void {
        visit_fields(value.initializer, value.condition, value.steps);
    }

    auto visit(const ASTForHeader& value) noexcept -> void {
        visit_fields(value.span, value.value);
    }

    auto visit(const ASTWhileStmt& value) noexcept -> void {
        visit_fields(value.keyword_span, value.condition, value.body);
    }

    auto visit(const ASTForStmt& value) noexcept -> void {
        visit_fields(value.keyword_span, value.header, value.body);
    }

    auto visit(const ASTStmt& value) noexcept -> void { visit_fields(value.span, value.value); }

    auto visit(const ASTBlock& value) noexcept -> void {
        visit_fields(value.span, value.statements);
    }

    auto visit(const ASTBranchBlock& value) noexcept -> void {
        visit_fields(value.span, value.statements, value.result);
    }

    Visitor* visitor;
};

template<typename Node, typename Visitor>
auto visit_ast_topology(const Node& node, Visitor visitor) noexcept -> void {
    auto walker = ASTTopologyWalker<Visitor>(visitor);
    walker.run(node);
}
