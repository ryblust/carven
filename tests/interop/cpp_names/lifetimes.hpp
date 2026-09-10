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

inline auto observe_owner(const Owner&, std::int32_t) noexcept -> void {
    mark(9);
}

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

struct PatternOwner final {
    std::int32_t id;

    explicit PatternOwner(std::int32_t value) noexcept
        : id(value) {
        mark(id);
    }

    PatternOwner(const PatternOwner& other) noexcept
        : id(other.id) {
        mark(100 + id);
    }

    PatternOwner(PatternOwner&& other) noexcept
        : id(other.id) {
        mark(200 + id);
    }

    auto operator=(const PatternOwner&) -> PatternOwner& = delete;
    auto operator=(PatternOwner&&) -> PatternOwner& = delete;

    ~PatternOwner() { mark(-id); }
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

struct BorrowedReceiver final {
    BorrowedReceiver() = default;
    BorrowedReceiver(const BorrowedReceiver&) = delete;
    BorrowedReceiver(BorrowedReceiver&&) = delete;
    auto operator=(const BorrowedReceiver&) -> BorrowedReceiver& = delete;
    auto operator=(BorrowedReceiver&&) -> BorrowedReceiver& = delete;

    ~BorrowedReceiver() { mark(-6); }

    auto probe(std::int32_t) noexcept -> bool {
        mark(6);
        return true;
    }

    auto probe(std::int32_t) const noexcept -> bool {
        mark(9);
        return true;
    }
};

inline auto borrowed_receiver = BorrowedReceiver {};

inline auto receiver_reference() noexcept -> BorrowedReceiver& {
    mark(1);
    return borrowed_receiver;
}

inline auto receiver_const_reference() noexcept -> const BorrowedReceiver& {
    mark(1);
    return borrowed_receiver;
}

inline auto receiver_from_literal(const std::int32_t&) noexcept -> const BorrowedReceiver& {
    mark(3);
    return borrowed_receiver;
}

inline auto receiver_from_literal(std::int32_t&&) noexcept -> BorrowedReceiver {
    mark(4);
    return BorrowedReceiver {};
}

inline auto receiver_from_literal(const std::int32_t&, const std::int32_t&) noexcept
    -> const BorrowedReceiver& {
    mark(3);
    return borrowed_receiver;
}

inline auto receiver_from_literal(std::int32_t&&, const std::int32_t&) noexcept
    -> BorrowedReceiver {
    mark(4);
    return BorrowedReceiver {};
}

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

struct ArraySource final {
    std::int32_t id;

    explicit ArraySource(std::int32_t value) noexcept
        : id(value) {}

    ArraySource(const ArraySource& other) noexcept
        : id(other.id) {}

    ArraySource(ArraySource&& other) noexcept
        : id(other.id) {
        other.id = 0;
    }

    auto operator=(const ArraySource&) -> ArraySource& = delete;
    auto operator=(ArraySource&&) -> ArraySource& = delete;
    ~ArraySource() = default;
};

inline auto first_array_source = ArraySource {1};
inline auto second_array_source = ArraySource {2};
inline auto array_sources_preserved = true;

inline auto reset_array_sources() noexcept -> void {
    reset();
    first_array_source.id = 1;
    second_array_source.id = 2;
    array_sources_preserved = true;
}

inline auto array_source(std::int32_t index) noexcept -> ArraySource& {
    mark(10 + index);
    return index == 1 ? first_array_source : second_array_source;
}

inline auto const_array_source(std::int32_t index) noexcept -> const ArraySource& {
    return array_source(index);
}

inline auto mutate_array_source() noexcept -> void {
    mark(3);
    array_sources_preserved = first_array_source.id == 1 && second_array_source.id == 2;
    first_array_source.id = 9;
}

inline auto array_sources_intact() noexcept -> bool {
    return array_sources_preserved && first_array_source.id == 9 && second_array_source.id == 2;
}

inline auto native_take_copies = std::int32_t {0};
inline auto native_take_pointee = std::int32_t {1};

struct NativeTakeOwner final {
    NativeTakeOwner() = default;

    NativeTakeOwner(const NativeTakeOwner&) noexcept { ++native_take_copies; }

    NativeTakeOwner(NativeTakeOwner&&) noexcept = default;
    auto operator=(const NativeTakeOwner&) -> NativeTakeOwner& = delete;
    auto operator=(NativeTakeOwner&&) -> NativeTakeOwner& = delete;
    ~NativeTakeOwner() = default;
};

inline auto reset_native_take() noexcept -> void {
    reset();
    native_take_copies = 0;
}

inline auto native_take_copy_count() noexcept -> std::int32_t {
    return native_take_copies;
}

inline auto native_take_address() noexcept -> std::int32_t* {
    return &native_take_pointee;
}

inline auto choose_native_take(const std::int32_t&, std::int32_t = 0) noexcept -> std::int32_t {
    mark(5);
    return 1;
}

inline auto choose_native_take(std::int32_t&&, std::int32_t = 0) noexcept -> std::int32_t {
    mark(6);
    return 2;
}

inline auto choose_native_take(std::int32_t* const&, std::int32_t = 0) noexcept -> std::int32_t {
    mark(5);
    return 1;
}

inline auto choose_native_take(std::int32_t*&&, std::int32_t = 0) noexcept -> std::int32_t {
    mark(6);
    return 2;
}

inline auto choose_native_take(const NativeTakeOwner&, std::int32_t = 0) noexcept -> std::int32_t {
    mark(5);
    return 1;
}

inline auto choose_native_take(NativeTakeOwner&&, std::int32_t = 0) noexcept -> std::int32_t {
    mark(6);
    return 2;
}

} // namespace lifetime_probe
