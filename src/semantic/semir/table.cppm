module carven:semantic.semir.table;

import :semantic.semir.identity;
import :semantic.semir.ids;
import :support.invariant;
import std;

class ProgramDraft;

template<typename ID, typename Value>
struct IDTableEntry final {
    ID id;
    const Value& value;
};

template<typename ID, typename Value, typename Identity>
class IDTableEntries final {
public:
    class Iterator final {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = IDTableEntry<ID, Value>;

        Iterator() = default;

        auto operator*() const noexcept -> value_type {
            return {
                .id = IDTableEntries::make_id(*owner, static_cast<std::uint32_t>(position)),
                .value = (*storage)[position],
            };
        }

        auto operator++() noexcept -> Iterator& {
            ++position;
            return *this;
        }

        auto operator++(int) noexcept -> Iterator {
            auto previous = *this;
            ++*this;
            return previous;
        }

        auto operator==(const Iterator&) const noexcept -> bool = default;

    private:
        Iterator(Identity identity, const std::vector<Value>& values, std::size_t index) noexcept
            : owner(identity),
              storage(std::addressof(values)),
              position(index) {}

        std::optional<Identity> owner;
        const std::vector<Value>* storage = nullptr;
        std::size_t position = 0uz;

        friend class IDTableEntries;
    };

    auto begin() const noexcept -> Iterator { return Iterator(owner, *storage, 0uz); }

    auto end() const noexcept -> Iterator { return Iterator(owner, *storage, storage->size()); }

    auto size() const noexcept -> std::size_t { return storage->size(); }

private:
    static auto make_id(Identity identity, std::uint32_t index) noexcept -> ID {
        return ID(identity, index);
    }

    IDTableEntries(Identity identity, const std::vector<Value>& values) noexcept
        : owner(identity),
          storage(std::addressof(values)) {}

    Identity owner;
    const std::vector<Value>* storage;

    template<typename StoredValue, typename StoredID>
    friend class ImmutableProgramTable;
    template<typename StoredValue, typename StoredID>
    friend class ImmutableBodyTable;
    template<typename StoredValue, typename StoredID>
    friend class MutableBodyTable;
};

template<typename Value, typename ID>
class ImmutableProgramTable final {
public:
    ImmutableProgramTable(const ImmutableProgramTable&) = delete;
    ImmutableProgramTable(ImmutableProgramTable&&) noexcept = default;
    ~ImmutableProgramTable() = default;
    auto operator=(const ImmutableProgramTable&) -> ImmutableProgramTable& = delete;
    auto operator=(ImmutableProgramTable&&) -> ImmutableProgramTable& = delete;

    auto owner() const noexcept -> ProgramIdentity { return program_identity; }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto empty() const noexcept -> bool { return storage.empty(); }

    auto contains(ID id) const noexcept -> bool {
        return id.owner() == program_identity
            && static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto get(ID id) const noexcept -> const Value& {
        if (!contains(id)) {
            invariant_violation(
                "immutable program table lookup used a foreign or invalid identity"
            );
        }
        return storage[id.index()];
    }

    auto entries() const noexcept -> IDTableEntries<ID, Value, ProgramIdentity> {
        return IDTableEntries<ID, Value, ProgramIdentity>(program_identity, storage);
    }

private:
    ImmutableProgramTable(ProgramIdentity identity, std::vector<Value> values) noexcept
        : program_identity(identity),
          storage(std::move(values)) {}

    ProgramIdentity program_identity;
    std::vector<Value> storage;

    template<typename StoredValue, typename StoredID>
    friend class MutableProgramTable;
    template<typename StoredValue, typename StoredID>
    friend class ReservedProgramTable;
};

template<typename Value, typename ID>
class MutableProgramTable final {
public:
    explicit MutableProgramTable(ProgramIdentity owner) noexcept
        : program_identity(owner) {}

    MutableProgramTable(const MutableProgramTable&) = delete;
    MutableProgramTable(MutableProgramTable&&) noexcept = default;
    ~MutableProgramTable() = default;
    auto operator=(const MutableProgramTable&) -> MutableProgramTable& = delete;
    auto operator=(MutableProgramTable&&) -> MutableProgramTable& = delete;

    auto owner() const noexcept -> ProgramIdentity { return program_identity; }

    auto add(Value value) noexcept -> ID {
        ensure_capacity();
        const auto id = ID(program_identity, static_cast<std::uint32_t>(storage.size()));
        storage.push_back(std::move(value));
        return id;
    }

    auto intern(Value value) noexcept -> ID
        requires std::equality_comparable<Value>
    {
        for (auto index = 0uz; index < storage.size(); ++index) {
            if (storage[index] == value) {
                return ID(program_identity, static_cast<std::uint32_t>(index));
            }
        }
        return add(std::move(value));
    }

    auto contains(ID id) const noexcept -> bool {
        return id.owner() == program_identity
            && static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto copy(ID id) const noexcept -> Value
        requires std::copy_constructible<Value>
    {
        require_valid(id);
        return storage[id.index()];
    }

    auto replace(ID id, Value value) noexcept -> void {
        require_valid(id);
        storage[id.index()] = std::move(value);
    }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto seal() && noexcept -> ImmutableProgramTable<Value, ID> {
        return ImmutableProgramTable<Value, ID>(program_identity, std::move(storage));
    }

private:
    auto ensure_capacity() const noexcept -> void {
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("program table exhausted its 32-bit identity space");
        }
    }

    auto require_valid(ID id) const noexcept -> void {
        if (!contains(id)) {
            invariant_violation("mutable program table lookup used a foreign or invalid identity");
        }
    }

    ProgramIdentity program_identity;
    std::vector<Value> storage;
};

template<typename Value, typename ID>
class ReservedProgramTable final {
public:
    explicit ReservedProgramTable(ProgramIdentity owner) noexcept
        : program_identity(owner) {}

    ReservedProgramTable(const ReservedProgramTable&) = delete;
    ReservedProgramTable(ReservedProgramTable&&) noexcept = default;
    ~ReservedProgramTable() = default;
    auto operator=(const ReservedProgramTable&) -> ReservedProgramTable& = delete;
    auto operator=(ReservedProgramTable&&) -> ReservedProgramTable& = delete;

    auto owner() const noexcept -> ProgramIdentity { return program_identity; }

    auto reserve() noexcept -> ID {
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("reserved program table exhausted its 32-bit identity space");
        }
        const auto id = ID(program_identity, static_cast<std::uint32_t>(storage.size()));
        storage.emplace_back();
        return id;
    }

    auto define(ID id, Value value) noexcept -> void {
        require_valid(id);
        auto& slot = storage[id.index()];
        if (slot.has_value()) {
            invariant_violation("reserved program identity was defined more than once");
        }
        slot.emplace(std::move(value));
    }

    auto is_defined(ID id) const noexcept -> bool {
        return contains(id) && storage[id.index()].has_value();
    }

    auto all_defined() const noexcept -> bool {
        return std::ranges::all_of(storage, [](const auto& slot) static noexcept {
            return slot.has_value();
        });
    }

    auto copy_defined(ID id) const noexcept -> Value
        requires std::copy_constructible<Value>
    {
        require_defined(id);
        return *storage[id.index()];
    }

    auto ids() const noexcept -> std::vector<ID> {
        if (!all_defined()) {
            invariant_violation(
                "reserved program table identities were observed before definition"
            );
        }
        auto result = std::vector<ID>();
        result.reserve(storage.size());
        for (auto index = 0uz; index < storage.size(); ++index) {
            result.push_back(ID(program_identity, static_cast<std::uint32_t>(index)));
        }
        return result;
    }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto seal() && noexcept -> ImmutableProgramTable<Value, ID> {
        auto values = std::vector<Value>();
        values.reserve(storage.size());
        for (auto& slot : storage) {
            if (!slot.has_value()) {
                invariant_violation("reserved program table was sealed with an undefined identity");
            }
            values.push_back(std::move(*slot));
        }

        return ImmutableProgramTable<Value, ID>(program_identity, std::move(values));
    }

private:
    auto contains(ID id) const noexcept -> bool {
        return id.owner() == program_identity
            && static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto require_valid(ID id) const noexcept -> void {
        if (!contains(id)) {
            invariant_violation("reserved program table used a foreign or invalid identity");
        }
    }

    auto require_defined(ID id) const noexcept -> void {
        require_valid(id);
        if (!storage[id.index()].has_value()) {
            invariant_violation("reserved program table lookup used an undefined identity");
        }
    }

    ProgramIdentity program_identity;
    std::vector<std::optional<Value>> storage;

    friend class ProgramDraft;
};

template<typename Value, typename ID>
class ImmutableBodyTable final {
public:
    ImmutableBodyTable(const ImmutableBodyTable&) = delete;
    ImmutableBodyTable(ImmutableBodyTable&&) noexcept = default;
    ~ImmutableBodyTable() = default;
    auto operator=(const ImmutableBodyTable&) -> ImmutableBodyTable& = delete;
    auto operator=(ImmutableBodyTable&&) -> ImmutableBodyTable& = delete;

    auto owner() const noexcept -> BodyIdentity { return body_identity; }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto empty() const noexcept -> bool { return storage.empty(); }

    auto contains(ID id) const noexcept -> bool {
        return id.owner() == body_identity && static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto get(ID id) const noexcept -> const Value& {
        if (!contains(id)) {
            invariant_violation("immutable body table lookup used a foreign or invalid identity");
        }
        return storage[id.index()];
    }

    auto entries() const noexcept -> IDTableEntries<ID, Value, BodyIdentity> {
        return IDTableEntries<ID, Value, BodyIdentity>(body_identity, storage);
    }

    template<typename Result, typename Mapper>
    auto transform(Mapper mapper) && noexcept -> MutableBodyTable<Result, ID> {
        auto result = MutableBodyTable<Result, ID>(body_identity);
        for (auto index = 0uz; index < storage.size(); ++index) {
            const auto id = ID(body_identity, static_cast<std::uint32_t>(index));
            const auto mapped_id = result.add(std::invoke(mapper, id, std::move(storage[index])));
            if (mapped_id != id) {
                invariant_violation("body table transform changed a stable identity");
            }
        }
        storage.clear();

        return result;
    }

private:
    ImmutableBodyTable(BodyIdentity identity, std::vector<Value> values) noexcept
        : body_identity(identity),
          storage(std::move(values)) {}

    BodyIdentity body_identity;
    std::vector<Value> storage;

    template<typename StoredValue, typename StoredID>
    friend class MutableBodyTable;
};

template<typename Value, typename ID>
class MutableBodyTable final {
public:
    explicit MutableBodyTable(BodyIdentity owner) noexcept
        : body_identity(owner) {}

    MutableBodyTable(const MutableBodyTable&) = delete;
    MutableBodyTable(MutableBodyTable&&) noexcept = default;
    ~MutableBodyTable() = default;
    auto operator=(const MutableBodyTable&) -> MutableBodyTable& = delete;
    auto operator=(MutableBodyTable&&) -> MutableBodyTable& = delete;

    auto owner() const noexcept -> BodyIdentity { return body_identity; }

    auto add(Value value) noexcept -> ID {
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("body table exhausted its 32-bit identity space");
        }
        const auto id = ID(body_identity, static_cast<std::uint32_t>(storage.size()));
        storage.push_back(std::move(value));
        return id;
    }

    auto contains(ID id) const noexcept -> bool {
        return id.owner() == body_identity && static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto copy(ID id) const noexcept -> Value
        requires std::copy_constructible<Value>
    {
        require_valid(id);
        return storage[id.index()];
    }

    auto replace(ID id, Value value) noexcept -> void {
        require_valid(id);
        storage[id.index()] = std::move(value);
    }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto entries() const noexcept -> IDTableEntries<ID, Value, BodyIdentity> {
        return IDTableEntries<ID, Value, BodyIdentity>(body_identity, storage);
    }

    auto seal() && noexcept -> ImmutableBodyTable<Value, ID> {
        return ImmutableBodyTable<Value, ID>(body_identity, std::move(storage));
    }

private:
    auto require_valid(ID id) const noexcept -> void {
        if (!contains(id)) {
            invariant_violation("mutable body table lookup used a foreign or invalid identity");
        }
    }

    BodyIdentity body_identity;
    std::vector<Value> storage;
};
