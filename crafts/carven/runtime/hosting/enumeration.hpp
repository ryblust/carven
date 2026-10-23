#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime::hosting::detail {

template<typename T>
constexpr auto runtime_native_enum = requires {
    carven_type_name(static_cast<T*>(nullptr));
    carven_cases(static_cast<T*>(nullptr));
};

template<typename Signature>
struct NativeEnumPayload;

template<typename Result, typename... Arguments>
struct NativeEnumPayload<Result (*)(Arguments...) noexcept> final {
    using Type = std::tuple<Arguments...>;
};

template<typename T>
    requires std::is_enum_v<T>
struct NativeEnumPayload<T> final {
    using Type = std::tuple<>;
};

template<typename Case>
using NativeEnumArguments = typename NativeEnumPayload<typename Case::second_type>::Type;

// Generated case factories and storage describe the same source alternatives.
template<typename T, typename Consumer>
auto visit_native_enum(T&& value, Consumer&& consume) noexcept -> bool {
    using Enum = std::remove_cvref_t<T>;
    const auto cases = carven_cases(static_cast<Enum*>(nullptr));
    return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) noexcept {
        const auto visit = [&]<std::size_t Index>() noexcept -> bool {
            const auto& descriptor = std::get<Index>(cases);
            if constexpr (std::is_enum_v<Enum>) {
                if (value != descriptor.second) {
                    return false;
                }
                std::forward<Consumer>(consume)(descriptor.first, std::tuple<>());
            } else {
                auto&& storage =
                    std::forward<T>(value).*carven_enum_storage(static_cast<Enum*>(nullptr));
                if (storage.index() != Index) {
                    return false;
                }
                auto&& payload = std::get<Index>(std::forward<decltype(storage)>(storage));
                using Payload = std::remove_cvref_t<decltype(payload)>;
                std::apply(
                    [&](const auto&... field) noexcept {
                        std::forward<Consumer>(consume)(
                            descriptor.first,
                            std::forward_as_tuple(
                                // This only forms field references; it does not move the payload.
                                // NOLINTNEXTLINE(bugprone-use-after-move)
                                std::forward<decltype(payload)>(payload).*field.second...
                            )
                        );
                    },
                    carven_fields(static_cast<Payload*>(nullptr))
                );
            }
            return true;
        };
        return (visit.template operator()<Indices>() || ...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(cases)>>());
}

} // namespace carven::runtime::hosting::detail
