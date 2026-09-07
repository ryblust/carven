#include <carven/api/tests/interop/cpp_names/export.hpp>
#include <cstdlib>

namespace {
namespace api = carven::api::tests::interop::cpp_names::cv_escaped_6578706f7274;

struct VerifyEscapedAPI final {
    VerifyEscapedAPI() noexcept {
        if (api::cv_escaped_636c617373() != 17
            || api::cv_escaped_63765f657363617065645f36333663363137333733() != 19
            || api::cv_escaped_5f5570706572() != 23
            || api::cv_escaped_615f5f62() != 29) {
            std::abort();
        }
    }
};

const VerifyEscapedAPI verify;
} // namespace
