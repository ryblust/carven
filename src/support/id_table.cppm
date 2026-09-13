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

    auto get(this auto&& self, ID value_id) noexcept -> decltype(auto)
        requires std::is_lvalue_reference_v<decltype(self)>
    {
        if (!self.contains(value_id)) {
            invariant_violation("ID table lookup used an invalid identity");
        }
        return (self.storage[value_id.index()]);
    }

    auto contains(ID id) const noexcept -> bool {
        return static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto try_get(this auto&& self, ID value_id) noexcept -> auto*
        requires std::is_lvalue_reference_v<decltype(self)>
    {
        return self.contains(value_id) ? std::addressof(self.storage[value_id.index()]) : nullptr;
    }

    auto values(this auto&& self) noexcept -> auto
        requires std::is_lvalue_reference_v<decltype(self)>
    {
        return std::span {self.storage};
    }

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

template<typename Value, typename ID>
class ReservedTable final {
public:
    ReservedTable() = default;
    ReservedTable(const ReservedTable&) = delete;
    ReservedTable(ReservedTable&&) = default;
    ~ReservedTable() = default;
    auto operator=(const ReservedTable&) -> ReservedTable& = delete;
    auto operator=(ReservedTable&&) -> ReservedTable& = default;

    auto reserve() noexcept -> ID {
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("reserved table exhausted its 32-bit identity space");
        }
        storage.emplace_back();
        return ID::from_index(static_cast<std::uint32_t>(storage.size() - 1));
    }

    auto define(ID id, Value value) noexcept -> void {
        if (!contains(id)) {
            invariant_violation("reserved table definition used an invalid identity");
        }
        auto& slot = storage[id.index()];
        if (slot.has_value()) {
            invariant_violation("reserved table identity was defined more than once");
        }
        slot.emplace(std::move(value));
    }

    auto get_defined(this auto&& self, ID id) noexcept -> decltype(auto)
        requires std::is_lvalue_reference_v<decltype(self)>
    {
        if (!self.contains(id) || !self.storage[id.index()].has_value()) {
            invariant_violation("reserved table lookup used an undefined identity");
        }
        return (*self.storage[id.index()]);
    }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto empty() const noexcept -> bool { return storage.empty(); }

    auto is_defined(ID id) const noexcept -> bool {
        return contains(id) && storage[id.index()].has_value();
    }

    auto seal() && noexcept -> IDTable<Value, ID> {
        auto result = IDTable<Value, ID>();
        for (auto& slot : storage) {
            if (!slot.has_value()) {
                invariant_violation("reserved table was sealed with an undefined identity");
            }
            static_cast<void>(result.add(std::move(*slot)));
        }
        storage.clear();
        return result;
    }

private:
    auto contains(ID id) const noexcept -> bool {
        return static_cast<std::size_t>(id.index()) < storage.size();
    }

    std::vector<std::optional<Value>> storage;
};
