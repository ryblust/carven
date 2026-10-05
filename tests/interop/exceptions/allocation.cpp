#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include <carven/api/tests/interop/exceptions/precomputed.hpp>

#include <carven/runtime/format.hpp>
#include <carven/runtime/string.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <string_view>

namespace {

bool fail_allocation = false;

auto aborted(int signal) noexcept -> void {
    std::_Exit(signal == SIGABRT ? 73 : 74);
}

} // namespace

// Replacement allocation is confined to this failure-injection executable.
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

// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) -> int try {
    std::set_terminate([]() noexcept { std::_Exit(73); });
    std::signal(SIGABRT, aborted);
    if (argc != 2) {
        return 1;
    }
    const auto operation = std::string_view(argv[1]);
    if (operation == "print-inner-allocate") {
        // Termination uses _Exit; expose each completed write to the capture file.
        if (std::setvbuf(stdout, nullptr, _IONBF, 0) != 0) {
            return 3;
        }
    }
    if (operation == "format-allocate") {
        fail_allocation = true;
        static_cast<void>(carven::runtime::format("{:4096}", 1));
    } else if (operation == "precomputed-format-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::discard_precomputed();
    } else if (operation == "mixed-format-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::discard_mixed(7);
    } else if (operation == "append-format-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::append_dynamic(7);
    } else if (operation == "append-precomputed-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::append_precomputed();
    } else if (operation == "print-inner-allocate") {
        fail_allocation = true;
        carven::api::tests::interop::exceptions::precomputed::print_precomputed();
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
