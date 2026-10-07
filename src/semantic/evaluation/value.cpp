module carven:semantic.evaluation.value.impl;

import :semantic.evaluation.memory;
import :semantic.evaluation.operation;
import :semantic.evaluation.value;
import :support.invariant;
import std;

auto ExecutionIdentity::fresh() noexcept -> ExecutionIdentity {
    static auto next_identity = std::atomic<std::uint64_t> {1u};
    const auto value = next_identity.fetch_add(1u, std::memory_order_relaxed);
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        resource_limit_exceeded("execution identity space exhausted");
    }
    return ExecutionIdentity(value);
}

auto constant_atom(const ConstantFact& fact) noexcept -> std::optional<ConstantAtom> {
    return fact.value.visit([&](const auto& value) noexcept -> std::optional<ConstantAtom> {
        if constexpr (std::constructible_from<ConstantAtomValue, decltype(value)>) {
            return ConstantAtom {.type = fact.type, .value = value};
        }
        return std::nullopt;
    });
}

auto constant_fact(const ConstantAtom& atom) noexcept -> ConstantFact {
    return atom.value.visit([&](const auto& value) noexcept {
        return ConstantFact {.type = atom.type, .value = value};
    });
}

auto execution_value_type(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> ConstructionTypeRef {
    if (const auto* constant = std::get_if<ConstantID>(&value)) {
        return values.constant(*constant).type;
    }
    if (const auto* fact = std::get_if<ConstantAtom>(&value)) {
        return fact->type;
    }
    if (const auto* pointer = std::get_if<ExecutionPointer>(&value)) {
        return pointer->type;
    }
    if (const auto* slice = std::get_if<ExecutionSlice>(&value)) {
        return slice->type;
    }
    if (const auto* function = std::get_if<ExecutionFunction>(&value)) {
        return function->type;
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
    if (const auto* pointer = std::get_if<ExecutionPointer>(&value); pointer && !pointer->target) {
        if (const auto* type = std::get_if<TypeID>(&pointer->type)) {
            return ConstantAtom {.type = *type, .value = NullPointerConstant {}};
        }
    }
    return std::nullopt;
}

ExecutionTextStorage::ExecutionTextStorage(
    std::variant<std::string, std::string_view> bytes
) noexcept
    : bytes(std::move(bytes)) {}

auto ExecutionTextStorage::view() const noexcept -> std::string_view {
    return bytes.visit([](const auto& value) static noexcept { return std::string_view(value); });
}

ExecutionText::ExecutionText(std::string bytes) noexcept
    : storage(std::make_shared<const ExecutionTextStorage>(std::move(bytes))) {}

ExecutionText::ExecutionText(std::shared_ptr<const ExecutionTextStorage> storage) noexcept
    : storage(std::move(storage)) {}

ExecutionText::ExecutionText(std::weak_ptr<const ExecutionTextStorage> storage) noexcept
    : storage(std::move(storage)) {}

auto ExecutionText::retained(std::string_view bytes) noexcept -> ExecutionText {
    return ExecutionText(std::make_shared<const ExecutionTextStorage>(bytes));
}

auto ExecutionText::lock() const noexcept -> std::shared_ptr<const ExecutionTextStorage> {
    if (const auto* owned = std::get_if<std::shared_ptr<const ExecutionTextStorage>>(&storage)) {
        return *owned;
    }
    return std::get<std::weak_ptr<const ExecutionTextStorage>>(storage).lock();
}

auto ExecutionText::borrow() const noexcept -> ExecutionText {
    return storage.visit([](const auto& source) static noexcept {
        return ExecutionText(std::weak_ptr<const ExecutionTextStorage>(source));
    });
}

auto ExecutionText::is_borrowed() const noexcept -> bool {
    return std::holds_alternative<std::weak_ptr<const ExecutionTextStorage>>(storage);
}

auto ExecutionText::bytes() const noexcept -> std::optional<std::string_view> {
    const auto selected = lock();
    if (!selected) {
        return std::nullopt;
    }
    return selected->view();
}

ExecutionOwnedText::ExecutionOwnedText(std::string bytes) noexcept
    : storage(std::make_shared<ExecutionTextStorage>(std::move(bytes))) {}

auto ExecutionOwnedText::bytes() const noexcept -> std::string_view {
    return storage->view();
}

auto ExecutionOwnedText::borrow() const noexcept -> ExecutionText {
    return ExecutionText(std::weak_ptr<const ExecutionTextStorage>(storage));
}

auto ExecutionOwnedText::append(std::string_view bytes) noexcept -> void {
    auto* owned = std::get_if<std::string>(&storage->bytes);
    if (owned == nullptr) {
        invariant_violation("owning execution text has no owned bytes");
    }
    auto replacement = std::move(*owned);
    replacement += bytes;
    storage = std::make_shared<ExecutionTextStorage>(std::move(replacement));
}

auto ExecutionOwnedText::transfer() noexcept -> void {
    auto* owned = std::get_if<std::string>(&storage->bytes);
    if (owned == nullptr) {
        invariant_violation("owning execution text has no owned bytes");
    }
    auto replacement = std::move(*owned);
    storage = std::make_shared<ExecutionTextStorage>(std::move(replacement));
}

auto execution_text(const ConstantValueReader& values, const ExecutionValue& value) noexcept
    -> std::optional<std::string_view> {
    if (const auto* text = std::get_if<ExecutionText>(&value)) {
        return text->bytes();
    }
    if (const auto* text = std::get_if<ExecutionOwnedText>(&value)) {
        return text->bytes();
    }
    if (const auto atom = execution_atom(values, value)) {
        if (const auto* text = std::get_if<StringConstant>(&atom->value)) {
            return values.spelling(text->value);
        }
    }
    return std::nullopt;
}

auto transfer_owned_text(ExecutionValue& value) noexcept -> void {
    if (auto* text = std::get_if<ExecutionOwnedText>(&value)) {
        text->transfer();
    }
    for (auto& element : execution_elements(value)) {
        transfer_owned_text(element);
    }
}

auto ExecutionByteView::size() const noexcept -> std::size_t {
    return bytes.size();
}

auto ExecutionByteView::operator[](std::size_t index) const noexcept -> ExecutionValue {
    return ConstantAtom {
        .type = element,
        .value = IntegerConstant::from_parts(static_cast<unsigned char>(bytes[index]), false)
    };
}

auto execution_equal(
    const ExecutionValueAccess& values,
    const ExecutionValue& left,
    const ExecutionValue& right,
    std::size_t& steps,
    std::size_t maximum_steps,
    const ExecutionMemory* memory
) noexcept -> std::expected<bool, ExecutionComparisonFailure> {
    if (steps >= maximum_steps) {
        return std::unexpected(ExecutionComparisonFailure::StepLimit);
    }
    ++steps;
    const auto* lhs_pointer = std::get_if<ExecutionPointer>(&left);
    const auto* rhs_pointer = std::get_if<ExecutionPointer>(&right);
    if (lhs_pointer || rhs_pointer) {
        if (lhs_pointer && rhs_pointer) {
            if (lhs_pointer->target && rhs_pointer->target) {
                if (*lhs_pointer->target == *rhs_pointer->target) {
                    return true;
                }
                const auto relation = memory
                    ? memory->same_address(values, *lhs_pointer->target, *rhs_pointer->target)
                    : std::nullopt;
                if (!relation) {
                    return std::unexpected(ExecutionComparisonFailure::UnknownAddress);
                }
                return *relation;
            }
            return lhs_pointer->target == rhs_pointer->target;
        }
        const auto other = execution_atom(values, lhs_pointer ? right : left);
        return other
            && std::holds_alternative<NullPointerConstant>(other->value)
            && !(lhs_pointer ? lhs_pointer : rhs_pointer)->target;
    }
    const auto lhs_text = execution_text(values, left);
    const auto rhs_text = execution_text(values, right);
    if ((std::holds_alternative<ExecutionText>(left) && !lhs_text)
        || (std::holds_alternative<ExecutionText>(right) && !rhs_text)) {
        return std::unexpected(ExecutionComparisonFailure::ExpiredText);
    }
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
            [&](const auto left_children, const auto right_children) noexcept
                -> std::expected<bool, ExecutionComparisonFailure> {
                if (left_children.size() != right_children.size()) {
                    return false;
                }
                for (auto index = 0uz; index < left_children.size(); ++index) {
                    const auto equal = execution_equal(
                        values,
                        left_children[index],
                        right_children[index],
                        steps,
                        maximum_steps,
                        memory
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
    if ((lhs_fact && std::holds_alternative<CStringConstant>(lhs_fact->value))
        || (rhs_fact && std::holds_alternative<CStringConstant>(rhs_fact->value))) {
        return std::unexpected(ExecutionComparisonFailure::Unsupported);
    }
    return lhs_fact
        && rhs_fact
        && constant_value_equal(
               values,
               constant_fact(*lhs_fact).value,
               constant_fact(*rhs_fact).value
        );
}

auto ExecutionCompoundView::size() const noexcept -> std::size_t {
    return elements.visit([](const auto children) static noexcept { return children.size(); });
}

auto execution_compound_view(
    const ConstantValueReader& values,
    const ExecutionValue& value,
    const ExecutionMemory* memory
) noexcept -> std::optional<ExecutionCompoundView> {
    if (const auto* slice = std::get_if<ExecutionSlice>(&value)) {
        const auto children = memory ? memory->view(*slice) : std::nullopt;
        if (!children) {
            return std::nullopt;
        }
        return ExecutionCompoundView {
            .type = slice->type,
            .enum_case = std::nullopt,
            .elements = children->visit(
                [](const auto& view) static noexcept -> decltype(ExecutionCompoundView::elements) {
                    return view;
                }
            )
        };
    }
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
