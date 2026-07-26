module carven:backend.lowering.declarations.schedule.impl;

import :backend.lowering.program;
import :backend.lowering.declarations;
import :backend.target.item;
import :backend.target.raw;
import :semantic.hir;
import :semantic.hir.decl;
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
                .bytes = std::string(context.semantic().provenance().spelling(region.bytes)),
            },
        .attribution = {
            .kind = TargetAttributionKind::RawSource,
            .origin = target_source_origin(context, region.origin),
            .reason = std::nullopt,
        },
    });
}

auto append_test(TargetModuleLowerer& context, TestID test_id) noexcept -> TargetItemID {
    const auto& test = context.semantic().test(test_id);
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
    std::span<const HIRModuleItem> items,
    std::span<const HIRDeclarationRef> surface_declarations,
    std::span<const HIRNominalDeclRef> implementation_nominal_order
) noexcept -> LoweredDeclarationSchedule {
    const auto surface_set =
        std::flat_set<HIRDeclarationRef>(surface_declarations.begin(), surface_declarations.end());
    auto result = LoweredDeclarationSchedule {
        .cpp_preamble = {},
        .implementation = lower_nominals(context, implementation_nominal_order),
        .entry_point = std::nullopt,
    };
    auto private_functions = std::vector<FunctionID>();
    for (const auto& item : items) {
        if (const auto* cpp = std::get_if<HIRCppRegion>(&item)) {
            result.cpp_preamble.push_back(append_cpp(context, *cpp));
            continue;
        }
        const auto* function_item = std::get_if<FunctionID>(&item);
        if (function_item == nullptr) {
            continue;
        }
        const auto function_id = *function_item;
        const auto& function = context.semantic().function(function_id);
        if (function.entry_point.has_value()) {
            result.entry_point = function_id;
        }
        if (!surface_set.contains(HIRDeclarationRef {function_id})) {
            private_functions.push_back(function_id);
        }
    }
    for (const auto function : private_functions) {
        result.implementation.push_back(
            lower_declaration(context, HIRDeclarationRef {function}, true)
        );
    }
    auto tests = std::vector<TargetItemID>();
    for (const auto& item : items) {
        if (const auto* function = std::get_if<FunctionID>(&item)) {
            result.implementation.push_back(
                lower_declaration(context, HIRDeclarationRef {*function}, false)
            );
        } else if (const auto* test = std::get_if<TestID>(&item);
                   test != nullptr && context.emits_tests()) {
            tests.push_back(append_test(context, *test));
        }
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
