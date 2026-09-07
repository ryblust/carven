#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lifetime_probe {

inline auto events = std::string {};

inline auto mark(std::int32_t event) noexcept -> void {
    events += std::to_string(event) + " ";
}

inline auto reset() noexcept -> void {
    events.clear();
}

inline auto matches(std::string_view expected) noexcept -> bool {
    return events == expected;
}

struct Owner final {
    std::int32_t id;

    explicit Owner(std::int32_t id) noexcept
        : id(id) {
        mark(id);
    }

    Owner(const Owner&) = delete;

    Owner(Owner&& other) noexcept
        : id(other.id) {
        mark(100 + id);
    }

    auto operator=(const Owner&) -> Owner& = delete;
    auto operator=(Owner&&) -> Owner& = delete;

    ~Owner() { mark(-id); }

    auto probe() const noexcept -> bool {
        mark(10 + id);
        return true;
    }
};

inline auto consume_owner(Owner&&, std::int32_t) noexcept -> void {
    mark(9);
}

inline auto event(std::int32_t value) noexcept -> std::int32_t {
    mark(value);
    return value;
}

struct FixedOwner final {
    std::int32_t id;

    explicit FixedOwner(std::int32_t id) noexcept
        : id(id) {
        mark(id);
    }

    template<typename Value>
    explicit FixedOwner(Value&&) noexcept
        : id(99) {
        mark(id);
    }

    FixedOwner(const FixedOwner&) = delete;
    FixedOwner(FixedOwner&&) = delete;

    ~FixedOwner() { mark(-id); }

    auto probe(std::int32_t, std::int32_t) const noexcept -> bool {
        mark(9);
        return true;
    }
};

struct CopyOwner final {
    int id;

    explicit CopyOwner(int value) noexcept
        : id(value) {
        mark(id);
    }

    CopyOwner(const CopyOwner& other) noexcept
        : id(other.id) {
        mark(100 + id);
    }

    template<typename Value>
    explicit CopyOwner(Value&&) noexcept
        : id(99) {
        mark(id);
    }

    ~CopyOwner() { mark(-id); }
};

struct ResultMaker final {
    std::int32_t id;

    explicit ResultMaker(std::int32_t id) noexcept
        : id(id) {
        mark(id);
    }

    ResultMaker(const ResultMaker&) = delete;
    ResultMaker(ResultMaker&&) = delete;

    ~ResultMaker() { mark(-id); }

    auto make(std::int32_t value) const noexcept -> FixedOwner { return FixedOwner {value}; }
};

} // namespace lifetime_probe
