#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include <carven/runtime/callable.hpp>
#include <carven/runtime/outcome.hpp>

#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>

namespace {

struct Foreign final {
    bool fail;
    explicit Foreign(bool source) noexcept
        : fail(source) {}
    Foreign(const Foreign&) { throw 1; }
    Foreign(Foreign&& source) noexcept(false)
        : fail(source.fail) {
        if (fail) {
            throw 2;
        }
    }
    auto operator=(const Foreign&) -> Foreign& = delete;
    auto operator=(Foreign&&) -> Foreign& = delete;
};

auto foreign_function() -> int {
    throw 3;
}

} // namespace

// This process is built with C++ exceptions enabled to test the protocol boundary.
// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) -> int {
    std::set_terminate([]() noexcept { std::_Exit(73); });
    if (argc != 2) {
        return 1;
    }
    const auto operation = std::string_view(argv[1]);
    using Outcome = carven::runtime::Outcome<Foreign, Foreign>;
    if (operation == "copy") {
        const auto source = Foreign(false);
        static_cast<void>(Outcome::success(source));
    } else if (operation == "move") {
        auto source = Outcome::success(Foreign(false));
        source.success_if()->value.fail = true;
        static_cast<void>(Outcome(std::move(source)));
    } else if (operation == "failure") {
        const auto source = Foreign(false);
        static_cast<void>(Outcome::failure(source));
    } else if (operation == "function") {
        const auto view = carven::runtime::FunctionRef<int() noexcept>(&foreign_function);
        static_cast<void>(view());
    } else if (operation == "object") {
        const auto callable = []() -> int {
            throw 4;
        };
        const auto view = carven::runtime::FunctionRef<int() noexcept>(callable);
        static_cast<void>(view());
    }
    return 2;
}
