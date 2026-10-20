module carven:semantic.evaluation.memory;

import :semantic.evaluation.value;
import std;

// Ordinary reads borrow storage; byte elements materialize one scalar for this read.
class ExecutionRead final {
public:
    explicit ExecutionRead(const ExecutionValue& value) noexcept;
    explicit ExecutionRead(ExecutionValue&& value) noexcept;
    auto get() const noexcept -> const ExecutionValue&;

private:
    std::variant<const ExecutionValue*, ExecutionValue> value;
    friend class ExecutionMemory;
};

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
    auto create(ExecutionValue&& value) noexcept -> ExecutionPlace;

    // Give a product slot its own logical lifetime without relocating storage.
    auto adopt_projection(ExecutionPlace place) noexcept -> ExecutionPlace;

    auto text_bytes(const ExecutionText& text, TypeID element) noexcept
        -> std::optional<ExecutionPlace>;

    auto release(const ExecutionPlace& place) noexcept -> void;

    // Returned borrows end when the object is assigned, released, or memory is destroyed.
    auto read(const ExecutionPlace& place) const noexcept -> std::optional<ExecutionRead>;
    auto resolve(const ExecutionPlace& place) noexcept -> ExecutionValue*;
    auto resolve(const ExecutionPlace& place) const noexcept -> const ExecutionValue*;
    auto owning_value(const ExecutionPlace& place) const noexcept -> const ExecutionValue*;

    // Expands logical projection owners to the coordinates of their backing object.
    auto storage_place(ExecutionPlace place) const noexcept -> std::optional<ExecutionPlace>;

    auto view(const ExecutionPlace& place, std::size_t offset, std::size_t extent) const noexcept
        -> std::optional<ExecutionSequenceView>;

    auto project(ExecutionPlace place, std::size_t index) noexcept -> std::optional<ExecutionPlace>;

    auto assign(
        const ConstantValueReader& values,
        const ExecutionPlace& place,
        ExecutionValue&& value
    ) noexcept -> bool;

    // Derives canonical coordinates from established native layout relations.
    // Missing storage or type facts leave the address relation unknown.
    auto address_key(const ExecutionValueAccess& values, const ExecutionPlace& place) const noexcept
        -> std::optional<ExecutionPlace>;

    auto same_address(
        const ExecutionValueAccess& values,
        const ExecutionPlace& first,
        const ExecutionPlace& second
    ) const noexcept -> std::optional<bool>;

    // Selects a live object of the requested type at the preserved address.
    auto access_address(
        const ExecutionValueAccess& values,
        const ExecutionPlace& place,
        ConstructionTypeRef type
    ) const noexcept -> std::optional<ExecutionPlace>;

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
    };

    struct Projection final {
        std::optional<ExecutionPlace> target;
    };

    std::deque<std::variant<std::optional<ExecutionValue>, TextBytes, Projection>> objects;
    std::map<std::weak_ptr<const ExecutionTextStorage>, std::size_t, std::owner_less<>>
        text_backings;
};
