module carven:backend.lower.impl;

import :backend.generation.plan;
import :backend.lower;
import :backend.lowering.context;
import :backend.lowering.decl;
import :backend.target.item;
import :backend.target.name;
import :backend.target.unit;
import :backend.target;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto append_items(std::vector<TargetItem>& destination, std::vector<TargetItem> source) noexcept
    -> void {
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end())
    );
}

auto wrap_linkage_namespaces(
    const ArtifactLowering& context,
    std::vector<TargetItem> items
) noexcept -> std::vector<TargetItem> {
    if (items.empty()) {
        return {};
    }
    auto domain = namespace_item(
        context.plan().names().domain_namespace(),
        std::move(items),
        TargetCompilerReason::ArtifactScaffolding,
        false
    );
    auto generated = namespace_item(
        context.plan().names().generated_namespace(),
        target_items(std::move(domain))
    );
    return target_items(std::move(generated));
}

auto lower_interface(ArtifactLowering& context, const TargetInterfaceArtifact& schedule) noexcept
    -> TargetUnitSections {
    auto root = std::vector<TargetItem>();
    auto active = std::optional<ModuleLowering>();
    auto module_items = std::vector<TargetItem>();
    const auto flush = [&]() noexcept {
        if (!active.has_value()) {
            return;
        }
        root.push_back(namespace_item(
            context.plan().names().module_names(active->active_module()).module_namespace_name,
            std::move(module_items),
            TargetCompilerReason::ArtifactScaffolding,
            false
        ));
        module_items.clear();
    };
    for (const auto& planned : schedule.forward_declarations) {
        if (!active.has_value() || active->active_module() != planned.module_id) {
            flush();
            active.emplace(context, planned.module_id);
        }
        auto& module_context = *active;
        module_items.push_back(lower_forward_declaration(module_context, planned.declaration));
    }
    flush();

    active.reset();
    for (const auto& planned : schedule.declarations) {
        if (!active.has_value() || active->active_module() != planned.module_id) {
            flush();
            active.emplace(context, planned.module_id);
        }
        auto& module_context = *active;
        std::visit(
            [&](auto id) noexcept {
                if constexpr (std::same_as<decltype(id), CallableID>) {
                    module_items.push_back(lower_closure_type(module_context, id));
                } else {
                    append_items(
                        module_items,
                        lower_declaration(
                            module_context,
                            DeclarationRef {id},
                            std::same_as<decltype(id), FunctionID>
                        )
                    );
                }
            },
            planned.declaration
        );
    }
    flush();
    return {
        .preamble = {},
        .body = wrap_linkage_namespaces(context, std::move(root)),
        .epilogue = {},
    };
}

auto lower_cpp_api_header(
    ArtifactLowering& context,
    const TargetCppAPIHeaderArtifact& schedule
) noexcept -> TargetUnitSections {
    auto module_context = context.module_context(schedule.module_id);
    auto declarations = std::vector<TargetItem>();
    for (const auto function : schedule.cpp_export_declarations) {
        declarations.push_back(lower_cpp_export_header_declaration(module_context, function));
    }
    return {
        .preamble = {},
        .body = target_items(namespace_item(
            context.plan().names().module_names(schedule.module_id).public_namespace_name,
            std::move(declarations)
        )),
        .epilogue = {},
    };
}

auto lower_module(
    ArtifactLowering& context,
    const TargetModuleImplementationArtifact& artifact
) noexcept -> TargetUnitSections {
    const auto& schedule = artifact.schedule;
    auto module_context = context.module_context(schedule.module_id);
    auto lowered = lower_module_schedule(module_context, schedule);
    auto module_items = std::vector<TargetItem>();
    context.require_cpp_environment(schedule.module_id, CppEnvironmentRequirement::Using);
    if (!lowered.private_items.empty()) {
        module_items.push_back(namespace_item(std::nullopt, std::move(lowered.private_items)));
    }
    append_items(module_items, std::move(lowered.module_items));
    auto root = std::vector<TargetItem>();
    if (!module_items.empty()) {
        root.push_back(namespace_item(
            context.plan().names().module_names(schedule.module_id).module_namespace_name,
            std::move(module_items),
            TargetCompilerReason::ArtifactScaffolding,
            false
        ));
    }
    auto epilogue = std::vector<TargetItem>();
    if (lowered.entry_wrapper.has_value()) {
        epilogue.push_back(std::move(*lowered.entry_wrapper));
    }
    if (!lowered.cpp_export_facades.empty()) {
        epilogue.push_back(namespace_item(
            context.plan().names().module_names(schedule.module_id).public_namespace_name,
            std::move(lowered.cpp_export_facades)
        ));
    }
    return {
        .preamble = std::move(lowered.source_fragments),
        .body = wrap_linkage_namespaces(context, std::move(root)),
        .epilogue = std::move(epilogue),
    };
}

} // namespace

auto lower_artifact(const PlannedCompilation& compilation, TargetArtifactID artifact_id) noexcept
    -> TargetUnit {
    auto context = ArtifactLowering(compilation, artifact_id);
    auto sections = std::visit(
        Overloaded {
            [&](const TargetInterfaceArtifact& artifact) noexcept {
                return lower_interface(context, artifact);
            },
            [&](const TargetCppAPIHeaderArtifact& artifact) noexcept {
                return lower_cpp_api_header(context, artifact);
            },
            [&](const TargetModuleImplementationArtifact& artifact) noexcept {
                return lower_module(context, artifact);
            },
            [&](const TargetTestRunnerHeaderArtifact& artifact) noexcept {
                return lower_test_runner_header(context, artifact);
            },
            [&](const TargetTestEntryArtifact&) noexcept { return lower_test_entry(context); },
        },
        context.artifact()
    );
    return std::move(context).finish(std::move(sections));
}
