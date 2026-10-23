module;

#include <carven/hosting/api.hpp>

module carven:hosting.entry.impl;

import std;

extern "C++" {

HostEntry::HostEntry(State state) noexcept
    : state(std::move(state)) {}

auto HostEntryAccess::registration(const HostEntry& entry) noexcept -> const HostEntry::State& {
    return entry.state;
}

} // extern "C++"
