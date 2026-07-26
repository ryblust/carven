module carven:semantic.analysis.elaboration.declarations;

import :semantic.analysis.elaboration.module_analysis;
import :semantic.hir.ids;
import std;

auto elaborate_declaration_contract(
    ModuleAnalysis& source,
    DeclarationDefinitionSink& definitions,
    SymbolID symbol,
    ASTItemID item
) noexcept -> bool;

auto elaborate_module_constant(
    ModuleAnalysis& source,
    SymbolID symbol,
    const ASTConstantDecl& declaration
) noexcept -> bool;

auto elaborate_enum_case(ModuleAnalysis& source, SymbolID owner, std::size_t index) noexcept
    -> bool;

auto validate_enum_codes(ModuleAnalysis& source, SymbolID owner) noexcept -> void;

auto build_module(ModuleAnalysis& source) noexcept -> void;
