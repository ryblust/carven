module carven:support.task;

import :support.invariant;
import std;

// A root drives suspended dependencies through a depth-first continuation loop.
// Completed results need no turn; awaiting a coroutine never resumes it inline.
struct ContinuationTaskLoop final {
    std::coroutine_handle<> next;

    auto resume_one() noexcept -> void;
};

template<typename Value>
class [[nodiscard]] ContinuationTask final {
public:
    class Promise;
    using promise_type = Promise;
    using Handle = std::coroutine_handle<Promise>;

    explicit ContinuationTask(Handle handle) noexcept
        : ready(false),
          state(handle) {}

    // A completed operation needs neither a coroutine frame nor a scheduling turn.
    explicit ContinuationTask(Value&& result) noexcept
        : ready(true),
          state(std::move(result)) {}

    ContinuationTask(const ContinuationTask&) = delete;

    ContinuationTask(ContinuationTask&& other) noexcept
        : ready(other.ready),
          state(Handle {}) {
        if (ready) {
            std::construct_at(std::addressof(state.value), std::move(other.state.value));
        } else {
            state.handle = std::exchange(other.state.handle, {});
        }
    }

    auto operator=(const ContinuationTask&) -> ContinuationTask& = delete;
    auto operator=(ContinuationTask&&) -> ContinuationTask& = delete;

    ~ContinuationTask() noexcept {
        if (ready) {
            std::destroy_at(std::addressof(state.value));
        } else if (state.handle) {
            state.handle.destroy();
        }
    }

    class Awaiter final {
    public:
        explicit Awaiter(ContinuationTask& task) noexcept
            : task(task) {}

        auto await_ready() const noexcept -> bool { return task.done(); }

        template<typename Parent>
        auto await_suspend(std::coroutine_handle<Parent> parent) noexcept -> void {
            const auto handle = task.state.handle;
            auto& promise = handle.promise();
            promise.attach(parent.promise().task_loop(), parent);
            promise.task_loop().next = handle;
        }

        // The task owns the result through the consuming full expression.
        // Forwarding to another promise need not construct an intermediate value.
        auto await_resume() noexcept -> Value&& { return task.result_value(); }

    private:
        ContinuationTask& task;
    };

    auto operator co_await() && noexcept -> Awaiter { return Awaiter(*this); }

    auto start(ContinuationTaskLoop& loop) noexcept -> void {
        loop.next = {};
        if (!ready) {
            state.handle.promise().attach(loop, {});
            loop.next = state.handle;
        }
    }

    auto done() const noexcept -> bool { return ready || state.handle.done(); }

    auto result() const noexcept -> const Value& {
        return ready ? state.value : state.handle.promise().result_view();
    }

    auto take_result() noexcept -> Value { return result_value(); }

    // Synchronous entry points drive a root task. Dependencies await their children.
    auto run() && noexcept -> Value {
        auto loop = ContinuationTaskLoop {.next = {}};
        start(loop);
        while (loop.next) {
            loop.resume_one();
        }
        return result_value();
    }

    class Promise final {
    public:
        auto get_return_object() noexcept -> ContinuationTask {
            return ContinuationTask(Handle::from_promise(*this));
        }

        auto initial_suspend() const noexcept -> std::suspend_always { return {}; }

        class Completion final {
        public:
            auto await_ready() const noexcept -> bool { return false; }

            auto await_suspend(Handle self) const noexcept -> void {
                auto& promise = self.promise();
                promise.task_loop().next = promise.parent;
            }

            auto await_resume() const noexcept -> void {}
        };

        auto final_suspend() const noexcept -> Completion { return {}; }

        auto return_value(Value&& value) noexcept -> void { result.emplace(std::move(value)); }

        template<typename Result>
            requires std::constructible_from<Value, Result&&>
        auto return_value(Result&& value) noexcept -> void {
            result.emplace(std::forward<Result>(value));
        }

        auto unhandled_exception() const noexcept -> void { std::terminate(); }

        auto attach(ContinuationTaskLoop& scheduler, std::coroutine_handle<> continuation) noexcept
            -> void {
            if (loop != nullptr) {
                invariant_violation("continuation task was started more than once");
            }
            loop = std::addressof(scheduler);
            parent = continuation;
        }

        auto task_loop() const noexcept -> ContinuationTaskLoop& {
            if (loop == nullptr) {
                invariant_violation("continuation task has no continuation loop");
            }
            return *loop;
        }

        auto result_view() const noexcept -> const Value& {
            if (!result) {
                invariant_violation("continuation task has no terminal result");
            }
            return *result;
        }

        auto take_result() noexcept -> Value&& {
            if (!result) {
                invariant_violation("continuation task completed without a result");
            }
            return std::move(*result);
        }

    private:
        ContinuationTaskLoop* loop = nullptr;
        std::coroutine_handle<> parent;
        std::optional<Value> result;
    };

private:
    auto result_value() noexcept -> Value&& {
        return ready ? std::move(state.value) : state.handle.promise().take_result();
    }

    // The tag is fixed at construction. Only the selected member has a lifetime.
    union State {
        explicit State(Handle handle) noexcept
            : handle(handle) {}

        explicit State(Value&& value) noexcept
            : value(std::move(value)) {}

        ~State() noexcept {}

        Handle handle;
        Value value;
    };

    bool ready;
    State state;
};
