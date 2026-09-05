#include "carven/api/tests/interop/unicode_contract/export_argument.hpp"
#include "carven/api/tests/interop/unicode_contract/export_argument.hpp"

namespace {

struct InvokeWithInvalidScalar final {
    InvokeWithInvalidScalar() noexcept {
        carven::api::tests::interop::unicode_contract::export_argument::accept_unicode_scalar(
            static_cast<char32_t>(0xd800)
        );
    }
};

const InvokeWithInvalidScalar invoke_with_invalid_scalar {};

} // namespace
