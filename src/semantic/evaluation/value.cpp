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

auto constant_execution_value_type(
    ConstantValueAccess& values,
    const ConstantExecutionValue& value
) noexcept -> TypeID {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        return values.constant(*constant).type;
    }
    if (const auto* fact = std::get_if<ConstantAtom>(&value)) {
        return fact->type;
    }
    if (std::holds_alternative<ConstantText>(value)) {
        return values.intern_builtin_type(BuiltinType::Str);
    }
    if (const auto* enumeration = std::get_if<ConstantEnumValue>(&value)) {
        return enumeration->type;
    }
    if (const auto* aggregate = std::get_if<ConstantAggregateValue>(&value)) {
        return aggregate->type;
    }
    return values.intern_builtin_type(
        std::holds_alternative<ConstantOwnedText>(value) ? BuiltinType::String : BuiltinType::Void
    );
}

auto constant_execution_atom(
    const ConstantValueReader& values,
    const ConstantExecutionValue& value
) noexcept -> std::optional<ConstantAtom> {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        return constant_atom(values.constant(*constant));
    }
    if (const auto* atom = std::get_if<ConstantAtom>(&value)) {
        return *atom;
    }
    return std::nullopt;
}

auto constant_execution_text(
    const ConstantValueReader& values,
    const ConstantExecutionValue& value
) noexcept -> std::optional<std::string_view> {
    if (const auto* text = std::get_if<ConstantText>(&value)) {
        return *text->bytes;
    }
    if (const auto* text = std::get_if<ConstantOwnedText>(&value)) {
        return text->bytes;
    }
    if (const auto atom = constant_execution_atom(values, value)) {
        if (const auto* text = std::get_if<StringConstant>(&atom->value)) {
            return values.spelling(text->value);
        }
    }
    return std::nullopt;
}

auto constant_execution_equal(
    const ConstantValueReader& values,
    const ConstantExecutionValue& left,
    const ConstantExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps
) noexcept -> std::optional<bool> {
    if (steps >= maximum_steps) {
        return std::nullopt;
    }
    ++steps;
    const auto lhs_text = constant_execution_text(values, left);
    const auto rhs_text = constant_execution_text(values, right);
    if (lhs_text || rhs_text) {
        if (!lhs_text || !rhs_text || lhs_text->size() != rhs_text->size()) {
            return false;
        }
        return lhs_text->data() == rhs_text->data() || *lhs_text == *rhs_text;
    }
    const auto lhs = constant_compound_view(values, left);
    const auto rhs = constant_compound_view(values, right);
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
                    const auto equal = constant_execution_equal(
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
    const auto lhs_fact = constant_execution_atom(values, left);
    const auto rhs_fact = constant_execution_atom(values, right);
    return lhs_fact
        && rhs_fact
        && constant_value_equal(
               values,
               constant_fact(*lhs_fact).value,
               constant_fact(*rhs_fact).value
        );
}

auto ConstantCompoundView::size() const noexcept -> std::size_t {
    return std::visit(
        [](const auto children) static noexcept { return children.size(); },
        elements
    );
}

auto constant_compound_view(
    const ConstantValueReader& values,
    const ConstantExecutionValue& value
) noexcept -> std::optional<ConstantCompoundView> {
    if (const auto* local = std::get_if<ConstantAggregateValue>(&value)) {
        return ConstantCompoundView {
            .type = local->type,
            .enum_case = std::nullopt,
            .elements = std::span<const ConstantExecutionValue>(local->elements)
        };
    }
    if (const auto* local = std::get_if<ConstantEnumValue>(&value)) {
        return ConstantCompoundView {
            .type = local->type,
            .enum_case = local->enum_case,
            .elements = std::span<const ConstantExecutionValue>(local->payload)
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
    return ConstantCompoundView {
        .type = fact->type,
        .enum_case = payload ? std::optional(payload->enum_case) : std::nullopt,
        .elements = *children
    };
}

auto constant_execution_elements(ConstantExecutionValue& value) noexcept
    -> std::span<ConstantExecutionValue> {
    if (auto* aggregate = std::get_if<ConstantAggregateValue>(&value)) {
        return aggregate->elements;
    }
    if (auto* enumeration = std::get_if<ConstantEnumValue>(&value)) {
        return enumeration->payload;
    }
    return {};
}
