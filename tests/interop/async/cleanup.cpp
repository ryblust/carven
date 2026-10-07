#include "cleanup.hpp"
#include <carven/api/tests/interop/async/cleanup.hpp>

#include <cstdio>
#include <cstdlib>

namespace {

namespace api = carven::api::tests::interop::async::cleanup;
namespace async = carven::runtime::async;
namespace probe = tail_outcome_probe;

auto check(bool condition) noexcept -> void {
    if (!condition) {
        std::exit(EXIT_FAILURE);
    }
}

auto scalar(auto operation, int expected) noexcept -> void {
    probe::reset();
    const auto result = async::drive_root(operation());
    const auto* success = result.success_if();
    check(success != nullptr && success->value == expected);
    check(probe::matches("G1 g1 c2 "));
}

} // namespace

auto main() -> int {
    scalar(api::nested_return, 7);
    scalar(api::nested_break, 7);
    scalar(api::nested_continue, 7);
    scalar(api::caught_failure, 9);
    scalar(api::release_before_wait, 7);
    probe::reset();
    {
        const auto result = async::drive_root(api::nested_result());
        const auto* success = result.success_if();
        check(success != nullptr && success->value.value == 7);
        check(probe::matches("G1 G7 g1 c2 "));
    }
    check(probe::matches("G1 G7 g1 c2 g7 "));
    probe::reset();
    {
        const auto result = async::drive_root(api::nested_failure());
        check(result.success_if() == nullptr && !result.is_cancelled());
        check(probe::matches("G1 g1 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::nested_cancel());
        check(result.is_cancelled());
        check(probe::matches("G1 g1 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::initializer_break());
        check(result.success_if() != nullptr && result.success_if()->value == 2);
        check(probe::matches("G1 g1 c2 G1 g1 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::initializer_continue());
        check(result.success_if() != nullptr && result.success_if()->value == 2);
        check(probe::matches("G1 g1 G1 g1 c2 "));
    }
    scalar([] { return api::branch_return(true); }, 7);
    probe::reset();
    {
        const auto result = async::drive_root(api::branch_return(false));
        check(result.success_if() != nullptr && result.success_if()->value == 8);
        check(probe::matches("G5 c2 g5 "));
    }
    for (int kind = 0; kind < 3; ++kind) {
        probe::reset();
        const auto result = async::drive_root(api::layered_exit(kind));
        if (kind == 0) {
            check(result.success_if() != nullptr && result.success_if()->value == 7);
        } else {
            check(result.success_if() == nullptr && result.is_cancelled() == (kind == 2));
        }
        check(probe::matches("G1 c3 g1 c2 "));
    }
    scalar([] { return api::assignment_regions(0); }, 7);
    scalar([] { return api::assignment_regions(1); }, 7);
    std::puts("async lexical cleanup passed");
    return EXIT_SUCCESS;
}
