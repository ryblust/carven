module carven:backend.preparation.async;

import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

struct PreparedAwaitFactory final {
    CallableID callable;
    BodyID body;
    const SemColdCall* returned;
};

// Eligibility and expansion cost depend only on the published program.
// Realization selects module visibility, recursive edges, and its remaining budget.
struct PreparedAwaitProducer final {
    CallableID callable;
    BodyID body;
    std::size_t nodes;
    std::optional<PreparedAwaitFactory> factory;
};

auto prepare_await_producer(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> std::optional<PreparedAwaitProducer>;

// Selected occurrences borrow the callable's published body. Only scalar
// invocation storage is reused; body awaits and terminal exits realize normally.
struct PreparedTailAwaitLoop final {
    std::vector<const SemAwait*> awaits;
};

auto prepare_tail_await_loop(const SemIRProgram& program, CallableID callable) noexcept
    -> std::optional<PreparedTailAwaitLoop>;
