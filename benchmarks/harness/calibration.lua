import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})

local interval_ns = 20000000
local maximum_iterations = 1000000000
local exact_integer_limit = 9007199254740991

function target_ns()
    return interval_ns
end

function below_target(elapsed_ns)
    return elapsed_ns < interval_ns
end

function iteration_limit(maximum_result)
    return math.min(maximum_iterations,
        math.floor(exact_integer_limit / math.max(1, maximum_result)))
end

function run(session, record, observe, initial_iterations, limit)
    assert(initial_iterations <= limit,
        "%s: initial work budget exceeds the iteration or exact checksum limit", record.id)
    local iterations = initial_iterations
    record.calibration = benchmark.array()
    while true do
        local raw = observe(iterations)
        table.insert(record.calibration, raw)
        benchmark.checkpoint(session)
        assert(raw.exit_code == 0 and not raw.validation_error, "%s: calibration failed: %s", record.id,
            raw.validation_error or raw.process_error or raw.stderr or tostring(raw.exit_code))
        assert(raw.metrics, "%s: calibration produced no metrics", record.id)
        local elapsed_ns = raw.metrics.elapsed_ns
        assert(type(elapsed_ns) == "number" and elapsed_ns >= 0,
            "%s: calibration requires a nonnegative measured interval", record.id)
        if elapsed_ns >= interval_ns then return iterations end
        assert(iterations < limit,
            "%s: calibration did not reach 20 ms within the iteration or exact checksum limit", record.id)
        local next_iterations = elapsed_ns == 0 and iterations * 10
            or math.ceil(iterations * interval_ns * 1.25 / elapsed_ns)
        iterations = math.min(limit, math.max(iterations + 1, next_iterations))
    end
end
