module carven:semantic.evaluation.limits;

import std;

inline constexpr auto maximum_constant_steps = 100'000uz;
inline constexpr auto maximum_constant_depth = 128uz;
inline constexpr auto maximum_constant_text_bytes = 1024uz * 1024uz;
inline constexpr auto maximum_constant_aggregate_elements = 65'536uz;
inline constexpr auto maximum_constant_aggregate_depth = 64uz;
inline constexpr auto maximum_constant_aggregate_work = 8uz * maximum_constant_aggregate_elements;

// Cumulative work available to one root and all of its calls.
// Per-value size, aggregate depth, and call depth have fixed limits.
struct ExecutionLimits final {
    std::size_t steps;
    std::size_t text_work;
    std::size_t aggregate_work;
};

auto constant_execution_limits() noexcept -> ExecutionLimits {
    return {
        .steps = maximum_constant_steps,
        .text_work = 8uz * maximum_constant_text_bytes,
        .aggregate_work = maximum_constant_aggregate_work
    };
}
