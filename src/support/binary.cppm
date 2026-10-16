module carven:support.binary;

import :support.tree_value;
import :support.unique_indirect;
import std;

struct BinaryError final {
    std::string message;
    std::size_t offset;
};

struct BinaryLimits final {
    std::size_t bytes;
    std::size_t elements;
    std::size_t depth;
};

inline constexpr auto compilation_binary_limits = BinaryLimits {
    .bytes = 1024uz * 1024 * 1024,
    .elements = 16uz * 1024 * 1024,
    .depth = 1024,
};

template<typename T>
struct BinaryCodec;

class BinaryWriter final {
public:
    BinaryWriter() noexcept;

    template<typename T, typename Context>
    auto write(const T& value, Context& context) noexcept -> void;

    auto unsigned_value(std::uint64_t value, std::size_t width) noexcept -> void;
    auto append(std::span<const std::byte> bytes) noexcept -> void;
    auto fail(std::string message) noexcept -> void;
    auto error() const noexcept -> const std::optional<BinaryError>&;
    auto take() && noexcept -> std::vector<std::byte>;

private:
    std::vector<std::byte> storage;
    std::size_t depth;
    std::optional<BinaryError> failure;
};

class BinaryReader final {
public:
    explicit BinaryReader(std::span<const std::byte> bytes) noexcept;

    template<typename T, typename Context>
    auto read(Context& context) noexcept -> std::expected<T, BinaryError>;

    auto unsigned_value(std::size_t width) noexcept -> std::expected<std::uint64_t, BinaryError>;
    auto take(std::size_t count) noexcept -> std::expected<std::span<const std::byte>, BinaryError>;
    auto count(std::size_t element_size = 1) noexcept -> std::expected<std::size_t, BinaryError>;
    auto fail(std::string message) const noexcept -> std::unexpected<BinaryError>;
    auto remaining() const noexcept -> std::size_t;
    auto finish() const noexcept -> std::expected<void, BinaryError>;

private:
    std::span<const std::byte> storage;
    std::size_t offset;
    std::size_t elements_remaining;
    std::size_t allocation_remaining;
    std::size_t depth;
};

// Opaque owners specialize BinaryCodec in their owning subsystem. Aggregate
// records retain their actual field types; this traversal creates no wire IR.
template<typename T>
auto binary_aggregate_fields(T& value) noexcept {
    auto& [... fields] = value;
    return std::tie(fields...);
}

template<typename T, typename Tuple, std::size_t... Indices>
auto binary_construct_aggregate(Tuple& fields, std::index_sequence<Indices...>) noexcept -> T {
    return T {std::move(*std::get<Indices>(fields))...};
}

template<typename Tuple, std::size_t... Indices>
auto binary_optional_fields(std::index_sequence<Indices...>) noexcept
    -> std::tuple<std::optional<std::remove_cvref_t<std::tuple_element_t<Indices, Tuple>>>...>;

template<typename T>
struct BinaryCodec {
    template<typename Context>
    static auto write(BinaryWriter& writer, const T& value, Context& context) noexcept -> void {
        static_assert(std::is_aggregate_v<T>, "type needs an owning BinaryCodec specialization");
        std::apply(
            [&](const auto&... fields) noexcept { (writer.write(fields, context), ...); },
            binary_aggregate_fields(value)
        );
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<T, BinaryError> {
        static_assert(std::is_aggregate_v<T>, "type needs an owning BinaryCodec specialization");
        using Fields = decltype(binary_aggregate_fields(std::declval<T&>()));
        using Indices = std::make_index_sequence<std::tuple_size_v<Fields>>;
        using Values = decltype(binary_optional_fields<Fields>(Indices {}));
        auto values = Values {};
        auto failure = std::optional<BinaryError>();
        std::apply(
            [&](auto&... fields) noexcept {
                auto read_field = [&](auto& field) noexcept -> void {
                    if (failure) {
                        return;
                    }
                    using Field = typename std::remove_cvref_t<decltype(field)>::value_type;
                    auto result = reader.read<Field>(context);
                    if (!result) {
                        failure.emplace(std::move(result.error()));
                        return;
                    }
                    field.emplace(std::move(*result));
                };
                (read_field(fields), ...);
            },
            values
        );
        if (failure) {
            return std::unexpected(std::move(*failure));
        }
        return binary_construct_aggregate<T>(values, Indices {});
    }
};

template<typename T>
    requires std::is_integral_v<T> && (!std::same_as<T, bool>)
struct BinaryCodec<T> {
    template<typename Context>
    static auto write(BinaryWriter& writer, T value, Context&) noexcept -> void {
        using Unsigned = std::make_unsigned_t<T>;
        writer.unsigned_value(static_cast<Unsigned>(value), sizeof(T));
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context&) noexcept -> std::expected<T, BinaryError> {
        auto result = reader.unsigned_value(sizeof(T));
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        using Unsigned = std::make_unsigned_t<T>;
        return std::bit_cast<T>(static_cast<Unsigned>(*result));
    }
};

template<>
struct BinaryCodec<bool> {
    template<typename Context>
    static auto write(BinaryWriter& writer, bool value, Context&) noexcept -> void {
        writer.unsigned_value(value ? 1 : 0, 1);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context&) noexcept -> std::expected<bool, BinaryError> {
        auto result = reader.unsigned_value(1);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        if (*result > 1) {
            return reader.fail("invalid binary boolean");
        }
        return *result == 1;
    }
};

template<typename T>
    requires std::is_enum_v<T>
struct BinaryCodec<T> {
    template<typename Context>
    static auto write(BinaryWriter& writer, T value, Context& context) noexcept -> void {
        writer.write(static_cast<std::underlying_type_t<T>>(value), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<T, BinaryError> {
        auto result = reader.read<std::underlying_type_t<T>>(context);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        return static_cast<T>(*result);
    }
};

template<typename T>
    requires std::same_as<T, float> || std::same_as<T, double>
struct BinaryCodec<T> {
    using Bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;

    template<typename Context>
    static auto write(BinaryWriter& writer, T value, Context& context) noexcept -> void {
        writer.write(std::bit_cast<Bits>(value), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<T, BinaryError> {
        auto result = reader.read<Bits>(context);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        return std::bit_cast<T>(*result);
    }
};

template<>
struct BinaryCodec<std::byte> {
    template<typename Context>
    static auto write(BinaryWriter& writer, std::byte value, Context&) noexcept -> void {
        writer.unsigned_value(std::to_integer<std::uint8_t>(value), 1);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context&) noexcept
        -> std::expected<std::byte, BinaryError> {
        auto result = reader.unsigned_value(1);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        return static_cast<std::byte>(*result);
    }
};

template<>
struct BinaryCodec<std::string> {
    template<typename Context>
    static auto write(BinaryWriter& writer, const std::string& value, Context&) noexcept -> void {
        writer.unsigned_value(value.size(), 8);
        writer.append(std::as_bytes(std::span(value.data(), value.size())));
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context&) noexcept
        -> std::expected<std::string, BinaryError> {
        auto size = reader.count();
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto bytes = reader.take(*size);
        if (!bytes) {
            return std::unexpected(std::move(bytes.error()));
        }
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }
};

template<typename T>
struct BinaryCodec<std::optional<T>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const std::optional<T>& value,
        Context& context
    ) noexcept -> void {
        writer.write(value.has_value(), context);
        if (value) {
            writer.write(*value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<std::optional<T>, BinaryError> {
        auto present = reader.read<bool>(context);
        if (!present) {
            return std::unexpected(std::move(present.error()));
        }
        if (!*present) {
            return std::optional<T>();
        }
        auto value = reader.read<T>(context);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return std::optional<T>(std::in_place, std::move(*value));
    }
};

template<typename T>
struct BinaryCodec<std::unique_ptr<T>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const std::unique_ptr<T>& value,
        Context& context
    ) noexcept -> void {
        writer.write(value != nullptr, context);
        if (value) {
            writer.write(*value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<std::unique_ptr<T>, BinaryError> {
        auto present = reader.read<bool>(context);
        if (!present) {
            return std::unexpected(std::move(present.error()));
        }
        if (!*present) {
            return std::unique_ptr<T>();
        }
        auto value = reader.read<std::remove_const_t<T>>(context);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return std::unique_ptr<T>(std::make_unique<std::remove_const_t<T>>(std::move(*value)));
    }
};

template<typename T>
struct BinaryCodec<UniqueIndirect<T>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const UniqueIndirect<T>& value,
        Context& context
    ) noexcept -> void {
        if (value.get() == nullptr) {
            writer.fail("cannot serialize a moved owning edge");
            return;
        }
        writer.write(*value, context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<UniqueIndirect<T>, BinaryError> {
        auto value = reader.read<T>(context);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return UniqueIndirect<T>(std::move(*value));
    }
};

template<typename T, typename Allocator>
struct BinaryCodec<std::vector<T, Allocator>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const std::vector<T, Allocator>& values,
        Context& context
    ) noexcept -> void {
        writer.unsigned_value(values.size(), 8);
        for (const auto& value : values) {
            if constexpr (std::same_as<T, bool>) {
                writer.write(static_cast<bool>(value), context);
            } else {
                writer.write(value, context);
            }
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<std::vector<T, Allocator>, BinaryError> {
        auto size = reader.count(sizeof(T));
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto values = std::vector<T, Allocator>();
        values.reserve(*size);
        for (auto index = 0uz; index < *size; ++index) {
            auto value = reader.read<T>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            values.push_back(std::move(*value));
        }
        return values;
    }
};

template<typename... Values>
struct BinaryCodec<std::tuple<Values...>> {
    using Tuple = std::tuple<Values...>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Tuple& value, Context& context) noexcept -> void {
        std::apply(
            [&](const auto&... fields) noexcept { (writer.write(fields, context), ...); },
            value
        );
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Tuple, BinaryError> {
        auto values = std::tuple<std::optional<Values>...> {};
        auto failure = std::optional<BinaryError>();
        std::apply(
            [&](auto&... fields) noexcept {
                auto read_field = [&](auto& field) noexcept -> void {
                    if (failure) {
                        return;
                    }
                    using Field = typename std::remove_cvref_t<decltype(field)>::value_type;
                    auto result = reader.read<Field>(context);
                    if (!result) {
                        failure.emplace(std::move(result.error()));
                        return;
                    }
                    field.emplace(std::move(*result));
                };
                (read_field(fields), ...);
            },
            values
        );
        if (failure) {
            return std::unexpected(std::move(*failure));
        }
        return std::apply(
            [](auto&... fields) static noexcept -> Tuple { return Tuple(std::move(*fields)...); },
            values
        );
    }
};

template<typename T, typename Allocator>
struct BinaryCodec<std::deque<T, Allocator>> {
    using Sequence = std::deque<T, Allocator>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Sequence& values, Context& context) noexcept
        -> void {
        writer.unsigned_value(values.size(), 8);
        for (const auto& value : values) {
            writer.write(value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Sequence, BinaryError> {
        auto size = reader.count(sizeof(T));
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto values = Sequence();
        for (auto index = 0uz; index < *size; ++index) {
            auto value = reader.read<T>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            values.push_back(std::move(*value));
        }
        return values;
    }
};

template<typename T>
struct BinaryCodec<std::shared_ptr<T>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const std::shared_ptr<T>& value,
        Context& context
    ) noexcept -> void {
        writer.write(value != nullptr, context);
        if (value) {
            writer.write(*value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<std::shared_ptr<T>, BinaryError> {
        auto present = reader.read<bool>(context);
        if (!present) {
            return std::unexpected(std::move(present.error()));
        }
        if (!*present) {
            return std::shared_ptr<T>();
        }
        auto value = reader.read<std::remove_const_t<T>>(context);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return std::shared_ptr<T>(std::make_shared<std::remove_const_t<T>>(std::move(*value)));
    }
};

template<typename Key, typename Value, typename Compare, typename Keys, typename Values>
struct BinaryCodec<std::flat_map<Key, Value, Compare, Keys, Values>> {
    using Map = std::flat_map<Key, Value, Compare, Keys, Values>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Map& values, Context& context) noexcept -> void {
        writer.unsigned_value(values.size(), 8);
        for (const auto& [key, value] : values) {
            writer.write(key, context);
            writer.write(value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Map, BinaryError> {
        auto size = reader.count(sizeof(Key) + sizeof(Value));
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto values = Map();
        for (auto index = 0uz; index < *size; ++index) {
            auto value = reader.read<std::pair<Key, Value>>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            if (!values.emplace(std::move(value->first), std::move(value->second)).second) {
                return reader.fail("duplicate binary flat map key");
            }
        }
        return values;
    }
};

template<typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
struct BinaryCodec<std::unordered_multimap<Key, Value, Hash, Equal, Allocator>> {
    using Map = std::unordered_multimap<Key, Value, Hash, Equal, Allocator>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Map& values, Context& context) noexcept -> void {
        writer.unsigned_value(values.size(), 8);
        for (const auto& value : values) {
            writer.write(value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Map, BinaryError> {
        auto size = reader.count(sizeof(typename Map::value_type));
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto values = Map();
        values.reserve(*size);
        for (auto index = 0uz; index < *size; ++index) {
            auto value = reader.read<std::pair<Key, Value>>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            values.emplace(std::move(value->first), std::move(value->second));
        }
        return values;
    }
};

template<typename First, typename Second>
struct BinaryCodec<std::pair<First, Second>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const std::pair<First, Second>& value,
        Context& context
    ) noexcept -> void {
        writer.write(value.first, context);
        writer.write(value.second, context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<std::pair<First, Second>, BinaryError> {
        auto first = reader.read<std::remove_const_t<First>>(context);
        if (!first) {
            return std::unexpected(std::move(first.error()));
        }
        auto second = reader.read<Second>(context);
        if (!second) {
            return std::unexpected(std::move(second.error()));
        }
        return std::pair<First, Second>(std::move(*first), std::move(*second));
    }
};

template<typename T, std::size_t Size>
struct BinaryCodec<std::array<T, Size>> {
    template<typename Context>
    static auto write(
        BinaryWriter& writer,
        const std::array<T, Size>& values,
        Context& context
    ) noexcept -> void {
        for (const auto& value : values) {
            writer.write(value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<std::array<T, Size>, BinaryError> {
        return read_values(reader, context, std::make_index_sequence<Size> {});
    }

private:
    template<typename Context, std::size_t... Indices>
    static auto read_values(
        BinaryReader& reader,
        Context& context,
        std::index_sequence<Indices...>
    ) noexcept -> std::expected<std::array<T, Size>, BinaryError> {
        auto values = std::array<std::optional<T>, Size> {};
        for (auto& value : values) {
            auto result = reader.read<T>(context);
            if (!result) {
                return std::unexpected(std::move(result.error()));
            }
            value.emplace(std::move(*result));
        }
        return std::array<T, Size> {std::move(*values[Indices])...};
    }
};

template<typename Key, typename Value, typename Compare, typename Allocator>
struct BinaryCodec<std::map<Key, Value, Compare, Allocator>> {
    using Map = std::map<Key, Value, Compare, Allocator>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Map& values, Context& context) noexcept -> void {
        writer.unsigned_value(values.size(), 8);
        for (const auto& value : values) {
            writer.write(value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Map, BinaryError> {
        auto size = reader.count(sizeof(typename Map::value_type));
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto values = Map();
        for (auto index = 0uz; index < *size; ++index) {
            auto value = reader.read<std::pair<Key, Value>>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            if (!values.emplace(std::move(value->first), std::move(value->second)).second) {
                return reader.fail("duplicate binary map key");
            }
        }
        return values;
    }
};

template<typename T, typename Compare, typename Allocator>
struct BinaryCodec<std::set<T, Compare, Allocator>> {
    using Set = std::set<T, Compare, Allocator>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Set& values, Context& context) noexcept -> void {
        writer.unsigned_value(values.size(), 8);
        for (const auto& value : values) {
            writer.write(value, context);
        }
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Set, BinaryError> {
        auto size = reader.count(sizeof(T));
        if (!size) {
            return std::unexpected(std::move(size.error()));
        }
        auto values = Set();
        for (auto index = 0uz; index < *size; ++index) {
            auto value = reader.read<T>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            if (!values.emplace(std::move(*value)).second) {
                return reader.fail("duplicate binary set entry");
            }
        }
        return values;
    }
};

template<typename Variant, std::size_t Index = 0, typename Context>
auto read_binary_variant(BinaryReader& reader, std::uint32_t tag, Context& context) noexcept
    -> std::expected<Variant, BinaryError> {
    if constexpr (Index < std::variant_size_v<Variant>) {
        if (tag == Index) {
            auto value = reader.read<std::variant_alternative_t<Index, Variant>>(context);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            return Variant(std::in_place_index<Index>, std::move(*value));
        }
        return read_binary_variant<Variant, Index + 1>(reader, tag, context);
    } else {
        return reader.fail("invalid binary variant tag");
    }
}

template<typename... Alternatives>
struct BinaryCodec<std::variant<Alternatives...>> {
    using Variant = std::variant<Alternatives...>;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Variant& value, Context& context) noexcept
        -> void {
        writer.unsigned_value(value.index(), 4);
        value.visit([&](const auto& alternative) noexcept { writer.write(alternative, context); });
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Variant, BinaryError> {
        auto tag = reader.unsigned_value(4);
        if (!tag) {
            return std::unexpected(std::move(tag.error()));
        }
        return read_binary_variant<Variant>(reader, static_cast<std::uint32_t>(*tag), context);
    }
};

template<typename Cleanup, typename... Alternatives>
struct BinaryCodec<TreeValue<Cleanup, Alternatives...>> {
    using Value = TreeValue<Cleanup, Alternatives...>;
    using Variant = typename Value::Base;

    template<typename Context>
    static auto write(BinaryWriter& writer, const Value& value, Context& context) noexcept -> void {
        BinaryCodec<Variant>::write(writer, static_cast<const Variant&>(value), context);
    }

    template<typename Context>
    static auto read(BinaryReader& reader, Context& context) noexcept
        -> std::expected<Value, BinaryError> {
        auto result = BinaryCodec<Variant>::read(reader, context);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        return std::move(*result).visit([](auto&& alternative) static noexcept -> Value {
            return Value(std::forward<decltype(alternative)>(alternative));
        });
    }
};

template<typename T, typename Context>
auto BinaryWriter::write(const T& value, Context& context) noexcept -> void {
    if (failure) {
        return;
    }
    if (depth >= compilation_binary_limits.depth) {
        fail("binary value exceeds nesting limit");
        return;
    }
    ++depth;
    BinaryCodec<std::remove_cvref_t<T>>::write(*this, value, context);
    --depth;
}

template<typename T, typename Context>
auto BinaryReader::read(Context& context) noexcept -> std::expected<T, BinaryError> {
    if (storage.size() > compilation_binary_limits.bytes) {
        return fail("binary input exceeds byte limit");
    }
    if (depth >= compilation_binary_limits.depth) {
        return fail("binary value exceeds nesting limit");
    }
    ++depth;
    auto result = BinaryCodec<T>::read(*this, context);
    --depth;
    return result;
}
