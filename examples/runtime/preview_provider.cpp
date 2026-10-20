#include <carven/providers/examples/runtime/preview.hpp>

#include <carven/runtime/hosting/async.hpp>

namespace carven::providers::examples::runtime::preview::cv_escaped_functions::query {

auto invoke(Argument0 text) noexcept -> Completion {
    return service(std::move(text));
}

} // namespace carven::providers::examples::runtime::preview::cv_escaped_functions::query
