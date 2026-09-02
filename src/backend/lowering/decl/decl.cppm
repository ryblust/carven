module carven:backend.lowering.decl;

import :backend.lowering.program;
import :backend.generation.program;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :semantic.hir.decl;
import std;

struct LoweredDeclarationSchedule final {
    std::vector<TargetItemID> cpp_source_fragments;
    std::vector<TargetItemID> private_implementation;
    std::vector<TargetItemID> module_implementation;
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
    const TargetModuleSchedule& schedule
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

auto lower_cpp_export_header_declaration(TargetModuleLowerer& context, FunctionID function) noexcept
    -> TargetItemID;

auto lower_cpp_export_facade(TargetModuleLowerer& context, FunctionID function) noexcept
    -> TargetItemID;

auto lower_structure_declaration(TargetModuleLowerer& context, StructID structure) noexcept
    -> TargetItemValue;

auto lower_enumeration_declaration(TargetModuleLowerer& context, EnumID enumeration) noexcept
    -> TargetItemValue;

auto lower_test_declaration(TargetModuleLowerer& context, TestID test) noexcept -> TargetItemValue;
