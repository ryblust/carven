module carven:test.internal.support.task;

import :support.task;
import :test.harness.framework;
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

const TestSuite suite([] static noexcept {
    "Continuation tasks: dependencies preserve one execution and ordered cleanup"_test =
        [] static noexcept {
            constexpr auto depth = 32768uz;
            const auto outcomes = std::array {false, true};
            each(
                outcomes,
                [](bool fail) static noexcept { return fail ? "failure" : "success"; },
                [=](bool fail) noexcept {
                    auto trace = TaskTrace {.entered = 0uz, .exited = 0uz, .ordered = true};
                    const auto result = dependency(depth, fail, trace).run();
                    expect_equal(result.has_value(), !fail);
                    if (result) {
                        expect_equal(*result, depth);
                    } else {
                        expect_equal(result.error(), TaskFailure::Leaf);
                    }
                    expect_equal(trace.entered, depth + 1uz);
                    expect_equal(trace.exited, trace.entered);
                    expect(trace.ordered);
                }
            );
        };

    "Continuation tasks: unrequested dependencies perform no work"_test = [] static noexcept {
        auto trace = TaskTrace {.entered = 0uz, .exited = 0uz, .ordered = true};
        {
            const auto task = dependency(1uz, false, trace);
            expect_equal(trace.entered, 0uz);
        }
        expect_equal(trace.entered, 0uz);
        expect_equal(trace.exited, 0uz);
    };
});

} // namespace
