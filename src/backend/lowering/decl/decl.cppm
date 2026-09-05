module carven:backend.lowering.decl;

import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.target.item;
import :backend.target.unit;
import :semantic.semir;
import std;

struct LoweredModuleSchedule final {
    std::vector<TargetItem> source_fragments;
    std::vector<TargetItem> private_items;
    std::vector<TargetItem> module_items;
    std::optional<TargetItem> entry_wrapper;
    std::vector<TargetItem> cpp_export_facades;
};

auto lower_declaration(
    ModuleLowering& context,
    DeclarationRef declaration,
    bool declaration_only
) noexcept -> std::vector<TargetItem>;

auto lower_forward_declaration(ModuleLowering& context, NominalDeclarationRef declaration) noexcept
    -> TargetItem;

auto lower_cpp_export_header_declaration(ModuleLowering& context, FunctionID function) noexcept
    -> TargetItem;

auto lower_cpp_export_facade(ModuleLowering& context, FunctionID function) noexcept -> TargetItem;

auto lower_module_schedule(ModuleLowering& context, const TargetModuleSchedule& schedule) noexcept
    -> LoweredModuleSchedule;

auto lower_entry_wrapper(ModuleLowering& context, FunctionID function) noexcept -> TargetItem;

auto lower_test_runner_header(
    ArtifactLowering& context,
    const TargetTestRunnerHeaderArtifact& artifact
) noexcept -> TargetUnitSections;

auto lower_test_entry(ArtifactLowering& context) noexcept -> TargetUnitSections;
