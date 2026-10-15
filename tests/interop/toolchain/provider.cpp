#include <carven/providers/app.hpp>

#include "provider.hpp"

auto provided() -> double {
    auto network = carven::standard::net::Network();
    return network.startup_error() == 0 ? provided_base + 0.75 : 0.0;
}
