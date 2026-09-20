#pragma once

#include <array>
#include <utility>

namespace owning_field_probe {

inline std::array<int, 3> live {};
inline int evaluations = 0;

class Resource final {
public:
    explicit Resource(int id) noexcept
        : id_(id) {
        ++live[id_];
    }

    Resource(const Resource&) = delete;

    Resource(Resource&& other) noexcept
        : id_(std::exchange(other.id_, 0)) {}

    ~Resource() {
        if (id_ != 0) {
            --live[id_];
        }
    }

    auto id() const noexcept -> int { return id_; }

private:
    int id_;
};

inline auto count(int id) noexcept -> int {
    return live[id];
}

inline auto evaluated() noexcept -> int {
    return ++evaluations;
}

inline auto calls() noexcept -> int {
    return evaluations;
}

} // namespace owning_field_probe
