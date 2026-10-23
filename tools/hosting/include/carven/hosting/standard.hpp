#pragma once

#include <carven/hosting/api.hpp>

#include <vector>

// Hosts choose these ordinary bindings when they provide Standard Craft capabilities.
auto host_standard_services() noexcept -> std::vector<HostServiceBinding>;
