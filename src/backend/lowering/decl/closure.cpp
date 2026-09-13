module carven:backend.lowering.decl.closure.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.lowering.decl.lowerer;
import :backend.lowering.decl;
import :backend.realization.body;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

namespace {

constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

auto closure_capture_identifier(std::size_t index) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(std::format("carven_capture_{}", index));
}

auto parameter_identifier(
    const ModuleLowering& context,
    TargetNameAllocator& names,
    const SemIRBody& body,
    LocalBindingID binding
) noexcept -> TargetIdentifier {
    const auto& row = body.binding(binding);
    return names.local_symbol(
        context.semantic().provenance().spelling(row.name),
        binding.index(),
        callable_scope
    );
}

} // namespace

auto lower_closure_type(ModuleLowering& context, CallableID callable_id) noexcept -> TargetItem {
    const auto& callable = context.semantic().declarations().callable(callable_id);
    const auto* implementation = std::get_if<ClosureBodyImplementation>(&callable.implementation);
    if (implementation == nullptr) {
        invariant_violation("closure type lowering received a non-closure callable");
    }
    const auto& body = context.semantic().bodies().body(implementation->body);
    const auto& signature = context.semantic().callable_signatures().signature(callable.signature);
    auto members = std::vector<TargetRecordMember>();
    for (auto index = 0uz; index < body.inputs().captures.size(); ++index) {
        const auto& binding = body.binding(body.inputs().captures[index]);
        const auto* storage = std::get_if<CaptureBindingStorage>(&binding.storage);
        if (storage == nullptr) {
            invariant_violation("closure input does not name a capture");
        }
        auto type = context.lower_type(binding.type);
        if (storage->mode == CaptureMode::Write) {
            type = context.target().intern_type({
                .value =
                    TargetIntrinsicType {
                        .symbol = TargetSymbol::StdReferenceWrapper,
                        .type_argument_ids = {type},
                    },
                .const_qualified = false,
            });
        }
        members.push_back(
            TargetStructField {.name = closure_capture_identifier(index), .type = type}
        );
    }
    auto parameters = std::vector<TargetParameter>();
    for (const auto& parameter : signature.parameters) {
        parameters.push_back({.name = std::nullopt, .type = context.lower_parameter(parameter)});
    }
    members.push_back(
        TargetMemberFunctionDecl {
            .name = TargetOperatorName::Call,
            .parameters = std::move(parameters),
            .result = context.callable_result(callable_id),
            .form = TargetMemberFunctionDeclaration {},
            .maybe_unused = false,
            .static_specifier = false,
            .constexpr_specifier = false,
            .friend_specifier = false,
            .result_reference = false,
            .const_qualified = true,
        }
    );
    return source_item(
        context.semantic(),
        body.region().origin,
        TargetDecl {TargetStructDecl {
            .name = context.closure_type_name(callable_id).components().back(),
            .members = std::move(members),
        }}
    );
}

auto lower_closure_body(ModuleLowering& context, CallableID callable_id) noexcept -> TargetItem {
    const auto& callable = context.semantic().declarations().callable(callable_id);
    const auto* implementation = std::get_if<ClosureBodyImplementation>(&callable.implementation);
    if (implementation == nullptr) {
        invariant_violation("closure body lowering received a non-closure callable");
    }
    const auto& body = context.semantic().bodies().body(implementation->body);
    const auto& signature = context.semantic().callable_signatures().signature(callable.signature);
    if (body.inputs().parameters.size() != signature.parameters.size()) {
        invariant_violation("closure body inputs do not match its target signature");
    }
    auto inputs = BodyRealizationInputs {
        .parameters = {},
        .captures = {},
        .exit = CallableBodyExit {.callable_id = callable_id},
    };
    auto names = context.make_callable_name_allocator();
    for (auto index = 0uz; index < body.inputs().captures.size(); ++index) {
        const auto name = closure_capture_identifier(index);
        names.reserve(name.spelling());
        names.reserve(name.spelling(), callable_scope);
        inputs.captures.push_back(name);
    }
    auto parameters = std::vector<TargetParameter>();
    for (auto index = 0uz; index < signature.parameters.size(); ++index) {
        const auto name =
            parameter_identifier(context, names, body, body.inputs().parameters[index]);
        inputs.parameters.push_back(name);
        parameters.push_back(
            {.name = name, .type = context.lower_parameter(signature.parameters[index])}
        );
    }
    auto lowered = lower_body(context, implementation->body, std::move(inputs));
    if (lowered.referenced_parameters.size() != parameters.size()) {
        invariant_violation("lowered closure parameter reference facts are incomplete");
    }
    for (auto index = 0uz; index < parameters.size(); ++index) {
        if (!lowered.referenced_parameters[index]) {
            parameters[index].name.reset();
        }
    }
    return source_item(
        context.semantic(),
        body.region().origin,
        TargetDecl {TargetOutOfClassMemberDefinition {
            .owner = context.closure_type_name(callable_id),
            .name = TargetOperatorName::Call,
            .parameters = std::move(parameters),
            .result = context.callable_result(callable_id),
            .body = std::move(lowered.statements),
            .const_qualified = true,
        }}
    );
}
