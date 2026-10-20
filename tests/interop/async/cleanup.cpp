#include "cleanup.hpp"

#include <carven/api/tests/interop/async/cleanup.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

namespace api = carven::api::tests::interop::async::cleanup;
namespace async = carven::runtime::execution;
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

auto run_cleanup_cases() noexcept -> void {
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
    probe::reset();
    {
        const auto result = async::drive_root(api::selected_failure());
        check(result.success_if() != nullptr && result.success_if()->value == 79);
        check(probe::matches_value_lifetime("G1 c7 g1 c2 h7 r7 d7 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::cleanup_stop_priority());
        check(result.is_stopped());
        check(probe::matches("G1 s3 g1 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::tail_await_stop());
        check(result.is_stopped());
        check(probe::matches("s3 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::tail_close_stop());
        check(result.is_stopped());
        check(probe::matches("v7 s3 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::implicit_fused_stop());
        check(result.is_stopped());
        check(probe::matches("s7 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::fused_stop());
        check(result.is_stopped());
        check(probe::matches("G1 p2 i7 g1 c2 "));
    }
    for (const auto internal_wait : {false, true}) {
        probe::reset();
        const auto result = async::drive_root(api::fused_checked_stop(internal_wait));
        check(result.is_stopped());
        check(probe::matches(internal_wait ? "G1 i8 a7 g1 c2 " : "G1 a7 g1 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::fused_yield_stop());
        check(result.is_stopped());
        check(probe::matches("G1 p2 o0 g1 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::prefix_stop(3));
        check(result.is_stopped());
        check(probe::matches("p3 p2 p1 p0 "));
    }
    probe::reset();
    {
        auto root = async::Root(api::tail_cancel(3));
        root.cancel();
        while (root.advance() != async::Progress::Complete) {
        }
        const auto result = root.take_completion();
        check(result.is_cancelled());
        check(probe::matches(""));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::tail_actual_stop(3, 9));
        check(result.is_stopped());
        check(probe::matches("a2 "));
    }
    for (const auto accept : {false, true}) {
        probe::reset();
        auto root = async::Root(api::cancellation_checkpoint(accept));
        root.cancel();
        while (root.advance() != async::Progress::Complete) {
        }
        const auto result = root.take_completion();
        if (accept) {
            check(result.is_cancelled());
        } else {
            const auto* success = result.success_if();
            check(success != nullptr && success->value);
        }
        check(probe::matches(accept ? "G1 g1 " : "G1 g1 u9 "));
    }
    for (const auto cancelled : {false, true}) {
        probe::reset();
        const auto result = async::drive_root(api::cancellation_policy(cancelled));
        check(cancelled ? result.is_cancelled() : result.success_if() != nullptr);
        check(probe::matches(cancelled ? "G1 g1 c4 " : "G1 g1 u9 c0 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::cancellation_tail());
        check(result.is_cancelled());
        check(probe::matches("G1 g1 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::cancellation_cleanup_stop());
        check(result.is_stopped());
        check(probe::matches("G1 s5 g1 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::cancellation_child_stop());
        check(result.is_stopped());
        check(probe::matches("G1 g1 s3 "));
    }
    for (const auto recover : {false, true}) {
        probe::reset();
        const auto result = async::drive_root(api::failure_recovery(recover));
        const auto* success = result.success_if();
        check(success != nullptr && success->value);
        check(
            probe::matches_value_lifetime(
                recover ? "G1 c7 l7 r7 d7 g1 u9 c0 " : "G1 c7 l7 g1 h7 r7 d7 u9 c0 "
            )
        );
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::failure_with_child());
        const auto* failure =
            result.failure_if<api::cv_escaped_functions::failure_with_child::Failure0>();
        check(failure != nullptr && failure->value == 7);
        check(failure->payload.get() == 7);
    }
    check(probe::matches_value_lifetime("G1 c7 l7 g1 c4 r7 d7 "));
    probe::reset();
    {
        const auto result = async::drive_root(api::failure_cleanup_stop());
        check(result.is_stopped());
        check(probe::events.find("s5 ") != std::string::npos);
        check(probe::matches_value_lifetime("G1 c7 g1 d7 c2 "));
    }
    probe::reset();
    {
        const auto result = async::drive_root(api::failure_child_stop());
        check(result.is_stopped());
        check(probe::events.find("s3 ") != std::string::npos);
        check(probe::matches_value_lifetime("G1 c7 l7 g1 d7 "));
    }
    std::puts("async lexical cleanup passed");
}
