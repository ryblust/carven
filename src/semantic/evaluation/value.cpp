module carven:semantic.evaluation.value.impl;

import :semantic.evaluation.operation;
import :semantic.evaluation.value;
import std;

auto constant_atom(const ConstantFact& fact) noexcept -> std::optional<ConstantAtom> {
    return std::visit(
        [&](const auto& value) noexcept -> std::optional<ConstantAtom> {
            if constexpr (std::constructible_from<ConstantAtomValue, decltype(value)>) {
                return ConstantAtom {.type = fact.type, .value = value};
            }
            return std::nullopt;
        },
        fact.value
    );
}

auto constant_fact(const ConstantAtom& atom) noexcept -> ConstantFact {
    return std::visit(
        [&](const auto& value) noexcept {
            return ConstantFact {.type = atom.type, .value = value};
        },
        atom.value
    );
}

auto execution_value_type(ExecutionValueAccess& values, const ExecutionValue& value) noexcept
    -> TypeID {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        return values.constant(*constant).type;
    }
    if (const auto* fact = std::get_if<ConstantAtom>(&value)) {
        return fact->type;
    }
    if (std::holds_alternative<ExecutionText>(value)) {
        return values.builtin_type(BuiltinType::Str);
    }
    if (const auto* enumeration = std::get_if<ExecutionEnumValue>(&value)) {
        return enumeration->type;
    }
    if (const auto* aggregate = std::get_if<ExecutionAggregateValue>(&value)) {
        return aggregate->type;
    }
    return values.builtin_type(
        std::holds_alternative<ExecutionOwnedText>(value) ? BuiltinType::String : BuiltinType::Void
    );
}

auto execution_atom(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<ConstantAtom> {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        return constant_atom(values.constant(*constant));
    }
    if (const auto* atom = std::get_if<ConstantAtom>(&value)) {
        return *atom;
    }
    return std::nullopt;
}

auto execution_text(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<std::string_view> {
    if (const auto* text = std::get_if<ExecutionText>(&value)) {
        return *text->bytes;
    }
    if (const auto* text = std::get_if<ExecutionOwnedText>(&value)) {
        return text->bytes;
    }
    if (const auto atom = execution_atom(values, value)) {
        if (const auto* text = std::get_if<StringConstant>(&atom->value)) {
            return values.spelling(text->value);
        }
    }
    return std::nullopt;
}

auto execution_equal(
    const ConstantValueReader& values,
    const ExecutionValue& left,
    const ExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps
) noexcept -> std::optional<bool> {
    if (steps >= maximum_steps) {
        return std::nullopt;
    }
    ++steps;
    const auto lhs_text = execution_text(values, left);
    const auto rhs_text = execution_text(values, right);
    if (lhs_text || rhs_text) {
        if (!lhs_text || !rhs_text || lhs_text->size() != rhs_text->size()) {
            return false;
        }
        return lhs_text->data() == rhs_text->data() || *lhs_text == *rhs_text;
    }
    const auto lhs = execution_compound_view(values, left);
    const auto rhs = execution_compound_view(values, right);
    if (lhs || rhs) {
        if (!lhs || !rhs || lhs->type != rhs->type || lhs->enum_case != rhs->enum_case) {
            return false;
        }
        return std::visit(
            [&](const auto left_children,
                const auto right_children) noexcept -> std::optional<bool> {
                if (left_children.size() != right_children.size()) {
                    return false;
                }
                for (auto index = 0uz; index < left_children.size(); ++index) {
                    const auto equal = execution_equal(
                        values,
                        left_children[index],
                        right_children[index],
                        steps,
                        maximum_steps
                    );
                    if (!equal || !*equal) {
                        return equal;
                    }
                }
                return true;
            },
            lhs->elements,
            rhs->elements
        );
    }
    const auto lhs_fact = execution_atom(values, left);
    const auto rhs_fact = execution_atom(values, right);
    return lhs_fact
        && rhs_fact
        && constant_value_equal(
               values,
               constant_fact(*lhs_fact).value,
               constant_fact(*rhs_fact).value
        );
}

auto ExecutionCompoundView::size() const noexcept -> std::size_t {
    return std::visit(
        [](const auto children) static noexcept { return children.size(); },
        elements
    );
}

auto execution_compound_view(
    const ConstantValueReader& values,
    const ExecutionValue& value
) noexcept -> std::optional<ExecutionCompoundView> {
    if (const auto* local = std::get_if<ExecutionAggregateValue>(&value)) {
        return ExecutionCompoundView {
            .type = local->type,
            .enum_case = std::nullopt,
            .elements = std::span<const ExecutionValue>(local->elements)
        };
    }
    if (const auto* local = std::get_if<ExecutionEnumValue>(&value)) {
        return ExecutionCompoundView {
            .type = local->type,
            .enum_case = local->enum_case,
            .elements = std::span<const ExecutionValue>(local->payload)
        };
    }
    const auto* retained = std::get_if<ConstantID>(&value);
    const auto* fact = retained ? &values.constant(*retained) : nullptr;
    if (!fact) {
        return std::nullopt;
    }
    const auto children = constant_children(fact->value);
    if (!children) {
        return std::nullopt;
    }
    const auto* payload = std::get_if<PayloadEnumConstant>(&fact->value);
    return ExecutionCompoundView {
        .type = fact->type,
        .enum_case = payload ? std::optional(payload->enum_case) : std::nullopt,
        .elements = *children
    };
}

auto execution_elements(ExecutionValue& value) noexcept -> std::span<ExecutionValue> {
    if (auto* aggregate = std::get_if<ExecutionAggregateValue>(&value)) {
        return aggregate->elements;
    }
    if (auto* enumeration = std::get_if<ExecutionEnumValue>(&value)) {
        return enumeration->payload;
    }
    return {};
}
