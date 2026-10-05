module carven:semantic.evaluation.memory.impl;

import :semantic.evaluation.limits;
import :semantic.evaluation.memory;
import :semantic.semir.delegation;
import :semantic.semir.type;
import std;

ExecutionMemory::ExecutionMemory() noexcept
    : identity(ExecutionIdentity::fresh()) {}

auto ExecutionMemory::create(ExecutionValue value) noexcept -> ExecutionPlace {
    const auto index = objects.size();
    objects.emplace_back(std::optional<ExecutionValue>(std::move(value)));
    return {.owner = identity, .object = index, .path = {}};
}

auto ExecutionMemory::text_bytes(const ExecutionText& text, TypeID element) noexcept
    -> std::optional<ExecutionPlace> {
    const auto storage = text.lock();
    if (!storage) {
        return std::nullopt;
    }
    if (const auto found = text_backings.find(storage); found != text_backings.end()) {
        return ExecutionPlace {.owner = identity, .object = found->second, .path = {}};
    }
    const auto index = objects.size();
    objects.emplace_back(TextBytes {.storage = storage, .element = element, .observed = {}});
    text_backings.emplace(storage, index);
    return ExecutionPlace {.owner = identity, .object = index, .path = {}};
}

auto ExecutionMemory::release(const ExecutionPlace& place) noexcept -> void {
    if (place.owner == identity && place.object < objects.size() && place.path.empty()) {
        if (auto* object = std::get_if<std::optional<ExecutionValue>>(&objects[place.object]);
            object && *object) {
            object->reset();
        }
    }
}

auto ExecutionMemory::resolve(const ExecutionPlace& place) const noexcept -> const ExecutionValue* {
    if (place.owner != identity || place.object >= objects.size()) {
        return nullptr;
    }
    if (const auto* text = std::get_if<TextBytes>(&objects[place.object])) {
        const auto storage = text->storage.lock();
        const auto bytes = storage ? std::optional(storage->view()) : std::nullopt;
        if (!bytes || place.path.size() != 1uz || place.path[0] >= bytes->size()) {
            return nullptr;
        }
        const auto index = place.path[0];
        return &text->observed
                    .try_emplace(
                        index,
                        ExecutionByteView {.element = text->element, .bytes = *bytes}[index]
                    )
                    .first->second;
    }
    const auto& object = std::get<std::optional<ExecutionValue>>(objects[place.object]);
    if (!object) {
        return nullptr;
    }
    auto* current = &*object;
    for (const auto index : place.path) {
        const auto* aggregate = std::get_if<ExecutionAggregateValue>(current);
        if (aggregate == nullptr || index >= aggregate->elements.size()) {
            return nullptr;
        }
        current = &aggregate->elements[index];
    }
    return current;
}

auto ExecutionMemory::resolve(const ExecutionPlace& place) noexcept -> ExecutionValue* {
    return const_cast<ExecutionValue*>(std::as_const(*this).resolve(place));
}

auto ExecutionMemory::view(const ExecutionSlice& slice) const noexcept
    -> std::optional<ExecutionSequenceView> {
    const auto& place = slice.backing;
    if (place.owner != identity || place.object >= objects.size()) {
        return std::nullopt;
    }
    if (const auto* text = std::get_if<TextBytes>(&objects[place.object])) {
        const auto storage = text->storage.lock();
        const auto bytes = storage ? std::optional(storage->view()) : std::nullopt;
        if (!bytes
            || !place.path.empty()
            || slice.offset > bytes->size()
            || slice.extent > bytes->size() - slice.offset) {
            return std::nullopt;
        }
        return ExecutionByteView {
            .element = text->element,
            .bytes = bytes->substr(slice.offset, slice.extent)
        };
    }
    const auto* backing = resolve(place);
    const auto* aggregate = backing ? std::get_if<ExecutionAggregateValue>(backing) : nullptr;
    if (aggregate == nullptr
        || slice.offset > aggregate->elements.size()
        || slice.extent > aggregate->elements.size() - slice.offset) {
        return std::nullopt;
    }
    return std::span<const ExecutionValue>(aggregate->elements).subspan(slice.offset, slice.extent);
}

auto ExecutionMemory::project(ExecutionPlace place, std::size_t index) noexcept
    -> std::optional<ExecutionPlace> {
    if (place.owner == identity && place.object < objects.size()) {
        if (const auto* text = std::get_if<TextBytes>(&objects[place.object])) {
            const auto storage = text->storage.lock();
            const auto bytes = storage ? std::optional(storage->view()) : std::nullopt;
            if (!bytes || !place.path.empty() || index >= bytes->size()) {
                return std::nullopt;
            }
            place.path.push_back(index);
            return place;
        }
    }
    const auto* value = resolve(place);
    const auto* aggregate = value ? std::get_if<ExecutionAggregateValue>(value) : nullptr;
    if (aggregate == nullptr || index >= aggregate->elements.size()) {
        return std::nullopt;
    }
    place.path.push_back(index);
    return place;
}

auto ExecutionMemory::assign(const ExecutionPlace& place, ExecutionValue value) noexcept -> bool {
    if (place.owner != identity
        || place.object >= objects.size()
        || std::holds_alternative<TextBytes>(objects[place.object])) {
        return false;
    }
    auto* destination = resolve(place);
    if (destination == nullptr) {
        return false;
    }
    *destination = std::move(value);
    return true;
}

auto ExecutionMemory::proves_standard_layout(
    const ExecutionValueAccess& values,
    TypeID type,
    std::size_t depth
) const noexcept -> bool {
    if (standard_layout_types.contains(type)) {
        return true;
    }
    if (depth > maximum_constant_aggregate_depth) {
        return false;
    }
    const auto canonical = values.type_copy(type);
    auto result = false;
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
        result = builtin_is_numeric(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char;
    } else if (std::holds_alternative<PointerTypeValue>(canonical.value)) {
        result = true;
    } else if (const auto* native = std::get_if<CppTypeValue>(&canonical.value)) {
        result = std::holds_alternative<CppConstCharPointerType>(native->form);
    } else if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
        const auto fields = values.struct_field_types(structure->structure);
        if (!fields) {
            return false;
        }
        for (const auto field : *fields) {
            if (!proves_standard_layout(values, field, depth + 1uz)) {
                return false;
            }
        }
        result = true;
    }
    if (result) {
        standard_layout_types.insert(type);
    }
    return result;
}

auto ExecutionMemory::address_key(
    const ExecutionValueAccess& values,
    const ExecutionPlace& place
) const noexcept -> std::optional<ExecutionPlace> {
    return address_key_from(values, place, 0uz);
}

auto ExecutionMemory::address_key_from(
    const ExecutionValueAccess& values,
    const ExecutionPlace& place,
    std::size_t root_depth
) const noexcept -> std::optional<ExecutionPlace> {
    if (place.owner != identity || place.object >= objects.size()) {
        return std::nullopt;
    }
    if (std::holds_alternative<TextBytes>(objects[place.object])) {
        // A text buffer is not an independent native object; its relation to
        // the String owner or enclosing aggregate depends on that representation.
        return std::nullopt;
    }
    const auto& object = std::get<std::optional<ExecutionValue>>(objects[place.object]);
    if (!object || !resolve(place)) {
        return std::nullopt;
    }
    if (place.path.empty()) {
        return place;
    }
    auto type = execution_value_type(values, *object);
    auto retained_depth = root_depth;
    auto unknown_tail = false;
    for (const auto [position, index] : std::views::enumerate(place.path)) {
        const auto depth = static_cast<std::size_t>(position);
        auto first_at_parent_address = false;
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            const auto construction = values.construction_type_copy(*term);
            const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value);
            if (!array || index >= array->extent) {
                return std::nullopt;
            }
            type = array->element;
        } else {
            const auto concrete = std::get<TypeID>(type);
            const auto canonical = values.type_copy(concrete);
            if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
                const auto fields = values.struct_field_types(structure->structure);
                if (!fields || index >= fields->size()) {
                    return std::nullopt;
                }
                if (depth >= root_depth && index == 0uz) {
                    first_at_parent_address = proves_standard_layout(values, concrete, 0uz);
                }
                type = (*fields)[index];
            } else if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
                if (index >= array->extent) {
                    return std::nullopt;
                }
                // std::array establishes element contiguity, not container/element overlap.
                type = array->element;
            } else {
                return std::nullopt;
            }
        }
        if (depth >= root_depth) {
            if (index != 0uz) {
                retained_depth = depth + 1uz;
                unknown_tail = false;
            } else if (!first_at_parent_address) {
                unknown_tail = true;
            }
        }
    }
    if (unknown_tail) {
        return std::nullopt;
    }
    auto result = place;
    result.path.resize(retained_depth);
    return result;
}

auto ExecutionMemory::same_address(
    const ExecutionValueAccess& values,
    const ExecutionPlace& first,
    const ExecutionPlace& second
) const noexcept -> std::optional<bool> {
    if (first == second) {
        return true;
    }
    if (!resolve(first) || !resolve(second)) {
        return std::nullopt;
    }
    if (first.owner != second.owner || first.object != second.object) {
        const auto* left = std::get_if<TextBytes>(&objects[first.object]);
        const auto* right = std::get_if<TextBytes>(&objects[second.object]);
        if (left || right) {
            if (left && right) {
                const auto lhs = left->storage.lock();
                const auto rhs = right->storage.lock();
                // Independent owned buffers are disjoint. Retained spellings
                // can overlap through native literal pooling.
                if (lhs
                    && rhs
                    && std::holds_alternative<std::string>(lhs->bytes)
                    && std::holds_alternative<std::string>(rhs->bytes)) {
                    return false;
                }
            }
            return std::nullopt;
        }
        return false;
    }
    // Live sibling subobjects are disjoint even when their parent address is unknown.
    const auto common = std::min(first.path.size(), second.path.size());
    for (auto index = 0uz; index < common; ++index) {
        if (first.path[index] != second.path[index]) {
            return false;
        }
    }
    // Compare only the remaining projections. Unknown outer layout does not
    // hide established address relations within the shared ancestor.
    const auto left = address_key_from(values, first, common);
    const auto right = address_key_from(values, second, common);
    if (left && right) {
        return *left == *right;
    }
    return std::nullopt;
}
