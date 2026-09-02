module carven:frontend.ast.storage;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.type;
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
    friend class SyntaxTree;
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
