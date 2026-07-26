module carven:support.id_table;

import :support.invariant;
import std;

template<typename Value, typename ID>
class IDTable final {
public:
    using Checkpoint = std::size_t;

    IDTable() = default;
    IDTable(const IDTable&) = delete;
    IDTable(IDTable&&) = default;
    ~IDTable() = default;

    auto operator=(const IDTable&) -> IDTable& = delete;
    auto operator=(IDTable&&) -> IDTable& = default;

    auto add(Value value) noexcept -> ID {
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("ID table exhausted its 32-bit identity space");
        }
        storage.push_back(std::move(value));
        return ID::from_index(static_cast<std::uint32_t>(storage.size() - 1));
    }

    template<typename... Arguments>
    auto emplace(Arguments&&... arguments) noexcept -> ID {
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("ID table exhausted its 32-bit identity space");
        }
        storage.emplace_back(std::forward<Arguments>(arguments)...);
        return ID::from_index(static_cast<std::uint32_t>(storage.size() - 1));
    }

    auto get(ID id) noexcept -> Value& {
        if (!contains(id)) {
            invariant_violation("ID table lookup used an invalid identity");
        }
        return storage[id.index()];
    }
    auto get(ID id) const noexcept -> const Value& {
        if (!contains(id)) {
            invariant_violation("ID table lookup used an invalid identity");
        }
        return storage[id.index()];
    }

    auto contains(ID id) const noexcept -> bool {
        return static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto try_get(ID id) noexcept -> Value* {
        return contains(id) ? std::addressof(storage[id.index()]) : nullptr;
    }

    auto try_get(ID id) const noexcept -> const Value* {
        return contains(id) ? std::addressof(storage[id.index()]) : nullptr;
    }

    auto values() noexcept -> std::span<Value> { return storage; }
    auto values() const noexcept -> std::span<const Value> { return storage; }
    auto size() const noexcept -> std::size_t { return storage.size(); }
    auto empty() const noexcept -> bool { return storage.empty(); }

    auto checkpoint() const noexcept -> Checkpoint { return storage.size(); }

    auto rewind(Checkpoint checkpoint) noexcept -> void {
        if (checkpoint <= storage.size()) {
            storage.erase(storage.begin() + static_cast<std::ptrdiff_t>(checkpoint), storage.end());
        }
    }

private:
    std::vector<Value> storage;
};
