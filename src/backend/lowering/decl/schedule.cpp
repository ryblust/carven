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

auto append_cpp(TargetModuleLowerer& context, const HIRCppRegion& region) noexcept -> TargetItemID {
    return context.target().append_item({
        .value =
            TargetRawFragment {
                .bytes = std::string(context.source().provenance().spelling(region.bytes)),
            },
        .attribution = {
            .kind = TargetAttributionKind::RawSource,
            .origin = target_source_origin(context, region.origin),
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
        .cpp_preamble = {},
        .implementation = lower_nominals(context, schedule.implementation_nominal_order),
        .entry_point = schedule.entry_point,
    };
    const auto& module_items = context.source().hir_module(schedule.module_id).items;
    result.cpp_preamble.reserve(schedule.cpp_preamble_items.size());
    for (const auto item_index : schedule.cpp_preamble_items) {
        if (item_index >= module_items.size()) {
            invariant_violation("target module schedule references an unknown source item");
        }
        const auto* cpp = std::get_if<HIRCppRegion>(&module_items[item_index]);
        if (cpp == nullptr) {
            invariant_violation("target module preamble schedule references a non-C++ item");
        }
        result.cpp_preamble.push_back(append_cpp(context, *cpp));
    }
    for (const auto function : schedule.private_function_declarations) {
        result.implementation.push_back(
            lower_declaration(context, HIRDeclarationRef {function}, true)
        );
    }
    auto tests = std::vector<TargetItemID>();
    for (const auto function : schedule.function_definitions) {
        result.implementation.push_back(
            lower_declaration(context, HIRDeclarationRef {function}, false)
        );
    }
    tests.reserve(schedule.emitted_tests.size());
    for (const auto test : schedule.emitted_tests) {
        tests.push_back(append_test(context, test));
    }
    if (!tests.empty()) {
        result.implementation.push_back(context.target().append_item({
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
