module carven:frontend.program.verify.impl;

import :frontend.ast.ids;
import :frontend.ast.storage;
import :frontend.program.verify;
import :frontend.program;
import :source.provenance.ids;
import :source.provenance.verify;
import :source.text;
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

class SyntaxTreeOwnershipVerifier final {
public:
    SyntaxTreeOwnershipVerifier(ASTView syntax_value, std::string_view module_path_value) noexcept;
    auto verify() noexcept -> std::expected<void, SyntaxProgramError>;

private:
    auto fail(SyntaxProgramErrorKind kind, std::string message) noexcept -> void;

    template<typename ID, typename Node>
    auto claim_node(
        ID id,
        std::vector<std::uint8_t>& claims,
        std::span<const Node> nodes,
        std::string_view kind
    ) noexcept -> void {
        if (failure.has_value()) {
            return;
        }
        if (static_cast<std::size_t>(id.index()) >= nodes.size()
            || static_cast<std::size_t>(id.index()) >= claims.size()) {
            fail(
                SyntaxProgramErrorKind::InvalidReference,
                std::format(
                    "{} @{} is outside the syntax tree for module '{}'",
                    kind,
                    id.index(),
                    module_path
                )
            );
            return;
        }
        auto& claimed = claims[id.index()];
        if (claimed != 0) {
            fail(
                SyntaxProgramErrorKind::InvalidRootOwnership,
                std::format(
                    "{} @{} has more than one structural owner in syntax tree for module '{}'",
                    kind,
                    id.index(),
                    module_path
                )
            );
            return;
        }
        claimed = 1;
        visit_ast_topology(nodes[id.index()], [&](auto field) noexcept { claim(field); });
    }

    auto all_claimed(std::span<const std::uint8_t> claims, std::string_view kind) noexcept -> bool;
    auto claim(Span) noexcept -> void;
    auto claim(ASTExprID id) noexcept -> void;
    auto claim(ASTTypeID id) noexcept -> void;
    auto claim(ASTStmtID id) noexcept -> void;
    auto claim(ASTPatternID id) noexcept -> void;
    auto claim(ASTBlockID id) noexcept -> void;
    auto claim(ASTBranchBlockID id) noexcept -> void;
    auto claim(ASTItemID id) noexcept -> void;
    auto claim(ASTModuleImportID id) noexcept -> void;

    ASTView syntax;
    std::string_view module_path;
    std::vector<std::uint8_t> claimed_expressions;
    std::vector<std::uint8_t> claimed_types;
    std::vector<std::uint8_t> claimed_statements;
    std::vector<std::uint8_t> claimed_patterns;
    std::vector<std::uint8_t> claimed_blocks;
    std::vector<std::uint8_t> claimed_branch_blocks;
    std::vector<std::uint8_t> claimed_items;
    std::vector<std::uint8_t> claimed_module_imports;
    std::optional<SyntaxProgramError> failure;
};

SyntaxTreeOwnershipVerifier::SyntaxTreeOwnershipVerifier(
    ASTView syntax_value,
    std::string_view module_path_value
) noexcept
    : syntax(syntax_value),
      module_path(module_path_value),
      claimed_expressions(syntax.expressions().size()),
      claimed_types(syntax.types().size()),
      claimed_statements(syntax.statements().size()),
      claimed_patterns(syntax.patterns().size()),
      claimed_blocks(syntax.blocks().size()),
      claimed_branch_blocks(syntax.branch_blocks().size()),
      claimed_items(syntax.items().size()),
      claimed_module_imports(syntax.module_imports().size()) {}

auto SyntaxTreeOwnershipVerifier::verify() noexcept -> std::expected<void, SyntaxProgramError> {
    for (const auto import_id : syntax.ast_module().module_imports) {
        claim(import_id);
    }
    for (const auto item_id : syntax.ast_module().items) {
        claim(item_id);
    }
    if (failure.has_value()) {
        return std::unexpected(std::move(*failure));
    }
    if (!all_claimed(claimed_expressions, "expression")
        || !all_claimed(claimed_types, "type")
        || !all_claimed(claimed_statements, "statement")
        || !all_claimed(claimed_patterns, "pattern")
        || !all_claimed(claimed_blocks, "block")
        || !all_claimed(claimed_branch_blocks, "branch block")
        || !all_claimed(claimed_items, "item")
        || !all_claimed(claimed_module_imports, "module import")) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

auto SyntaxTreeOwnershipVerifier::fail(SyntaxProgramErrorKind kind, std::string message) noexcept
    -> void {
    if (!failure.has_value()) {
        failure = SyntaxProgramError {.kind = kind, .message = std::move(message)};
    }
}

auto SyntaxTreeOwnershipVerifier::all_claimed(
    std::span<const std::uint8_t> claims,
    std::string_view kind
) noexcept -> bool {
    if (std::ranges::all_of(claims, [](std::uint8_t claimed) static noexcept {
            return claimed != 0;
        })) {
        return true;
    }
    fail(
        SyntaxProgramErrorKind::InvalidRootOwnership,
        std::format("syntax tree for module '{}' contains an unowned {}", module_path, kind)
    );
    return false;
}

auto SyntaxTreeOwnershipVerifier::claim(Span) noexcept -> void {}

auto SyntaxTreeOwnershipVerifier::claim(ASTExprID id) noexcept -> void {
    claim_node(id, claimed_expressions, syntax.expressions(), "expression");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTTypeID id) noexcept -> void {
    claim_node(id, claimed_types, syntax.types(), "type");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTStmtID id) noexcept -> void {
    claim_node(id, claimed_statements, syntax.statements(), "statement");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTPatternID id) noexcept -> void {
    claim_node(id, claimed_patterns, syntax.patterns(), "pattern");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTBlockID id) noexcept -> void {
    claim_node(id, claimed_blocks, syntax.blocks(), "block");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTBranchBlockID id) noexcept -> void {
    claim_node(id, claimed_branch_blocks, syntax.branch_blocks(), "branch block");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTItemID id) noexcept -> void {
    claim_node(id, claimed_items, syntax.items(), "item");
}

auto SyntaxTreeOwnershipVerifier::claim(ASTModuleImportID id) noexcept -> void {
    claim_node(id, claimed_module_imports, syntax.module_imports(), "module import");
}

auto verify_resolved_imports(
    const SyntaxProgram& program,
    ProgramModuleID module_id,
    std::size_t module_count,
    std::string_view module_path
) noexcept -> std::expected<void, SyntaxProgramError> {
    const auto syntax = program.syntax_tree(module_id).view();
    const auto& declarations = syntax.ast_module().module_imports;
    const auto resolved = program.resolved_imports(module_id);
    if (resolved.size() != declarations.size()) {
        return verification_error(
            SyntaxProgramErrorKind::InvalidResolvedImportGraph,
            std::format(
                "module '{}' has {} import declarations but {} resolved import edges",
                module_path,
                declarations.size(),
                resolved.size()
            )
        );
    }
    for (auto index = 0uz; index < resolved.size(); ++index) {
        const auto& edge = resolved[index];
        if (edge.declaration != declarations[index]) {
            return verification_error(
                SyntaxProgramErrorKind::InvalidResolvedImportGraph,
                std::format(
                    "resolved imports for module '{}' do not preserve declaration order",
                    module_path
                )
            );
        }
        if (!program.provenance().contains(edge.target)
            || edge.target.index() >= module_count
            || edge.target == module_id) {
            return verification_error(
                SyntaxProgramErrorKind::InvalidResolvedImportGraph,
                std::format(
                    "resolved import @{} for module '{}' has an invalid target",
                    edge.declaration.index(),
                    module_path
                )
            );
        }
    }
    return {};
}

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
    const auto import_graph = program.resolved_import_graph();
    if (import_graph.size() != module_count) {
        return verification_error(
            SyntaxProgramErrorKind::InvalidResolvedImportGraph,
            std::format(
                "syntax program contains {} modules but {} resolved-import rows",
                module_count,
                import_graph.size()
            )
        );
    }

    for (auto index = 0uz; index < module_count; ++index) {
        const auto module_id = provenance.module_id_at(index);
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
        const auto ownership =
            SyntaxTreeOwnershipVerifier(syntax, module_record.path.value()).verify();
        if (!ownership.has_value()) {
            return ownership;
        }
        const auto imports =
            verify_resolved_imports(program, module_id, module_count, module_record.path.value());
        if (!imports.has_value()) {
            return imports;
        }
    }
    return {};
}

} // namespace

auto verify_syntax_program(const SyntaxProgram& program) noexcept
    -> std::expected<void, SyntaxProgramError> {
    return verify_program(program);
}
