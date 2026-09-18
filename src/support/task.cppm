module carven:support.task;

import :support.invariant;
import std;

// A root drives its dependencies through a depth-first continuation loop.
// Awaiting a dependency yields to that loop; it never resumes a child inline.
struct ContinuationTaskLoop final {
    std::coroutine_handle<> next;
};

template<typename Value>
class [[nodiscard]] ContinuationTask final {
public:
    class Promise;
    using promise_type = Promise;
    using Handle = std::coroutine_handle<Promise>;

    explicit ContinuationTask(Handle handle) noexcept
        : handle(handle) {}

    ContinuationTask(const ContinuationTask&) = delete;

    ContinuationTask(ContinuationTask&& other) noexcept
        : handle(std::exchange(other.handle, {})) {}

    auto operator=(const ContinuationTask&) -> ContinuationTask& = delete;
    auto operator=(ContinuationTask&&) -> ContinuationTask& = delete;

    ~ContinuationTask() noexcept {
        if (handle) {
            handle.destroy();
        }
    }

    class Awaiter final {
    public:
        explicit Awaiter(Handle handle) noexcept
            : handle(handle) {}

        Awaiter(const Awaiter&) = delete;

        Awaiter(Awaiter&& other) noexcept
            : handle(std::exchange(other.handle, {})) {}

        auto operator=(const Awaiter&) -> Awaiter& = delete;
        auto operator=(Awaiter&&) -> Awaiter& = delete;

        ~Awaiter() noexcept {
            if (handle) {
                handle.destroy();
            }
        }

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Parent>
        auto await_suspend(std::coroutine_handle<Parent> parent) noexcept -> void {
            auto& promise = handle.promise();
            promise.attach(parent.promise().task_loop(), parent);
            promise.task_loop().next = handle;
        }

        auto await_resume() noexcept -> Value { return handle.promise().take_result(); }

    private:
        Handle handle;
    };

    auto operator co_await() && noexcept -> Awaiter { return Awaiter(std::exchange(handle, {})); }

    // Synchronous entry points drive a root task. Dependencies await their children.
    auto run() && noexcept -> Value {
        auto loop = ContinuationTaskLoop {.next = handle};
        handle.promise().attach(loop, {});
        while (loop.next) {
            std::exchange(loop.next, {}).resume();
        }
        return handle.promise().take_result();
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

        auto return_value(Value value) noexcept -> void { result.emplace(std::move(value)); }

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

        auto take_result() noexcept -> Value {
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
    Handle handle;
};
