module carven:semantic.evaluation.memory;

import :semantic.evaluation.value;
import std;

// Owns one execution's addressable objects. Object indices are never reused.
// Releasing storage invalidates coordinates without keeping the object alive.
class ExecutionMemory final {
public:
    ExecutionMemory() noexcept;
    auto create(ExecutionValue value) noexcept -> ExecutionPlace;
    auto text_bytes(std::shared_ptr<ExecutionTextStorage> storage, TypeID element) noexcept
        -> ExecutionPlace;
    auto release(const ExecutionPlace& place) noexcept -> void;
    // Returned borrows end when the object is assigned, released, or memory is destroyed.
    auto resolve(const ExecutionPlace& place) noexcept -> ExecutionValue*;
    auto resolve(const ExecutionPlace& place) const noexcept -> const ExecutionValue*;
    auto view(const ExecutionSlice& slice) const noexcept -> std::optional<ExecutionSequenceView>;
    auto project(ExecutionPlace place, std::size_t index) noexcept -> std::optional<ExecutionPlace>;
    auto assign(const ExecutionPlace& place, ExecutionValue value) noexcept -> bool;

private:
    std::shared_ptr<const ExecutionStorageIdentity> identity;

    struct TextBytes final {
        std::weak_ptr<const ExecutionTextStorage> storage;
        TypeID element;
        mutable std::map<std::size_t, ExecutionValue> observed;
    };

    std::deque<std::variant<std::optional<ExecutionValue>, TextBytes>> objects;
};
