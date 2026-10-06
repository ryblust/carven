#pragma once

#include <carven/runtime/string.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace carven::json {

struct Member;

// Number spelling is the authoritative value; conversions do not replace it.
struct Number final {
    runtime::String text;
};

// Storage operations require the kind and bounds checked by the Carven API.
class Storage final {
public:
    Storage() noexcept;
    // Define special members after Member; vector growth transfers tree storage.
    Storage(const Storage&);
    Storage(Storage&&) noexcept;
    ~Storage();
    auto operator=(const Storage&) -> Storage&;
    auto operator=(Storage&&) noexcept -> Storage&;

    static auto make_null() noexcept -> Storage;
    static auto make_boolean(bool value) noexcept -> Storage;
    // The caller establishes complete JSON number syntax.
    static auto make_number(std::string_view text) noexcept -> Storage;
    static auto make_integer(std::int64_t value) noexcept -> Storage;
    static auto make_unsigned_integer(std::uint64_t value) noexcept -> Storage;
    // The caller establishes a finite value.
    static auto make_real(double value) noexcept -> Storage;
    static auto make_string(runtime::String text) noexcept -> Storage;
    static auto make_array() noexcept -> Storage;
    static auto make_object() noexcept -> Storage;

    auto kind() const noexcept -> std::uint8_t;
    auto boolean_value() const noexcept -> bool;
    auto text() const noexcept -> const runtime::String&;
    auto size() const noexcept -> std::size_t;
    auto element(std::size_t index) const noexcept -> const Storage&;
    auto member_key(std::size_t index) const noexcept -> const runtime::String&;
    auto member_value(std::size_t index) const noexcept -> const Storage&;
    auto find_member(std::string_view key) const noexcept -> std::size_t;
    auto append_element(Storage value) noexcept -> void;
    auto replace_element(std::size_t index, Storage value) noexcept -> void;
    auto erase_element(std::size_t index) noexcept -> void;
    auto append_member(runtime::String key, Storage value) noexcept -> void;
    auto set_member(std::string_view key, Storage value) noexcept -> void;
    auto erase_key(std::string_view key) noexcept -> std::size_t;
    auto to_i64() const noexcept -> std::optional<std::int64_t>;
    auto to_u64() const noexcept -> std::optional<std::uint64_t>;
    auto to_f64() const noexcept -> std::optional<double>;

private:
    using Array = std::vector<Storage>;
    using Object = std::vector<Member>;
    using Data = std::variant<std::monostate, bool, Number, runtime::String, Array, Object>;

    explicit Storage(Data value) noexcept;

    template<typename T>
    auto convert_number() const noexcept -> std::optional<T> {
        const auto input = std::get<Number>(data).text.as_str();
        auto value = T {};
        const auto result = [&]() noexcept {
            if constexpr (std::is_floating_point_v<T>) {
                return std::from_chars(
                    input.data(),
                    input.data() + input.size(),
                    value,
                    std::chars_format::general
                );
            } else {
                return std::from_chars(input.data(), input.data() + input.size(), value);
            }
        }();
        if (result.ec != std::errc {} || result.ptr != input.data() + input.size()) {
            return std::nullopt;
        }
        return value;
    }

    template<typename T>
    static auto format_number(T value) noexcept -> Storage {
        // 64 bytes cover every i64/u64 decimal or finite f64 shortest spelling.
        auto bytes = std::array<char, 64> {};
        const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
        if (result.ec != std::errc {}) {
            std::terminate();
        }
        return make_number(std::string_view(bytes.data(), result.ptr - bytes.data()));
    }

    Data data;
};

struct Member final {
    runtime::String key;
    Storage value;
};

using StorageReference = std::reference_wrapper<const Storage>;

inline Storage::Storage(const Storage&) = default;
inline Storage::Storage(Storage&&) noexcept = default;
inline Storage::~Storage() = default;
inline auto Storage::operator=(const Storage&) -> Storage& = default;
inline auto Storage::operator=(Storage&&) noexcept -> Storage& = default;

inline Storage::Storage() noexcept
    : data(std::monostate {}) {}

inline Storage::Storage(Data value) noexcept
    : data(std::move(value)) {}

inline auto Storage::make_null() noexcept -> Storage {
    return Storage();
}

inline auto Storage::make_boolean(bool value) noexcept -> Storage {
    return Storage(value);
}

inline auto Storage::make_number(std::string_view text) noexcept -> Storage {
    return Storage(Number {.text = runtime::String::from_str(text)});
}

inline auto Storage::make_integer(std::int64_t value) noexcept -> Storage {
    return format_number(value);
}

inline auto Storage::make_unsigned_integer(std::uint64_t value) noexcept -> Storage {
    return format_number(value);
}

inline auto Storage::make_real(double value) noexcept -> Storage {
    return format_number(value);
}

inline auto Storage::make_string(runtime::String text) noexcept -> Storage {
    return Storage(std::move(text));
}

inline auto Storage::make_array() noexcept -> Storage {
    return Storage(Array {});
}

inline auto Storage::make_object() noexcept -> Storage {
    return Storage(Object {});
}

inline auto Storage::kind() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(data.index());
}

inline auto Storage::boolean_value() const noexcept -> bool {
    return std::get<bool>(data);
}

inline auto Storage::text() const noexcept -> const runtime::String& {
    if (const auto* number = std::get_if<Number>(&data)) {
        return number->text;
    }
    return std::get<runtime::String>(data);
}

inline auto Storage::size() const noexcept -> std::size_t {
    if (const auto* array = std::get_if<Array>(&data)) {
        return array->size();
    }
    return std::get<Object>(data).size();
}

inline auto Storage::element(std::size_t index) const noexcept -> const Storage& {
    return std::get<Array>(data)[index];
}

inline auto Storage::member_key(std::size_t index) const noexcept -> const runtime::String& {
    return std::get<Object>(data)[index].key;
}

inline auto Storage::member_value(std::size_t index) const noexcept -> const Storage& {
    return std::get<Object>(data)[index].value;
}

inline auto Storage::find_member(std::string_view key) const noexcept -> std::size_t {
    const auto& object = std::get<Object>(data);
    for (auto index = object.size(); index != 0; --index) {
        if (object[index - 1].key.as_str() == key) {
            return index - 1;
        }
    }
    return object.size();
}

inline auto Storage::append_element(Storage value) noexcept -> void {
    std::get<Array>(data).push_back(std::move(value));
}

inline auto Storage::replace_element(std::size_t index, Storage value) noexcept -> void {
    std::get<Array>(data)[index] = std::move(value);
}

inline auto Storage::erase_element(std::size_t index) noexcept -> void {
    auto& array = std::get<Array>(data);
    array.erase(array.begin() + static_cast<Array::difference_type>(index));
}

inline auto Storage::append_member(runtime::String key, Storage value) noexcept -> void {
    std::get<Object>(data).push_back(Member {.key = std::move(key), .value = std::move(value)});
}

inline auto Storage::set_member(std::string_view key, Storage value) noexcept -> void {
    auto& object = std::get<Object>(data);
    const auto index = find_member(key);
    if (index == object.size()) {
        append_member(runtime::String::from_str(key), std::move(value));
    } else {
        object[index].value = std::move(value);
    }
}

inline auto Storage::erase_key(std::string_view key) noexcept -> std::size_t {
    return std::erase_if(std::get<Object>(data), [&](const Member& member) noexcept {
        return member.key.as_str() == key;
    });
}

inline auto Storage::to_i64() const noexcept -> std::optional<std::int64_t> {
    return convert_number<std::int64_t>();
}

inline auto Storage::to_u64() const noexcept -> std::optional<std::uint64_t> {
    return convert_number<std::uint64_t>();
}

inline auto Storage::to_f64() const noexcept -> std::optional<double> {
    return convert_number<double>();
}

} // namespace carven::json
