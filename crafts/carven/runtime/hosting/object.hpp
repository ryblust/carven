#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace carven::runtime::hosting {

template<typename Target>
class ObjectRef;

namespace detail {

template<typename T>
auto type_key() noexcept -> const void* {
    static const auto identity = std::byte {};
    return &identity;
}

struct ObjectAccess;

struct ObjectRevoker final {
    std::weak_ptr<void> target;
    void (*revoke)(const std::shared_ptr<void>&) noexcept;
};

struct ObjectState final {
    void* address;
    const void* type;
    std::vector<ObjectRevoker> revokers;
    ObjectRef<void> (*copy)(const void*) noexcept;
};

template<typename T>
inline constexpr auto object_reference = false;

template<typename Target>
inline constexpr auto object_reference<ObjectRef<Target>> = true;

} // namespace detail

class Object;

// References share registration identity. Only own() retains native storage.
template<typename Target>
class ObjectRef final {
public:
    using TargetType = Target;

    ObjectRef() noexcept = default;

    template<typename Other>
    ObjectRef(const ObjectRef<Other>& other) noexcept
        : state(other.state),
          owner(other.owner) {
        static_assert(
            std::is_void_v<Target> || std::is_same_v<Target, Other>,
            "object references erase their target only at the dynamic boundary"
        );
    }

    template<typename T>
    static auto own(T value) noexcept -> ObjectRef;

    auto alive() const noexcept -> bool;

    template<typename Other>
    auto same_object(const ObjectRef<Other>& other) const noexcept -> bool {
        const auto object = state.lock();
        const auto compared = other.state.lock();
        return object && compared && object == compared;
    }

    template<typename T = Target>
    auto get() const noexcept -> T* {
        static_assert(!std::is_void_v<T>, "erased references require an explicit native target");

        static_assert(
            std::is_void_v<Target> || std::is_same_v<Target, T>,
            "typed references preserve their native target"
        );
        return static_cast<T*>(resolve(detail::type_key<T>()));
    }

private:
    auto resolve(const void* type) const noexcept -> void*;

    auto on_invalidate(
        std::weak_ptr<void> target,
        void (*revoke)(const std::shared_ptr<void>&) noexcept
    ) const noexcept -> bool;

    explicit ObjectRef(std::weak_ptr<detail::ObjectState> state) noexcept;
    std::weak_ptr<detail::ObjectState> state;
    std::shared_ptr<void> owner;

    template<typename>
    friend class ObjectRef;

    friend class Object;
    friend struct detail::ObjectAccess;
};

namespace detail {

struct ObjectAccess final {
    template<typename Target>
    static auto resolve(const ObjectRef<Target>& reference, const void* type) noexcept -> void* {
        return reference.resolve(type);
    }

    template<typename Target>
    static auto cast(const ObjectRef<void>& reference) noexcept -> ObjectRef<Target> {
        if constexpr (!std::is_void_v<Target>) {
            if (reference.resolve(type_key<Target>()) == nullptr) {
                return {};
            }
        }
        auto result = ObjectRef<Target>(reference.state);
        result.owner = reference.owner;
        return result;
    }

    static auto copy_value(const ObjectRef<void>& reference) noexcept -> ObjectRef<void> {
        const auto state = reference.state.lock();
        return state && state->address != nullptr && state->copy != nullptr
            ? state->copy(state->address)
            : reference;
    }

    static auto copyable(const ObjectRef<void>& reference) noexcept -> bool {
        const auto state = reference.state.lock();
        return state && state->copy != nullptr;
    }

    template<typename Target>
    static auto observe(
        const ObjectRef<Target>& reference,
        std::weak_ptr<void> target,
        void (*revoke)(const std::shared_ptr<void>&) noexcept
    ) noexcept -> bool {
        return reference.on_invalidate(std::move(target), revoke);
    }
};

} // namespace detail

// Registration addresses stay stable until invalidation. Access is serialized.
class Object final {
public:
    template<typename T>
    explicit Object(T& object) noexcept
        : Object(std::addressof(object), detail::type_key<T>()) {}

    Object(const Object&) = delete;
    Object(Object&&) = default;
    auto operator=(const Object&) -> Object& = delete;
    auto operator=(Object&&) -> Object& = delete;
    ~Object() noexcept;

    template<typename Target = void>
    auto reference() const noexcept -> ObjectRef<Target> {
        return detail::ObjectAccess::cast<Target>(ObjectRef<void>(state));
    }

    auto invalidate() noexcept -> void;

private:
    Object(void* address, const void* type) noexcept;
    std::shared_ptr<detail::ObjectState> state;
};

template<typename Target>
ObjectRef<Target>::ObjectRef(std::weak_ptr<detail::ObjectState> state) noexcept
    : state(std::move(state)) {}

template<typename Target>
auto ObjectRef<Target>::alive() const noexcept -> bool {
    const auto object = state.lock();
    return object
        && object->address != nullptr
        && (std::is_void_v<Target> || object->type == detail::type_key<Target>());
}

template<typename Target>
auto ObjectRef<Target>::resolve(const void* type) const noexcept -> void* {
    const auto object = state.lock();
    return object && object->type == type ? object->address : nullptr;
}

template<typename Target>
auto ObjectRef<Target>::on_invalidate(
    std::weak_ptr<void> target,
    void (*revoke)(const std::shared_ptr<void>&) noexcept
) const noexcept -> bool {
    const auto object = state.lock();
    if (!object || object->address == nullptr || target.expired() || revoke == nullptr) {
        return false;
    }
    std::erase_if(object->revokers, [](const auto& previous) noexcept {
        return previous.target.expired();
    });
    object->revokers.push_back({.target = std::move(target), .revoke = revoke});
    return true;
}

inline Object::Object(void* address, const void* type) noexcept
    : state(
          std::make_shared<detail::ObjectState>(detail::ObjectState {
              .address = address,
              .type = type,
              .revokers = {},
              .copy = nullptr
          })
      ) {}

inline Object::~Object() noexcept {
    invalidate();
}

template<typename Target>
template<typename T>
auto ObjectRef<Target>::own(T value) noexcept -> ObjectRef {
    static_assert(
        std::is_void_v<Target> || std::is_same_v<Target, T>,
        "owned references preserve their native target"
    );

    struct Owned final {
        T value;
        Object registration;

        explicit Owned(T input) noexcept
            : value(std::move(input)),
              registration(this->value) {}
    };

    auto owned = std::make_shared<Owned>(std::move(value));
    auto result = owned->registration.template reference<Target>();
    result.owner = std::move(owned);
    if constexpr (std::is_copy_constructible_v<T>) {
        const auto state = result.state.lock();
        state->copy = [](const void* address) noexcept -> ObjectRef<void> {
            return ObjectRef<void>::own(*static_cast<const T*>(address));
        };
    }
    return result;
}

inline auto Object::invalidate() noexcept -> void {
    if (!state || state->address == nullptr) {
        return;
    }
    state->address = nullptr;
    const auto revokers = std::exchange(state->revokers, {});
    for (const auto& revoker : revokers) {
        if (const auto target = revoker.target.lock()) {
            revoker.revoke(target);
        }
    }
}

} // namespace carven::runtime::hosting
