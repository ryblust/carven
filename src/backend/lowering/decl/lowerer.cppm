module carven:backend.lowering.decl.lowerer;

import :backend.lowering.context;
import :backend.target.decl;
import :backend.target.item;
import :backend.target.stmt;
import :semantic.semir;
import std;

namespace decl_lowering {

auto is_char_type(const SemIRProgram& semantic, TypeID type) noexcept -> bool;

auto lower_function(ModuleLowering& context, FunctionID function, bool declaration_only) noexcept
    -> TargetDecl;

auto lower_closure_definition(ModuleLowering& context, CallableID callable) noexcept -> TargetItem;

auto lower_structure(ModuleLowering& context, StructID structure) noexcept -> TargetDecl;

auto lower_enumeration(ModuleLowering& context, EnumID enumeration) noexcept
    -> std::vector<TargetItem>;

auto lower_test(ModuleLowering& context, TestID test) noexcept -> TargetItem;

auto lower_module_test_runner(ModuleLowering& context, std::span<const TestID> tests) noexcept
    -> TargetItem;

auto first_module(const SemIRProgram& semantic) noexcept -> ModuleID;

auto process_entry(
    ModuleLowering& context,
    bool accepts_arguments,
    std::vector<TargetStmt> body
) noexcept -> TargetItem;

} // namespace decl_lowering
