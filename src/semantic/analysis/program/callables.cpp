module carven:semantic.analysis.program.callables.impl;

import :semantic.analysis.program;
import :semantic.semir.decl;
import :semantic.semir.type;
import std;

auto ProgramDraft::callable_shape(ConstructionTypeRef type) const noexcept
    -> std::optional<ConstructionCallableShape> {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = type_copy(*concrete);
        auto callable = std::optional<CallableID>();
        if (const auto* function = std::get_if<FunctionTypeValue>(&canonical.value)) {
            callable = function->callable;
        } else if (const auto* closure = std::get_if<ClosureTypeValue>(&canonical.value)) {
            callable = closure->callable;
        } else if (const auto* view = std::get_if<CallableViewTypeValue>(&canonical.value)) {
            const auto signature = callable_signature_copy(view->signature);
            auto parameters = std::vector<ConstructionCallableParameter>();
            parameters.reserve(signature.parameters.size());
            for (const auto& parameter : signature.parameters) {
                parameters.push_back(
                    ConstructionCallableParameter {
                        .stage = parameter.stage,
                        .access = parameter.access,
                        .type = parameter.type,
                    }
                );
            }
            return ConstructionCallableShape {
                .owning_type = std::nullopt,
                .parameters = std::move(parameters),
                .result = signature.result,
                .failures = signature.failures,
                .policy = FailureContractPolicy::Declared,
            };
        }
        if (!callable.has_value()) {
            return std::nullopt;
        }
        auto contract = construction_callable_contract_copy(*callable);
        return ConstructionCallableShape {
            .owning_type = *concrete,
            .parameters = std::move(contract.parameters),
            .result = contract.result,
            .failures = contract.failures,
            .policy = contract.policy,
        };
    }

    const auto construction = construction_type_copy(std::get<TypeTermID>(type));
    const auto* view = std::get_if<ConstructionCallableViewTypeValue>(&construction.value);
    if (view == nullptr) {
        return std::nullopt;
    }
    return ConstructionCallableShape {
        .owning_type = std::nullopt,
        .parameters = view->parameters,
        .result = view->result,
        .failures = view->failures,
        .policy = FailureContractPolicy::Declared,
    };
}

auto ProgramDraft::callable_contract(ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionCallableContract> {
    auto shape = callable_shape(type);
    if (!shape.has_value()) {
        return std::nullopt;
    }
    const auto failures = [&]() noexcept -> FailureTermID {
        if (const auto* term = std::get_if<FailureTermID>(&shape->failures)) {
            return *term;
        }
        const auto set = std::get<FailureSetID>(shape->failures);
        const auto found = storage.known_failure_terms.find(set);
        if (found != storage.known_failure_terms.end()) {
            return found->second;
        }
        const auto term = storage.failure_constraints.add_known_term(failure_set_copy(set).members);
        storage.known_failure_terms.emplace(set, term);
        return term;
    }();
    return ConstructionCallableContract {
        .parameters = std::move(shape->parameters),
        .result = shape->result,
        .failures = failures,
        .policy = shape->policy,
    };
}
