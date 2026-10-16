import("core.base.json")
import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.harness.report", {rootdir = os.projectdir()})
import("benchmarks.harness.calibration", {rootdir = os.projectdir()})
import("benchmarks.harness.inputs", {alias = "input_snapshot", rootdir = os.projectdir()})
import("benchmarks.simd.workloads", {rootdir = os.projectdir()})

local function observe(target, scenario, iterations, timed, expected)
    local raw = benchmark.command(target.executable, {}, path.directory(target.executable), {
        target = target.name, capture_stdout = true,
        envs = {CARVEN_SIMD_BENCH_LENGTH = tostring(scenario.length),
            CARVEN_SIMD_BENCH_DISTRIBUTION = scenario.distribution,
            CARVEN_SIMD_BENCH_ITERATIONS = tostring(iterations)}})
    if raw.exit_code ~= 0 then return raw end
    local metrics, error = json.decode(raw.stdout)
    if not metrics then raw.validation_error = "invalid metrics: " .. tostring(error)
    elseif type(metrics.elapsed_ns) ~= "number" or metrics.elapsed_ns < 0 or (timed and metrics.elapsed_ns == 0) then
        raw.validation_error = "invalid timed interval"
    elseif metrics.iterations ~= iterations or metrics.checksum ~= metrics.expected * iterations then
        raw.validation_error = "checksum differs from validated scan result"
    elseif expected ~= nil and metrics.expected ~= expected then
        raw.validation_error = "input result changed across observations"
    else
        raw.metrics = {elapsed_ns = metrics.elapsed_ns, ns_per_scan = metrics.elapsed_ns / iterations,
            checksum = metrics.checksum, expected = metrics.expected, iterations = metrics.iterations}
        raw.runtime_backend = metrics.runtime_backend
        raw.below_calibration_target = calibration.below_target(metrics.elapsed_ns)
    end
    return raw
end

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    local session = benchmark.session("simd", options, {
        description = {"Generated SIMD scans versus ordinary optimized C++ byte loops; C++20, separate kernel translation units, no LTO.",
            "ns/scan includes the kernel call and checksum accumulation; excludes input allocation, validation and output.",
            "Input views are unaligned. Search starts at one and returns absolute positions. Count uses the entire view.",
            "Each sample validates its kernel against an independent membership table before timing; implementations use shuffled rounds.",
            "Each scenario and implementation calibrates to at least 20 ms; the byte-work budget sets its initial minimum iteration count."},
        primary = {key = "ns_per_scan", unit = "ns/scan"}, columns = {},
    })
    benchmark.perform(session, function ()
        benchmark.setup(session)
        session.results.metadata.calibration_target_ns = calibration.target_ns()
        session.results.metadata.backend_selection = options.portable and "forced portable" or "target default"
        input_snapshot.capture(session)
        report.header(session)
        local root = os.projectdir()
        local setup = {id = "programs", steps = benchmark.array()}
        table.insert(session.results.setup, setup)
        local variant = options.portable and "portable" or "native"
        local group = "benchmarks/simd/" .. variant .. "/" .. (options.case or "*")
        benchmark.step(session, setup, "build kernels", os.programfile(), {"build", "-g", group}, root,
            {capture_stdout = true, timeout = 180000})
        local names = {}
        for _, case in ipairs(selected) do
            table.insert(names, case.id .. "-carven")
            table.insert(names, case.id .. "-byte_loop")
        end
        local targets = import("benchmarks.harness.targets", {rootdir = root})
            .main("benchmarks::simd::" .. variant, names)
        setup.native_targets = targets
        for index, case in ipairs(selected) do
            report.begin_case(case, index, #selected)
            local records = {}
            for _, scenario in ipairs(workloads.scenarios(case)) do
                local iterations = math.max(128, math.floor(options.iterations * 32 / scenario.length))
                local expected
                for _, implementation in ipairs({"carven", "byte_loop"}) do
                    local record = benchmark.new_case(session, {
                        id = case.id .. "/" .. scenario.length .. "/" .. scenario.distribution .. "/" .. implementation,
                        label = case.label .. " " .. implementation, size = tostring(scenario.length),
                        workload = case.id, implementation = implementation, scenario = scenario,
                        initial_iterations = iterations, workspace = root})
                    local target = targets[case.id .. "-" .. implementation]
                    record.validation = observe(target, scenario, 1, false, nil)
                    local raw = record.validation
                    benchmark.checkpoint(session)
                    assert(raw.exit_code == 0 and not raw.validation_error, "kernel validation failed: %s",
                        raw.validation_error or ("exit " .. tostring(raw.exit_code) .. ": " .. raw.stderr))
                    assert(expected == nil or expected == raw.metrics.expected, "implementations disagree")
                    expected = raw.metrics.expected
                    record.expected = expected
                    record.iterations = calibration.run(session, record, function (count)
                        return observe(target, scenario, count, false, record.expected)
                    end, iterations, calibration.iteration_limit(record.expected))
                    table.insert(records, record)
                end
            end
            benchmark.measure_rounds(session, records, function (record)
                return observe(targets[case.id .. "-" .. record.implementation], record.scenario,
                    record.iterations, true, record.expected)
            end)
        end
    end)
end
