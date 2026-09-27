module carven:semantic.evaluation.memory.impl;

import :semantic.evaluation.memory;
import std;

ExecutionMemory::ExecutionMemory() noexcept
    : identity(std::make_shared<const ExecutionStorageIdentity>()) {}

auto ExecutionMemory::create(ExecutionValue value) noexcept -> ExecutionPlace {
    const auto index = objects.size();
    objects.emplace_back(std::optional<ExecutionValue>(std::move(value)));
    return {.owner = identity, .object = index, .path = {}};
}

auto ExecutionMemory::text_bytes(
    const std::shared_ptr<ExecutionTextStorage>& storage,
    TypeID element
) noexcept -> ExecutionPlace {
    if (storage->byte_backing && storage->byte_backing->owner == identity) {
        return *storage->byte_backing;
    }
    const auto index = objects.size();
    objects.emplace_back(TextBytes {.storage = storage, .element = element, .observed = {}});
    auto place = ExecutionPlace {.owner = identity, .object = index, .path = {}};
    storage->byte_backing = place;
    return place;
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
        const auto bytes = storage ? std::optional(execution_text_bytes(*storage)) : std::nullopt;
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
        const auto bytes = storage ? std::optional(execution_text_bytes(*storage)) : std::nullopt;
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
            const auto bytes =
                storage ? std::optional(execution_text_bytes(*storage)) : std::nullopt;
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
