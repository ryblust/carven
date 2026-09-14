#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include <carven/api/tests/interop/exceptions/precomputed.hpp>

#include <carven/runtime/callable.hpp>
#include <carven/runtime/outcome.hpp>
#include <carven/runtime/string.hpp>
#include <carven/runtime/format.hpp>
#include <carven/runtime/print.hpp>

#include "provider.hpp"

#include <csignal>
#include <cstdlib>
#include <exception>
#include <limits>
#include <locale>
#include <new>
#include <string>
#include <string_view>
#include <utility>

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

class InvalidGrouping final : public std::numpunct<char> {
private:
    auto do_grouping() const -> std::string override { return "\3"; }

    auto do_thousands_sep() const -> char override { return static_cast<char>(0xff); }
};

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
auto main(int argc, char** argv) -> int try {
    std::set_terminate([]() noexcept { std::_Exit(73); });
    std::signal(SIGABRT, aborted);
    if (argc != 2) {
        return 1;
    }
    const auto operation = std::string_view(argv[1]);
    if (operation.starts_with("print-")) {
        // Termination uses _Exit; expose each completed write to the capture file.
        if (std::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
            return 3;
        }
    }
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
    } else if (operation == "precomputed-format-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::discard_precomputed();
    } else if (operation == "mixed-format-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::discard_mixed(7);
    } else if (operation == "format-character-utf8") {
        // Stay within char's range so formatting succeeds before UTF-8 validation fails.
        constexpr auto byte = std::numeric_limits<char>::is_signed ? -1 : 255;
        carven::api::tests::interop::exceptions::precomputed::discard_character(byte);
    } else if (operation == "format-locale-utf8") {
        static_cast<void>(
            std::locale::global(std::locale(std::locale::classic(), new InvalidGrouping))
        );
        carven::api::tests::interop::exceptions::precomputed::discard_localized(1000);
    } else if (operation == "append-format-throw" || operation == "append-format-utf8") {
        carven::api::tests::interop::exceptions::precomputed::append_native_failure(
            operation == "append-format-utf8"
        );
    } else if (operation == "append-format-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::append_dynamic(7);
    } else if (operation == "append-precomputed-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::append_precomputed();
    } else if (operation == "append-format-width") {
        carven::api::tests::interop::exceptions::precomputed::append_width(-1);
    } else if (operation == "print-inner-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::print_precomputed();
    } else if (operation == "print-inner-throw" || operation == "print-inner-utf8") {
        carven::api::tests::interop::exceptions::precomputed::print_native_failure(
            operation == "print-inner-utf8"
        );
    } else if (operation == "print-later-throw") {
        carven::runtime::println(std::string_view("42"), FormatProbe {.invalid_utf8 = false});
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
} catch (...) {
    // Escaping the runtime boundary is a failure, even though uncaught exceptions terminate.
    return 75;
}
