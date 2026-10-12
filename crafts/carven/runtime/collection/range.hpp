#pragma once

#include <type_traits>

namespace carven::runtime {

// Integer interval with snapshotted bounds.
template<typename T>
    requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
struct Range final {
    T first;
    T last;
    bool inclusive;

    struct Sentinel final {};

    struct Iterator final {
        T current;
        T last;
        bool inclusive;
        bool finished;

        constexpr auto operator*() const noexcept -> T { return current; }

        constexpr auto operator++() noexcept -> Iterator& {
            // Test before increment so a closed interval can include the type maximum.
            if (current == last) {
                finished = true;
            } else {
                ++current;
                finished = !inclusive && current == last;
            }
            return *this;
        }

        constexpr auto operator!=(Sentinel) const noexcept -> bool { return !finished; }
    };

    constexpr auto begin() const noexcept -> Iterator {
        return {
            .current = first,
            .last = last,
            .inclusive = inclusive,
            .finished = first > last || (!inclusive && first == last)
        };
    }

    constexpr auto end() const noexcept -> Sentinel { return {}; }
};

} // namespace carven::runtime
