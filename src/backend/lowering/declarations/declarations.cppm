module carven:backend.lowering.declarations;

import :backend.lowering.program;
import :backend.generation.plan;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :semantic.hir.decl;
import std;

struct LoweredDeclarationSchedule final {
    std::vector<TargetItemID> cpp_preamble;
    std::vector<TargetItemID> implementation;
    std::optional<FunctionID> entry_point;
};

auto process_entry_arguments(TargetModuleLowerer& context) noexcept -> std::vector<TargetExprID>;
auto lower_process_entry(
    TargetModuleLowerer& context,
    bool accepts_arguments,
    std::vector<TargetStmtID> body
) noexcept -> TargetItemID;
auto lower_declaration(
    TargetModuleLowerer& context,
    HIRDeclarationRef declaration,
    bool declaration_only
) noexcept -> TargetItemID;
auto lower_declaration_schedule(
    TargetModuleLowerer& context,
    std::span<const HIRModuleItem> items,
    std::span<const HIRDeclarationRef> surface_declarations,
    std::span<const HIRNominalDeclRef> implementation_nominal_order
) noexcept -> LoweredDeclarationSchedule;
auto lower_entry_wrapper(
    TargetModuleLowerer& context,
    const HIRFunctionDecl& function,
    const TargetName& namespace_name
) noexcept -> TargetItemID;

auto lower_function_declaration(
    TargetModuleLowerer& context,
    FunctionID function,
    bool declaration_only
) noexcept -> TargetItemValue;
auto lower_structure_declaration(TargetModuleLowerer& context, StructID structure) noexcept
    -> TargetItemValue;
auto lower_enumeration_declaration(TargetModuleLowerer& context, EnumID enumeration) noexcept
    -> TargetItemValue;
auto lower_test_declaration(TargetModuleLowerer& context, TestID test) noexcept -> TargetItemValue;
