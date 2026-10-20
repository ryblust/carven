#include <carven/providers/tests/interop/hosting/preview.hpp>

#include <carven/runtime/hosting/async.hpp>

namespace carven::providers::tests::interop::hosting::preview::cv_escaped_functions::query {

auto invoke(Argument0 text) noexcept -> Completion {
    return service(std::move(text));
}

} // namespace carven::providers::tests::interop::hosting::preview::cv_escaped_functions::query
