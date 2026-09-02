module carven:backend.lowering.decl.schedule.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.generation.program;
import :backend.target.item;
import :backend.target.raw;
import :semantic.hir;
import :semantic.hir.decl;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto declaration_ref(HIRNominalDeclRef nominal) noexcept -> HIRDeclarationRef {
    return std::visit([](auto id) static noexcept -> HIRDeclarationRef { return id; }, nominal);
}

auto lower_nominals(
    TargetModuleLowerer& context,
    std::span<const HIRNominalDeclRef> ordered_declarations
) noexcept -> std::vector<TargetItemID> {
    auto result = std::vector<TargetItemID>();
    result.reserve(ordered_declarations.size());
    for (const auto declaration : ordered_declarations) {
        result.push_back(lower_declaration(context, declaration_ref(declaration), false));
    }
    return result;
}

auto append_cpp_source_fragment(
    TargetModuleLowerer& context,
    ProgramOriginID payload_origin
) noexcept -> TargetItemID {
    return context.target().append_item({
        .value =
            TargetRawFragment {
                .bytes = std::string(context.source().provenance().slice(payload_origin)),
            },
        .attribution = {
            .kind = TargetAttributionKind::RawSource,
            .origin = target_source_origin(context, payload_origin),
            .reason = std::nullopt,
        },
    });
}

auto append_test(TargetModuleLowerer& context, TestID test_id) noexcept -> TargetItemID {
    const auto& test = context.source().test(test_id);
    return context.target().append_item({
        .value = lower_test_declaration(context, test_id),
        .attribution = {
            .kind = TargetAttributionKind::SourceOwned,
            .origin = target_source_origin(context, test.origin),
            .reason = std::nullopt,
        },
    });
}

} // namespace

auto lower_declaration_schedule(
    TargetModuleLowerer& context,
    const TargetModuleSchedule& schedule
) noexcept -> LoweredDeclarationSchedule {
    auto result = LoweredDeclarationSchedule {
        .cpp_source_fragments = {},
        .private_implementation = lower_nominals(context, schedule.implementation_nominal_order),
        .module_implementation = {},
        .entry_point = schedule.entry_point,
    };
    const auto& module = context.source().hir_module(schedule.module_id);
    result.cpp_source_fragments.reserve(module.cpp_source_payload_origins.size());
    for (const auto payload_origin : module.cpp_source_payload_origins) {
        result.cpp_source_fragments.push_back(append_cpp_source_fragment(context, payload_origin));
    }
    for (const auto function : schedule.private_function_declarations) {
        result.private_implementation.push_back(
            lower_declaration(context, HIRDeclarationRef {function}, true)
        );
    }
    auto tests = std::vector<TargetItemID>();
    for (const auto function : schedule.function_definitions) {
        auto& destination =
            context.source().function(function).visibility == DeclarationVisibility::Module
            ? result.private_implementation
            : result.module_implementation;
        destination.push_back(lower_declaration(context, HIRDeclarationRef {function}, false));
    }
    tests.reserve(schedule.emitted_tests.size());
    for (const auto test : schedule.emitted_tests) {
        tests.push_back(append_test(context, test));
    }
    if (!tests.empty()) {
        result.module_implementation.push_back(context.target().append_item({
            .value =
                TargetNamespace {
                    .name = std::nullopt,
                    .items = std::move(tests),
                    .body_separation = TargetVerticalSeparation::Line,
                },
            .attribution = {
                .kind = TargetAttributionKind::CompilerOwned,
                .origin = std::nullopt,
                .reason = TargetSyntheticReason::TestHarness,
            },
        }));
    }
    return result;
}
