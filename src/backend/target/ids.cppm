module carven:backend.target.ids;

import :support.invariant;
import std;

class TargetPlan;
class PlannedCompilation;
class TargetTestingFixture;
class TargetUnitBuilder;

class TargetPlanIdentity final {
public:
    constexpr auto value() const noexcept -> std::uint64_t { return identity_value; }

    constexpr auto operator<=>(const TargetPlanIdentity&) const noexcept = default;

private:
    explicit constexpr TargetPlanIdentity(std::uint64_t value) noexcept
        : identity_value(value) {}

    static auto fresh() noexcept -> TargetPlanIdentity;

    std::uint64_t identity_value;

    friend class TargetPlan;
    friend class TargetTestingFixture;
};

template<typename Value, typename ID>
class TargetPlanTable;
template<typename Value, typename ID>
class TargetPlanTableBuilder;

template<typename ID, typename Value>
struct TargetPlanTableEntry final {
    ID id;
    const Value& value;
};

template<typename Value, typename ID>
class TargetPlanTableEntries final {
public:
    class Iterator final {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = TargetPlanTableEntry<ID, Value>;

        Iterator() = default;

        auto operator*() const noexcept -> value_type {
            return {
                .id = TargetPlanTableEntries::make_id(*owner, position),
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
        Iterator(
            TargetPlanIdentity identity,
            const std::vector<Value>& values,
            std::size_t index
        ) noexcept
            : owner(identity),
              storage(std::addressof(values)),
              position(index) {}

        std::optional<TargetPlanIdentity> owner;
        const std::vector<Value>* storage = nullptr;
        std::size_t position = 0uz;

        friend class TargetPlanTableEntries;
    };

    auto begin() const noexcept -> Iterator { return Iterator(owner, *storage, 0uz); }

    auto end() const noexcept -> Iterator { return Iterator(owner, *storage, storage->size()); }

private:
    static auto make_id(TargetPlanIdentity identity, std::size_t index) noexcept -> ID {
        return ID(identity, static_cast<std::uint32_t>(index));
    }

    TargetPlanTableEntries(TargetPlanIdentity identity, const std::vector<Value>& values) noexcept
        : owner(identity),
          storage(std::addressof(values)) {}

    TargetPlanIdentity owner;
    const std::vector<Value>* storage;

    friend class TargetPlanTable<Value, ID>;
};

template<typename Tag>
class TargetPlanID final {
public:
    constexpr auto owner() const noexcept -> TargetPlanIdentity { return plan_identity; }

    constexpr auto index() const noexcept -> std::uint32_t { return row_index; }

    constexpr auto operator<=>(const TargetPlanID&) const noexcept = default;

private:
    explicit constexpr TargetPlanID(TargetPlanIdentity owner, std::uint32_t index) noexcept
        : plan_identity(owner),
          row_index(index) {}

    TargetPlanIdentity plan_identity;
    std::uint32_t row_index;

    template<typename Value, typename ID>
    friend class TargetPlanTable;
    template<typename Value, typename ID>
    friend class TargetPlanTableBuilder;
    template<typename Value, typename ID>
    friend class TargetPlanTableEntries;
    friend class TargetTestingFixture;
};

struct TargetArtifactIDTag final {};

using TargetArtifactID = TargetPlanID<TargetArtifactIDTag>;

class TargetUnitIdentity final {
public:
    constexpr auto operator<=>(const TargetUnitIdentity&) const noexcept = default;

private:
    explicit constexpr TargetUnitIdentity(std::uint64_t value) noexcept
        : identity_value(value) {}

    static auto fresh() noexcept -> TargetUnitIdentity;

    std::uint64_t identity_value;

    friend class TargetUnitBuilder;
    friend class TargetTestingFixture;
};

template<typename Tag>
class TargetUnitID final {
public:
    constexpr auto owner() const noexcept -> TargetUnitIdentity { return unit_identity; }

    constexpr auto index() const noexcept -> std::uint32_t { return row_index; }

    constexpr auto operator<=>(const TargetUnitID&) const noexcept = default;

private:
    explicit constexpr TargetUnitID(TargetUnitIdentity owner, std::uint32_t index) noexcept
        : unit_identity(owner),
          row_index(index) {}

    TargetUnitIdentity unit_identity;
    std::uint32_t row_index;

    friend class TargetUnitBuilder;
    friend class TargetTestingFixture;
};

struct TargetTypeIDTag final {};

using TargetTypeID = TargetUnitID<TargetTypeIDTag>;

template<typename Value, typename ID>
class TargetPlanTable final {
public:
    TargetPlanTable(const TargetPlanTable&) = delete;
    TargetPlanTable(TargetPlanTable&&) = default;
    ~TargetPlanTable() = default;

    auto operator=(const TargetPlanTable&) -> TargetPlanTable& = delete;
    auto operator=(TargetPlanTable&&) -> TargetPlanTable& = default;

    auto owner() const noexcept -> TargetPlanIdentity { return plan_identity; }

    auto size() const noexcept -> std::size_t { return storage.size(); }

    auto empty() const noexcept -> bool { return storage.empty(); }

    auto contains(ID id) const noexcept -> bool {
        return id.owner() == plan_identity && static_cast<std::size_t>(id.index()) < storage.size();
    }

    auto get(ID id) const noexcept -> const Value& {
        if (!contains(id)) {
            invariant_violation("target plan table lookup used a foreign or invalid identity");
        }
        return storage[id.index()];
    }

    auto entries() const noexcept -> TargetPlanTableEntries<Value, ID> {
        return TargetPlanTableEntries<Value, ID>(plan_identity, storage);
    }

private:
    TargetPlanTable(TargetPlanIdentity identity, std::vector<Value> values) noexcept
        : plan_identity(identity),
          storage(std::move(values)) {}

    TargetPlanIdentity plan_identity;
    std::vector<Value> storage;

    friend class TargetPlanTableBuilder<Value, ID>;
};

template<typename Value, typename ID>
class TargetPlanTableBuilder final {
public:
    explicit TargetPlanTableBuilder(TargetPlanIdentity identity) noexcept
        : plan_identity(identity) {}

    TargetPlanTableBuilder(const TargetPlanTableBuilder&) = delete;

    TargetPlanTableBuilder(TargetPlanTableBuilder&& other) noexcept
        : plan_identity(std::exchange(other.plan_identity, std::nullopt)),
          storage(std::move(other.storage)) {}

    ~TargetPlanTableBuilder() = default;

    auto operator=(const TargetPlanTableBuilder&) -> TargetPlanTableBuilder& = delete;
    auto operator=(TargetPlanTableBuilder&&) -> TargetPlanTableBuilder& = delete;

    auto size() const noexcept -> std::size_t {
        static_cast<void>(require_identity());
        return storage.size();
    }

    auto reserve(std::size_t size) noexcept -> void {
        static_cast<void>(require_identity());
        storage.reserve(size);
    }

    auto add(Value value) noexcept -> ID {
        const auto identity = require_identity();
        if (storage.size() == std::numeric_limits<std::uint32_t>::max()) {
            resource_limit_exceeded("target plan table exhausted its 32-bit identity space");
        }
        const auto id = ID(identity, static_cast<std::uint32_t>(storage.size()));
        storage.push_back(std::move(value));
        return id;
    }

    auto seal() && noexcept -> TargetPlanTable<Value, ID> {
        const auto identity = require_identity();
        plan_identity.reset();
        return TargetPlanTable<Value, ID>(identity, std::move(storage));
    }

private:
    auto require_identity() const noexcept -> TargetPlanIdentity {
        if (!plan_identity.has_value()) {
            invariant_violation("target plan table builder was used after move or seal");
        }
        return *plan_identity;
    }

    std::optional<TargetPlanIdentity> plan_identity;
    std::vector<Value> storage;
};
