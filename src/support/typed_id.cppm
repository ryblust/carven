module carven:support.typed_id;

import std;

template<typename Tag>
class TypedID final {
public:
    static constexpr auto from_index(std::uint32_t index) noexcept -> TypedID {
        return TypedID(index);
    }

    constexpr auto index() const noexcept -> std::uint32_t { return id_index; }

    constexpr auto operator<=>(const TypedID&) const noexcept = default;

private:
    explicit constexpr TypedID(std::uint32_t index) noexcept
        : id_index(index) {}

    std::uint32_t id_index;
};
