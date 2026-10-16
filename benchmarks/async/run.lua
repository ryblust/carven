import("core.base.json")
import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.harness.report", {rootdir = os.projectdir()})
import("benchmarks.harness.calibration", {rootdir = os.projectdir()})
import("benchmarks.harness.inputs", {alias = "input_snapshot", rootdir = os.projectdir()})
import("benchmarks.async.workloads", {rootdir = os.projectdir()})

local modulus = 1048576

local function checksum(case, count, seed, depth)
    local cycles, remainder = math.floor(count / modulus), count % modulus
    local first = seed % modulus
    local length = math.min(remainder, modulus - first)
    local rest = remainder - length
    local delta = case.id == "chain" and depth or 1
    local value = cycles * modulus * (modulus - 1) / 2
        + (2 * first + length - 1) * length / 2 + rest * (rest - 1) / 2 + delta * count
    return case.id == "stored" and value * 2 or value
end

local function observe(target, case, count, seed, depth)
    local raw = benchmark.command(target.executable, {}, path.directory(target.executable), {
        target = target.name,
        capture_stdout = true, timeout = 60000,
        envs = {CARVEN_ASYNC_BENCH_ITERATIONS = tostring(count), CARVEN_ASYNC_BENCH_SEED = tostring(seed),
            CARVEN_ASYNC_BENCH_DEPTH = tostring(depth)},
    })
    raw.iterations, raw.seed, raw.depth = count, seed, depth
    if raw.exit_code ~= 0 then return raw end
    local metrics, errors = json.decode(raw.stdout)
    if not metrics then raw.validation_error = "invalid metrics: " .. tostring(errors)
    elseif metrics.checksum ~= checksum(case, count, seed, depth) then
        raw.validation_error = "checksum differs from the workload contract"
    elseif type(metrics.elapsed_ns) ~= "number" or metrics.elapsed_ns < 0 then
        raw.validation_error = "invalid timed interval"
    else
        raw.metrics = metrics
        metrics.ns_per_iteration = metrics.elapsed_ns / count
        raw.short_interval = metrics.elapsed_ns < 1000000
        raw.below_calibration_target = calibration.below_target(metrics.elapsed_ns)
    end
    return raw
end

local function validate(raw)
    assert(raw.exit_code == 0 and not raw.validation_error, "native workload failed: %s",
        raw.validation_error or raw.stderr or raw.process_error or tostring(raw.exit_code))
end

local function prepare(session, selected, implementations)
    local root = os.projectdir()
    local setup = {id = "programs", steps = benchmark.array()}
    table.insert(session.results.setup, setup)
    local group = "benchmarks/async/" .. (session.options.case or "*")
    benchmark.step(session, setup, "build measurement programs", os.programfile(),
        {"build", "-g", group}, root, {capture_stdout = true, timeout = 180000})
    local names = {}
    for _, case in ipairs(selected) do
        for _, implementation in ipairs(implementations) do
            local prefix = case.id .. "-" .. implementation .. "-"
            table.insert(names, prefix .. "timing")
            table.insert(names, prefix .. "allocations")
            if case.chain then table.insert(names, prefix .. "stack") end
        end
    end
    local targets = import("benchmarks.harness.targets", {rootdir = root})
        .main("benchmarks::async", names)
    setup.native_targets = targets
    return root, targets
end

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    local config = import("core.project.config")
    config.load()
    options.libcoro = config.get("libcoro")
    if options.libcoro == "" then options.libcoro = nil end
    if options.libcoro then options.libcoro = path.absolute(options.libcoro, os.projectdir()) end
    local implementations = {"carven", "cpp_task", "carven_runtime"}
    if options.libcoro then table.insert(implementations, "libcoro") end
    local session = benchmark.session("async", options, {
        description = {"Native C++20 kernels; timed interval is inside the root body, excluding root construction and output.",
            "ns/work measures one short iteration or a whole arithmetic chain that native optimization may collapse; process wall time is separate.",
            "Raw observations flag intervals below 1 ms; those intervals do not support fine performance comparisons.",
            "Timing work calibrates to at least 20 ms before warmups; allocation runs retain the initial work budget.",
            "Allocation counts come from two separate new/new[] instrumented runs; exclude malloc and root construction.",
            "Implementations use interleaved shuffled rounds; independent tasks have narrower contracts than Carven."},
        primary = {key = "ns_per_iteration", unit = "ns/work"},
        columns = {{key = "counted_allocations", label = "Counted new/work", statistic = "median"}},
    })
    benchmark.perform(session, function ()
        benchmark.setup(session)
        session.results.metadata.calibration_target_ns = calibration.target_ns()
        session.results.metadata.implementations = benchmark.array(implementations)
        if options.libcoro then session.results.metadata.libcoro = benchmark.revision(options.libcoro) end
        input_snapshot.capture(session)
        report.header(session)
        local root, targets = prepare(session, selected, implementations)
        for index, case in ipairs(selected) do
            case.size = case.id == "chain" and (options.depth .. " levels") or (options.iterations .. " iterations")
            report.begin_case(case, index, #selected)
            local records = {}
            for _, implementation in ipairs(implementations) do
                local record = benchmark.new_case(session, {id = case.id .. "/" .. implementation,
                    label = case.label, size = "dynamic inputs", workload = case.id, implementation = implementation})
                record.workspace = root
                table.insert(records, record)
            end
            local count = case.id == "chain" and math.max(1, math.floor(options.iterations / options.depth))
                or options.iterations
            for _, record in ipairs(records) do
                record.initial_iterations, record.allocation_iterations, record.depth = count, count, options.depth
                local maximum_result = (modulus - 1 + (case.id == "chain" and options.depth or 1))
                    * (case.id == "stored" and 2 or 1)
                record.iterations = calibration.run(session, record, function (iterations)
                    return observe(targets[case.id .. "-" .. record.implementation .. "-timing"],
                        case, iterations, options.seed, options.depth)
                end, count, calibration.iteration_limit(maximum_result))
                record.expected_checksum = checksum(case, record.iterations, options.seed, options.depth)
                record.allocation_runs = benchmark.array()
                for _ = 1, 2 do
                    local raw = observe(targets[case.id .. "-" .. record.implementation .. "-allocations"],
                        case, record.allocation_iterations, options.seed, options.depth)
                    table.insert(record.allocation_runs, raw)
                    benchmark.checkpoint(session)
                    validate(raw)
                    assert(raw.metrics.remaining_live_bytes == 0, "counted storage remains after full-expression cleanup")
                end
                for _, key in ipairs({"allocations", "allocated_bytes", "peak_live_bytes"}) do
                    assert(record.allocation_runs[1].metrics[key] == record.allocation_runs[2].metrics[key],
                        "unstable counted metric: %s", key)
                end
            end
            benchmark.measure_rounds(session, records, function (record)
                local raw = observe(targets[case.id .. "-" .. record.implementation .. "-timing"],
                    case, record.iterations, options.seed, options.depth)
                if raw.metrics then
                    if raw.metrics.elapsed_ns == 0 then raw.validation_error = "nonpositive timed sample" end
                    raw.metrics.counted_allocations = record.allocation_runs[1].metrics.allocations
                        / record.allocation_iterations
                end
                return raw
            end)
            if case.id == "chain" then
                for _, record in ipairs(records) do
                    record.stack_checks = benchmark.array()
                    for _, depth in ipairs({32, 512, options["stack-depth"]}) do
                        local raw = observe(targets[case.id .. "-" .. record.implementation .. "-stack"],
                            case, 1, options.seed, depth)
                        table.insert(record.stack_checks, raw)
                        benchmark.checkpoint(session)
                        validate(raw)
                    end
                    if record.implementation == "carven" then
                        local spans = {}
                        for _, raw in ipairs(record.stack_checks) do table.insert(spans, raw.metrics.stack_span_bytes) end
                        assert(math.max(table.unpack(spans)) <= math.min(table.unpack(spans)) + 4096,
                            "native stack grows with chain depth")
                    end
                end
            end
            for _, record in ipairs(records) do report.finish_case(record, session) end
        end
    end)
end
