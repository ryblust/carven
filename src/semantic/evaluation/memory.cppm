module carven:semantic.evaluation.memory;

import :semantic.evaluation.value;
import std;

// Owns one execution's addressable objects. Object indices are never reused.
// Releasing storage invalidates coordinates without keeping the object alive.
class ExecutionMemory final {
public:
    ExecutionMemory() noexcept;
    ExecutionMemory(const ExecutionMemory&) = delete;
    ExecutionMemory(ExecutionMemory&&) = delete;
    auto operator=(const ExecutionMemory&) -> ExecutionMemory& = delete;
    auto operator=(ExecutionMemory&&) -> ExecutionMemory& = delete;
    ~ExecutionMemory() = default;
    auto create(ExecutionValue value) noexcept -> ExecutionPlace;
    auto text_bytes(const ExecutionText& text, TypeID element) noexcept
        -> std::optional<ExecutionPlace>;
    auto release(const ExecutionPlace& place) noexcept -> void;
    // Returned borrows end when the object is assigned, released, or memory is destroyed.
    auto resolve(const ExecutionPlace& place) noexcept -> ExecutionValue*;
    auto resolve(const ExecutionPlace& place) const noexcept -> const ExecutionValue*;
    auto view(const ExecutionSlice& slice) const noexcept -> std::optional<ExecutionSequenceView>;
    auto project(ExecutionPlace place, std::size_t index) noexcept -> std::optional<ExecutionPlace>;
    auto assign(const ExecutionPlace& place, ExecutionValue value) noexcept -> bool;
    // Derives canonical coordinates from established native layout relations.
    // Missing storage or type facts leave the address relation unknown.
    auto address_key(const ExecutionValueAccess& values, const ExecutionPlace& place) const noexcept
        -> std::optional<ExecutionPlace>;
    auto same_address(
        const ExecutionValueAccess& values,
        const ExecutionPlace& first,
        const ExecutionPlace& second
    ) const noexcept -> std::optional<bool>;

private:
    auto address_key_from(
        const ExecutionValueAccess& values,
        const ExecutionPlace& place,
        std::size_t root_depth
    ) const noexcept -> std::optional<ExecutionPlace>;
    auto proves_standard_layout(
        const ExecutionValueAccess& values,
        TypeID type,
        std::size_t depth
    ) const noexcept -> bool;
    ExecutionIdentity identity;
    mutable std::set<TypeID> standard_layout_types;

    struct TextBytes final {
        std::weak_ptr<const ExecutionTextStorage> storage;
        TypeID element;
        mutable std::map<std::size_t, ExecutionValue> observed;
    };

    std::deque<std::variant<std::optional<ExecutionValue>, TextBytes>> objects;
    std::map<std::weak_ptr<const ExecutionTextStorage>, std::size_t, std::owner_less<>>
        text_backings;
};
