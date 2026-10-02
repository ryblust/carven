#pragma once

#include <cstdint>

namespace storage_probe {

class Observer final {
public:
    Observer() noexcept = default;
    Observer(const Observer&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(const Observer&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;

    auto remember(const std::int32_t& value, std::int32_t) const noexcept -> bool {
        pending = &value;
        return true;
    }

    auto observe(bool selected) const noexcept -> bool {
        return !selected || (pending != nullptr && *pending == 3);
    }

private:
    mutable const std::int32_t* pending = nullptr;
};

} // namespace storage_probe
