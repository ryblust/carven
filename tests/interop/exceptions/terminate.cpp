#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include <carven/runtime/callable.hpp>
#include <carven/runtime/outcome.hpp>
#include <carven/runtime/string.hpp>
#include <carven/runtime/format.hpp>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <utility>

struct FormatProbe final {
    bool invalid_utf8;
};

template<>
struct std::formatter<FormatProbe> final : std::formatter<std::string_view> {
    auto format(const FormatProbe& value, std::format_context& context) const {
        if (!value.invalid_utf8) {
            throw std::format_error("provider failure");
        }
        return std::formatter<std::string_view>::format(std::string_view("\xff", 1), context);
    }
};

namespace {

bool fail_allocation = false;

auto aborted(int signal) noexcept -> void {
    std::_Exit(signal == SIGABRT ? 73 : 74);
}

class Foreign final {
public:
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

// Allocation failure is enabled only by the selected isolated scenario.
auto operator new(std::size_t size) -> void* {
    if (fail_allocation) {
        throw std::bad_alloc();
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

auto operator delete(void* memory) noexcept -> void {
    std::free(memory);
}

// This process is built with C++ exceptions enabled to test the protocol boundary.
// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) -> int {
    std::set_terminate([]() noexcept { std::_Exit(73); });
    std::signal(SIGABRT, aborted);
    if (argc != 2) {
        return 1;
    }
    const auto operation = std::string_view(argv[1]);
    using Outcome = carven::runtime::Outcome<Foreign, Foreign>;
    if (operation == "copy") {
        const auto source = Foreign(false);
        static_cast<void>(Outcome::success_from([&]() { return Foreign(source); }));
    } else if (operation == "move") {
        auto source = Outcome::success_from([]() noexcept { return Foreign(false); });
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
    } else if (operation == "format-width") {
        static_cast<void>(carven::runtime::format("{:>{}}", 1, -1));
    } else if (operation == "format-throw" || operation == "format-utf8") {
        static_cast<void>(carven::runtime::format("{}", FormatProbe {operation == "format-utf8"}));
    } else if (operation == "format-allocate") {
        fail_allocation = true;
        static_cast<void>(carven::runtime::format("{:4096}", 1));
    } else if (operation == "string-allocate" || operation == "string-copy") {
        const auto input = std::string(4096, 'x');
        if (operation == "string-allocate") {
            fail_allocation = true;
            static_cast<void>(carven::runtime::String::from_str(input));
        } else {
            const auto source = carven::runtime::String::from_str(input);
            fail_allocation = true;
            // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): Exercise copy allocation failure.
            const auto copy = source;
            static_cast<void>(copy);
        }
    }
    return 2;
}
