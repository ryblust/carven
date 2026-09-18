module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.support.task;

import :support.task;
import std;

namespace {

struct TaskTrace final {
    std::size_t entered;
    std::size_t exited;
    bool ordered;
};

class TaskVisit final {
public:
    TaskVisit(TaskTrace& trace, std::size_t depth) noexcept;
    ~TaskVisit() noexcept;

private:
    TaskTrace& trace;
    std::size_t depth;
};

TaskVisit::TaskVisit(TaskTrace& trace, std::size_t depth) noexcept
    : trace(trace),
      depth(depth) {
    ++trace.entered;
}

TaskVisit::~TaskVisit() noexcept {
    trace.ordered &= trace.exited == depth;
    ++trace.exited;
}

enum class TaskFailure { Leaf };

auto dependency(std::size_t depth, bool fail, TaskTrace& trace) noexcept
    -> ContinuationTask<std::expected<std::size_t, TaskFailure>> {
    const auto visit = TaskVisit(trace, depth);
    if (depth == 0uz) {
        if (fail) {
            co_return std::unexpected(TaskFailure::Leaf);
        }
        co_return 0uz;
    }
    auto child = co_await dependency(depth - 1uz, fail, trace);
    if (!child) {
        co_return std::unexpected(child.error());
    }
    co_return *child + 1uz;
}

} // namespace

TEST_CASE("Continuation tasks: dependencies preserve one execution and ordered cleanup") {
    constexpr auto depth = 32768uz;
    for (const auto fail : {false, true}) {
        auto trace = TaskTrace {.entered = 0uz, .exited = 0uz, .ordered = true};
        const auto result = dependency(depth, fail, trace).run();
        CHECK(result.has_value() == !fail);
        if (result) {
            CHECK(*result == depth);
        } else {
            CHECK(result.error() == TaskFailure::Leaf);
        }
        CHECK(trace.entered == depth + 1uz);
        CHECK(trace.exited == trace.entered);
        CHECK(trace.ordered);
    }
}

TEST_CASE("Continuation tasks: unrequested dependencies perform no work") {
    auto trace = TaskTrace {.entered = 0uz, .exited = 0uz, .ordered = true};
    {
        const auto task = dependency(1uz, false, trace);
        CHECK(trace.entered == 0uz);
    }
    CHECK(trace.entered == 0uz);
    CHECK(trace.exited == 0uz);
}
