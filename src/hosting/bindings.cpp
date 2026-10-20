module;

#include <carven/hosting/api.hpp>

module carven:hosting.bindings.impl;

import std;

extern "C++" {

HostObjectBinding::HostObjectBinding(State state) noexcept
    : state(std::move(state)) {}

auto HostObjectAccess::make(HostObjectBinding::State state) noexcept -> HostObjectBinding {
    return HostObjectBinding(std::move(state));
}

auto HostObjectAccess::registration(const HostObjectBinding& binding) noexcept
    -> const HostObjectBinding::State& {
    return binding.state;
}

HostServiceBinding::HostServiceBinding(State state) noexcept
    : state(std::move(state)) {}

auto HostServiceAccess::make(HostServiceBinding::State state) noexcept -> HostServiceBinding {
    return HostServiceBinding(std::move(state));
}

auto HostServiceAccess::registration(const HostServiceBinding& binding) noexcept
    -> const HostServiceBinding::State& {
    return binding.state;
}

HostEntry::HostEntry(State state) noexcept
    : state(std::move(state)) {}

auto HostEntryAccess::registration(const HostEntry& entry) noexcept -> const HostEntry::State& {
    return entry.state;
}

} // extern "C++"
