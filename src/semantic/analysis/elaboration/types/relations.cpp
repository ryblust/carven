module carven:semantic.analysis.elaboration.types.relations.impl;

import :semantic.analysis.call_contract;
import :semantic.analysis.elaboration.types.relations;
import :semantic.analysis.session.read;
import :semantic.hir;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {

auto valid_type(SemanticDraftView hir, HIRTypeID id) noexcept -> bool {
    return id.index() < hir.types().size();
}

auto shapes_compatible(SemanticDraftView hir, HIRTypeID left, HIRTypeID right) noexcept -> bool {
    if (left == right) {
        return true;
    }
    if (!valid_type(hir, left) || !valid_type(hir, right)) {
        return false;
    }
    const auto& left_value = hir.type(left).value;
    const auto& right_value = hir.type(right).value;

    const auto* left_builtin = std::get_if<HIRBuiltinTypeValue>(&left_value);
    const auto* right_builtin = std::get_if<HIRBuiltinTypeValue>(&right_value);
    if (left_builtin != nullptr || right_builtin != nullptr) {
        return left_builtin != nullptr
            && right_builtin != nullptr
            && left_builtin->kind == right_builtin->kind;
    }
    const auto* left_structure = std::get_if<HIRStructTypeValue>(&left_value);
    const auto* right_structure = std::get_if<HIRStructTypeValue>(&right_value);
    if (left_structure != nullptr || right_structure != nullptr) {
        return left_structure != nullptr
            && right_structure != nullptr
            && left_structure->structure == right_structure->structure;
    }
    const auto* left_enumeration = std::get_if<HIREnumTypeValue>(&left_value);
    const auto* right_enumeration = std::get_if<HIREnumTypeValue>(&right_value);
    if (left_enumeration != nullptr || right_enumeration != nullptr) {
        return left_enumeration != nullptr
            && right_enumeration != nullptr
            && left_enumeration->enumeration == right_enumeration->enumeration;
    }
    const auto* left_array = std::get_if<HIRArrayTypeValue>(&left_value);
    const auto* right_array = std::get_if<HIRArrayTypeValue>(&right_value);
    if (left_array != nullptr || right_array != nullptr) {
        return left_array != nullptr
            && right_array != nullptr
            && left_array->extent == right_array->extent
            && shapes_compatible(hir, left_array->element_type_id, right_array->element_type_id);
    }

    const auto* left_function = std::get_if<HIRFunctionTypeValue>(&left_value);
    const auto* right_function = std::get_if<HIRFunctionTypeValue>(&right_value);
    const auto* left_ref = std::get_if<HIRFunctionRefTypeValue>(&left_value);
    const auto* right_ref = std::get_if<HIRFunctionRefTypeValue>(&right_value);
    const auto* left_closure = std::get_if<HIRClosureTypeValue>(&left_value);
    const auto* right_closure = std::get_if<HIRClosureTypeValue>(&right_value);
    if (left_closure != nullptr
        && right_closure != nullptr
        && left_closure->callable != right_closure->callable) {
        return false;
    }
    const auto callable_shape = [&](CallContractView left_callable,
                                    CallContractView right_callable) noexcept {
        if (left_callable.parameters.size() != right_callable.parameters.size()
            || !shapes_compatible(hir, left_callable.result, right_callable.result)) {
            return false;
        }
        return std::ranges::equal(
            left_callable.parameters,
            right_callable.parameters,
            [&](const HIRFunctionParameterType& left_parameter,
                const HIRFunctionParameterType& right_parameter) noexcept {
                return left_parameter.access == right_parameter.access
                    && shapes_compatible(hir, left_parameter.type, right_parameter.type);
            }
        );
    };
    const auto left_callable =
        left_function != nullptr || left_ref != nullptr || left_closure != nullptr;
    const auto right_callable =
        right_function != nullptr || right_ref != nullptr || right_closure != nullptr;
    if (left_callable || right_callable) {
        const auto left_contract = callable_contract(hir, left);
        const auto right_contract = callable_contract(hir, right);
        return left_contract.has_value()
            && right_contract.has_value()
            && callable_shape(*left_contract, *right_contract);
    }
    return left_value.index() == right_value.index();
}

auto failures_are_subset(
    std::span<const HIRTypeID> subset,
    std::span<const HIRTypeID> superset
) noexcept -> bool {
    return std::ranges::all_of(subset, [&](HIRTypeID failure) noexcept {
        return std::ranges::contains(superset, failure);
    });
}

auto adoption_compatible(
    SemanticDraftView hir,
    HIRTypeID target,
    HIRTypeID source,
    std::span<const std::vector<HIRTypeID>> callable_failures
) noexcept -> bool {
    if (target == source) {
        return true;
    }
    if (!shapes_compatible(hir, target, source)) {
        return false;
    }
    const auto& target_value = hir.type(target).value;
    const auto& source_value = hir.type(source).value;

    const auto* target_array = std::get_if<HIRArrayTypeValue>(&target_value);
    const auto* source_array = std::get_if<HIRArrayTypeValue>(&source_value);
    if (target_array != nullptr && source_array != nullptr) {
        return adoption_compatible(
            hir,
            target_array->element_type_id,
            source_array->element_type_id,
            callable_failures
        );
    }

    const auto* target_function = std::get_if<HIRFunctionTypeValue>(&target_value);
    const auto* source_function = std::get_if<HIRFunctionTypeValue>(&source_value);
    const auto* target_ref = std::get_if<HIRFunctionRefTypeValue>(&target_value);
    const auto* source_ref = std::get_if<HIRFunctionRefTypeValue>(&source_value);
    const auto* target_closure = std::get_if<HIRClosureTypeValue>(&target_value);
    const auto* source_closure = std::get_if<HIRClosureTypeValue>(&source_value);
    const auto callable_adoption = [&](CallContractView target_callable,
                                       CallContractView source_callable) noexcept {
        const auto failures = [&](
                                  const CallContractView& callable
                              ) noexcept -> std::optional<std::span<const HIRTypeID>> {
            return std::visit(
                Overloaded {
                    [&](const ConcreteCallableFailure& source) noexcept
                        -> std::optional<std::span<const HIRTypeID>> {
                        if (source.callable.index() >= callable_failures.size()) {
                            return std::nullopt;
                        }
                        return callable_failures[source.callable.index()];
                    },
                    [&](const FixedSignatureFailure& source) noexcept
                        -> std::optional<std::span<const HIRTypeID>> {
                        if (source.failure_set.index() >= hir.failure_sets().size()) {
                            return std::nullopt;
                        }
                        return hir.failure_set(source.failure_set).members;
                    },
                    [](const ForeignCallableFailure&) static noexcept
                        -> std::optional<std::span<const HIRTypeID>> { return std::nullopt; },
                },
                callable.failure_source
            );
        };
        const auto target_failures = failures(target_callable);
        const auto source_failures = failures(source_callable);
        if (!target_failures.has_value() || !source_failures.has_value()) {
            return false;
        }
        if (!failures_are_subset(*source_failures, *target_failures)
            || !adoption_compatible(
                hir,
                target_callable.result,
                source_callable.result,
                callable_failures
            )) {
            return false;
        }
        for (auto index = 0uz; index < target_callable.parameters.size(); ++index) {
            if (!adoption_compatible(
                    hir,
                    source_callable.parameters[index].type,
                    target_callable.parameters[index].type,
                    callable_failures
                )) {
                return false;
            }
        }
        return true;
    };
    const auto target_callable =
        target_function != nullptr || target_ref != nullptr || target_closure != nullptr;
    const auto source_callable =
        source_function != nullptr || source_ref != nullptr || source_closure != nullptr;
    if (target_callable || source_callable) {
        const auto target_contract = callable_contract(hir, target);
        const auto source_contract = callable_contract(hir, source);
        return target_contract.has_value()
            && source_contract.has_value()
            && callable_adoption(*target_contract, *source_contract);
    }
    return true;
}

} // namespace

auto builtin_type_supports_equality(HIRBuiltinType type) noexcept -> bool {
    switch (type) {
        case HIRBuiltinType::Bool:
        case HIRBuiltinType::Char:
        case HIRBuiltinType::I8:
        case HIRBuiltinType::I16:
        case HIRBuiltinType::I32:
        case HIRBuiltinType::I64:
        case HIRBuiltinType::U8:
        case HIRBuiltinType::U16:
        case HIRBuiltinType::U32:
        case HIRBuiltinType::U64:
        case HIRBuiltinType::Isize:
        case HIRBuiltinType::Usize:
        case HIRBuiltinType::F32:
        case HIRBuiltinType::F64:
        case HIRBuiltinType::Str:          return true;
        case HIRBuiltinType::StrBytesView:
        case HIRBuiltinType::StrCharsView:
        case HIRBuiltinType::Void:
        case HIRBuiltinType::EntryArgs:    return false;
    }
    std::unreachable();
}

auto type_compatible(SemanticDraftView hir, HIRTypeID left, HIRTypeID right) noexcept -> bool {
    if (!valid_type(hir, left) || !valid_type(hir, right)) {
        return false;
    }
    const auto& left_value = hir.type(left).value;
    const auto& right_value = hir.type(right).value;
    if (std::holds_alternative<HIRErrorTypeValue>(left_value)
        || std::holds_alternative<HIRErrorTypeValue>(right_value)
        || std::holds_alternative<HIRForeignTypeValue>(left_value)
        || std::holds_alternative<HIRForeignTypeValue>(right_value)) {
        return true;
    }
    return shapes_compatible(hir, left, right);
}

auto callable_adoption_compatible(
    SemanticDraftView hir,
    HIRTypeID target,
    HIRTypeID source,
    std::span<const std::vector<HIRTypeID>> callable_failures
) noexcept -> bool {
    return adoption_compatible(hir, target, source, callable_failures);
}
