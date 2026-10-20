#include <carven/providers/tests/interop/hosting/enumeration.hpp>

#include <carven/runtime/hosting/async.hpp>

namespace carven::providers::tests::interop::hosting::enumeration::cv_escaped_functions::
    native_failure {

auto invoke() noexcept -> Completion {
    return service();
}

} // namespace carven::providers::tests::interop::hosting::enumeration::cv_escaped_functions::native_failure

namespace carven::providers::tests::interop::hosting::enumeration::cv_escaped_functions::
    async_failure {

auto invoke() noexcept -> Completion {
    return service();
}

} // namespace carven::providers::tests::interop::hosting::enumeration::cv_escaped_functions::async_failure
