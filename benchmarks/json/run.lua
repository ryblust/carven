import("core.base.json")
import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.harness.report", {rootdir = os.projectdir()})
import("benchmarks.harness.calibration", {rootdir = os.projectdir()})
import("benchmarks.harness.inputs", {alias = "input_snapshot", rootdir = os.projectdir()})
import("benchmarks.json.workloads", {rootdir = os.projectdir()})

local function observe(target, record, iterations, timed)
    local raw = benchmark.command(target.executable, {}, path.directory(target.executable), {
        target = target.name, capture_stdout = true,
        envs = {CARVEN_JSON_BENCH_INPUT = record.input.path, CARVEN_JSON_BENCH_ITERATIONS = tostring(iterations),
            CARVEN_JSON_BENCH_EXPECTED = tostring(record.expected)}})
    if raw.exit_code ~= 0 then return raw end
    local metrics, error = json.decode(raw.stdout)
    if not metrics then raw.validation_error = "invalid metrics: " .. tostring(error)
    elseif metrics.iterations ~= iterations or metrics.bytes ~= record.input.bytes or metrics.expected ~= record.expected
        or metrics.checksum ~= record.expected * iterations then
        raw.validation_error = "result differs from the document outcome contract"
    elseif type(metrics.elapsed_ns) ~= "number" or metrics.elapsed_ns < 0 or (timed and metrics.elapsed_ns == 0) then
        raw.validation_error = "invalid measured interval"
    else
        raw.metrics = metrics
        raw.metrics.ns_per_document = metrics.elapsed_ns / iterations
        if metrics.elapsed_ns > 0 then
            raw.metrics.mb_per_second = record.input.bytes * iterations * 1000 / metrics.elapsed_ns
        end
        raw.below_calibration_target = calibration.below_target(metrics.elapsed_ns)
    end
    return raw
end

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    local session = benchmark.session("json", options, {
        description = {"Standard validate_bytes on API-shaped JSON documents, including UTF-8 and syntax validation.",
            "ns/document includes the exported call and outcome checksum; input creation, file I/O and correctness checks are outside timing.",
            "Documents are reused in memory. Each outcome calibrates to at least 20 ms before warmups and shuffled samples."},
        primary = {key = "ns_per_document", unit = "ns/document"},
        columns = {{key = "mb_per_second", label = "Input MB/s", statistic = "median"}},
    })
    benchmark.perform(session, function ()
        benchmark.setup(session)
        session.results.metadata.calibration_target_ns = calibration.target_ns()
        session.results.metadata.backend_selection = options.portable and "forced portable" or "target default"
        input_snapshot.capture(session)
        local root = os.projectdir()
        local setup = {id = "validation", steps = benchmark.array()}
        table.insert(session.results.setup, setup)
        local variant = options.portable and "portable" or "native"
        benchmark.step(session, setup, "build validation", os.programfile(),
            {"build", "-g", "benchmarks/json/" .. variant}, root,
            {capture_stdout = true, timeout = 180000})
        local targets = import("benchmarks.harness.targets", {rootdir = root})
            .main("benchmarks::json::" .. variant, {"validation"})
        setup.native_targets = targets
        local target = targets.validation
        local records = {}
        report.header(session)
        for index, case in ipairs(selected) do
            report.begin_case(case, index, #selected)
            for _, document in ipairs(workloads.documents(case)) do
                local file = path.join(session.artifacts, case.id .. "_" .. document.id .. ".json")
                io.writefile(file, document.text)
                local high_bytes = 0
                for byte in document.text:gmatch(".") do
                    if byte:byte() >= 128 then high_bytes = high_bytes + 1 end
                end
                local record = benchmark.new_case(session, {id = case.id .. "/" .. document.id, label = case.label,
                    size = #document.text .. " bytes", workload = case.id, implementation = "carven",
                    outcome = document.outcome, error_offset = document.offset, expected = document.expected,
                    input = {path = file, bytes = #document.text, sha256 = hash.sha256(file):lower(), high_bytes = high_bytes},
                    records = case.records, workspace = root})
                record.iterations = calibration.run(session, record, function (iterations)
                    return observe(target, record, iterations, false)
                end, 128, calibration.iteration_limit(record.expected))
                table.insert(records, record)
            end
        end
        benchmark.measure_rounds(session, records, function (record)
            return observe(target, record, record.iterations, true)
        end)
        for _, record in ipairs(records) do
            assert(hash.sha256(record.input.path):lower() == record.input.sha256, "JSON input changed during measurement")
            report.finish_case(record, session)
        end
    end)
end
