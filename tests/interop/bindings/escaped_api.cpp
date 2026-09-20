#include <carven/api/tests/interop/bindings/export.hpp>
#include <cstdlib>

namespace {

namespace api = carven::api::tests::interop::bindings::cv_escaped_6578706f7274;

struct VerifyEscapedAPI final {
    VerifyEscapedAPI() noexcept {
        if (api::cv_escaped_756e696f6e() != 17
            || api::cv_escaped_63765f657363617065645f37353665363936663665() != 19
            || api::cv_escaped_5f5570706572() != 23
            || api::cv_escaped_615f5f62() != 29) {
            std::abort();
        }
    }
};

const VerifyEscapedAPI verify;

} // namespace
