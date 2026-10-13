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
    if (const auto* operation = std::get_if<std::unique_ptr<ExecutionColdOperation>>(&value)) {
        if (!*operation) {
            invariant_violation("consumed cold operation has no execution value type");
        }
        return (*operation)->type;
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
    if (std::holds_alternative<ExecutionVoid>(value)) {
        return ConstantAtom {
            .type = values.builtin_type(BuiltinType::Void),
            .value = VoidConstant {}
        };
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

ExecutionTextObservation::ExecutionTextObservation(std::string_view bytes) noexcept
    : bytes(bytes) {}

ExecutionTextObservation::ExecutionTextObservation(std::string bytes) noexcept
    : bytes(std::move(bytes)) {}

auto ExecutionTextObservation::view() const noexcept -> std::string_view {
    return bytes.visit([](const auto& value) static noexcept { return std::string_view(value); });
}

auto ExecutionTextObservation::size() const noexcept -> std::size_t {
    return view().size();
}

auto ExecutionTextObservation::operator==(const ExecutionTextObservation& other) const noexcept
    -> bool {
    return view() == other.view();
}

auto ExecutionTextObservation::operator==(std::string_view other) const noexcept -> bool {
    return view() == other;
}

ExecutionText::ExecutionText(ExecutionSlice bytes) noexcept
    : storage(std::move(bytes)) {}

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
    const auto* borrowed = std::get_if<std::weak_ptr<const ExecutionTextStorage>>(&storage);
    return borrowed ? borrowed->lock() : nullptr;
}

auto ExecutionText::borrow() const noexcept -> ExecutionText {
    return storage.visit([](const auto& source) static noexcept {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(source)>, ExecutionSlice>) {
            return ExecutionText(source);
        } else {
            return ExecutionText(std::weak_ptr<const ExecutionTextStorage>(source));
        }
    });
}

auto ExecutionText::is_borrowed() const noexcept -> bool {
    return !std::holds_alternative<std::shared_ptr<const ExecutionTextStorage>>(storage);
}

auto ExecutionText::size(const ExecutionMemory* memory) const noexcept
    -> std::optional<std::size_t> {
    if (const auto* borrowed = slice()) {
        return alive(memory) ? std::optional(borrowed->extent) : std::nullopt;
    }
    const auto selected = lock();
    return selected ? std::optional(selected->view().size()) : std::nullopt;
}

auto ExecutionText::alive(const ExecutionMemory* memory) const noexcept -> bool {
    if (const auto* borrowed = slice()) {
        return memory && memory->view(*borrowed).has_value();
    }
    return bool(lock());
}

auto ExecutionText::slice() const noexcept -> const ExecutionSlice* {
    return std::get_if<ExecutionSlice>(&storage);
}

auto ExecutionText::observe(
    const ConstantValueReader& values,
    const ExecutionMemory* memory
) const noexcept -> std::optional<ExecutionTextObservation> {
    if (const auto* borrowed = slice()) {
        const auto elements = memory ? memory->view(*borrowed) : std::nullopt;
        if (!elements) {
            return std::nullopt;
        }
        if (const auto* text = std::get_if<ExecutionByteView>(&*elements)) {
            return ExecutionTextObservation(text->bytes);
        }
        const auto slots = std::get<std::span<const ExecutionValue>>(*elements);
        auto bytes = std::string();
        bytes.reserve(slots.size());
        for (const auto& slot : slots) {
            const auto atom = execution_atom(values, slot);
            const auto* integer = atom ? std::get_if<IntegerConstant>(&atom->value) : nullptr;
            if (!integer || integer->negative() || integer->magnitude() > 255u) {
                return std::nullopt;
            }
            bytes.push_back(static_cast<char>(integer->magnitude()));
        }
        return ExecutionTextObservation(std::move(bytes));
    }
    const auto selected = lock();
    return selected ? std::optional(ExecutionTextObservation(selected->view())) : std::nullopt;
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

auto execution_text(
    const ConstantValueReader& values,
    const ExecutionValue& value,
    const ExecutionMemory* memory
) noexcept -> std::optional<ExecutionTextObservation> {
    if (const auto* text = std::get_if<ExecutionText>(&value)) {
        return text->observe(values, memory);
    }
    if (const auto* text = std::get_if<ExecutionOwnedText>(&value)) {
        return ExecutionTextObservation(text->bytes());
    }
    if (const auto atom = execution_atom(values, value)) {
        if (const auto* text = std::get_if<StringConstant>(&atom->value)) {
            return ExecutionTextObservation(values.spelling(text->value));
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
    const auto lhs_text = execution_text(values, left, memory);
    const auto rhs_text = execution_text(values, right, memory);
    if ((std::holds_alternative<ExecutionText>(left) && !lhs_text)
        || (std::holds_alternative<ExecutionText>(right) && !rhs_text)) {
        return std::unexpected(ExecutionComparisonFailure::ExpiredText);
    }
    if (lhs_text || rhs_text) {
        if (!lhs_text || !rhs_text || lhs_text->size() != rhs_text->size()) {
            return false;
        }
        return *lhs_text == *rhs_text;
    }
    const auto lhs = execution_compound_view(values, left, memory);
    const auto rhs = execution_compound_view(values, right, memory);
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
