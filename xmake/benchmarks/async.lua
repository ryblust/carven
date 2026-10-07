import("core.base.json")
import("xmake.benchmarks.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("xmake.benchmarks.report", {rootdir = os.projectdir()})
import("xmake.benchmarks.async-native", {alias = "native", rootdir = os.projectdir()})
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

local function snapshot(session, options)
    local root = path.join(session.artifacts, "inputs")
    local manifest = {}
    local function capture(directory, prefix)
        for _, file in ipairs(os.files(path.join(directory, "**"))) do
            local name = path.join(prefix, path.relative(file, directory))
            local destination = path.join(root, name)
            os.mkdir(path.directory(destination))
            os.cp(file, destination)
            manifest[path.relative(destination, session.artifacts)] = {sha256 = hash.sha256(destination):lower(), bytes = os.filesize(destination)}
        end
    end
    capture(path.join(os.projectdir(), "crafts", "carven", "runtime"), "crafts/carven/runtime")
    capture(path.join(os.projectdir(), "benchmarks", "async"), "benchmarks/async")
    capture(path.join(os.projectdir(), "xmake", "benchmarks"), "xmake/benchmarks")
    if options.libcoro then capture(path.join(options.libcoro, "include"), "libcoro/include") end
    session.results.metadata.native_sources = {directory = session.artifacts, files = manifest}
    return root
end

local function observe(executable, case, count, seed, depth, stack)
    local raw = benchmark.command(executable, {}, path.directory(executable), {
        capture_stdout = true, timeout = 60000,
        envs = {CARVEN_ASYNC_BENCH_ITERATIONS = tostring(count), CARVEN_ASYNC_BENCH_SEED = tostring(seed),
            CARVEN_ASYNC_BENCH_DEPTH = tostring(depth), CARVEN_ASYNC_BENCH_STACK = stack and "1" or "0"},
    })
    raw.iterations, raw.seed, raw.depth = count, seed, depth
    if raw.exit_code ~= 0 then return raw end
    local metrics, errors = json.decode(raw.stdout)
    if not metrics then raw.validation_error = "invalid metrics: " .. tostring(errors)
    elseif metrics.checksum ~= checksum(case, count, seed, depth) then
        raw.validation_error = "checksum differs from the workload contract"
    elseif type(metrics.elapsed_ns) ~= "number" or metrics.elapsed_ns <= 0 then
        raw.validation_error = "nonpositive timed interval"
    else
        raw.metrics = metrics
        metrics.ns_per_iteration = metrics.elapsed_ns / count
    end
    return raw
end

local function validate(raw)
    assert(raw.exit_code == 0 and not raw.validation_error, "native workload failed: %s",
        raw.validation_error or raw.stderr or raw.process_error or tostring(raw.exit_code))
end

local function prepare(session, case, implementations, inputs)
    local root = path.join(session.artifacts, case.id)
    local generated = path.join(root, "generated")
    os.mkdir(generated)
    io.writefile(path.join(root, "workload.cv"), workloads.source(case))
    local setup = {id = case.id, label = case.label, steps = benchmark.array()}
    table.insert(session.results.setup, setup)
    benchmark.step(session, setup, "generate", session.compiler,
        {"compile", "workload.cv", "-o", generated, "--linkage-domain=benchmark:async:" .. case.id}, root,
        {capture_stdout = true, timeout = 180000})
    io.writefile(path.join(root, "xmake.lua"), native.project_text(workloads.defines(case), implementations, root, inputs, session.options.cxx))
    local arguments = {"f", "-y", "-m", "release"}
    benchmark.step(session, setup, "native configure", os.programfile(), arguments, root,
        {capture_stdout = true, timeout = 180000})
    local identity = benchmark.step(session, setup, "native target identity", os.programfile(),
        {"lua", "-c", native.describe_script()}, root, {capture_stdout = true})
    local targets, errors = json.decode(identity.stdout)
    assert(targets, "invalid native target identity: %s", errors)
    setup.native_targets = targets
    -- Configuration and build observations belong to setup, not timing rows.
    local records = {}
    for _, implementation in ipairs(implementations) do
        local record = benchmark.new_case(session, {id = case.id .. "/" .. implementation,
            label = case.label, size = "dynamic inputs", workload = case.id, implementation = implementation})
        record.workspace = root
        for _, profile in ipairs({"timing", "allocations"}) do
            benchmark.step(session, record, "build " .. profile, os.programfile(),
                {"build", implementation .. "-" .. profile}, root, {capture_stdout = true, timeout = 180000})
        end
        if implementation == "carven" then
            benchmark.step(session, record, "build without exceptions", os.programfile(),
                {"build", "carven-noexceptions"}, root, {capture_stdout = true, timeout = 180000})
            record.noexceptions = observe(targets["carven-noexceptions"].executable,
                case, 3, session.options.seed, session.options.depth)
            validate(record.noexceptions)
        end
        table.insert(records, record)
    end
    return records, targets
end

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    assert(not options.timings, "async measures executed programs; --timings is for compiler benchmarks")
    if options.cxx and (options.cxx:find("/", 1, true) or options.cxx:find("\\", 1, true)) then
        options.cxx = path.absolute(options.cxx, os.projectdir())
    end
    if options.libcoro then
        options.libcoro = path.absolute(options.libcoro, os.projectdir())
        assert(os.isfile(path.join(options.libcoro, "include", "coro", "task.hpp")),
            "--libcoro must point to an external libcoro checkout")
    end
    local implementations = {"carven", "handwritten", "handwritten_runtime"}
    if options.libcoro then table.insert(implementations, "libcoro") end
    local session = benchmark.session("async", options, {
        description = {"Native C++20 kernels; timed interval is inside the root body, excluding root construction and output.",
            "ns/work measures one short iteration or a whole chain; process wall time is a separate observation.",
            "Allocation counts come from two separate new/new[] instrumented runs; exclude malloc and root construction.",
            "Implementations use interleaved shuffled rounds; independent tasks have narrower contracts than Carven."},
        primary = {key = "ns_per_iteration", unit = "ns/work"},
        columns = {{key = "counted_allocations", label = "Counted new/work", statistic = "median"}},
    })
    benchmark.perform(session, function ()
        benchmark.setup(session)
        session.results.metadata.compiler.sha256 = hash.sha256(session.compiler):lower()
        session.results.metadata.order_seed = 1729
        session.results.metadata.implementations = benchmark.array(implementations)
        if options.libcoro then session.results.metadata.libcoro = benchmark.revision(options.libcoro) end
        local inputs = snapshot(session, options)
        report.header(session)
        for index, case in ipairs(selected) do
            case.size = case.id == "chain" and (options.depth .. " levels") or (options.iterations .. " iterations")
            report.begin_case(case, index, #selected)
            local records, targets = prepare(session, case, implementations, inputs)
            local count = case.id == "chain" and math.max(1, math.floor(options.iterations / options.depth))
                or options.iterations
            for _, record in ipairs(records) do
                record.iterations, record.depth = count, options.depth
                record.expected_checksum = checksum(case, count, options.seed, options.depth)
                record.allocation_runs = benchmark.array()
                for _ = 1, 2 do
                    local raw = observe(targets[record.implementation .. "-allocations"].executable,
                        case, count, options.seed, options.depth)
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
                local raw = observe(targets[record.implementation .. "-timing"].executable,
                    case, count, options.seed, options.depth)
                if raw.metrics then
                    raw.metrics.counted_allocations = record.allocation_runs[1].metrics.allocations / count
                end
                return raw
            end)
            if case.id == "chain" then
                for _, record in ipairs(records) do
                    benchmark.step(session, record, "build unoptimized stack probe", os.programfile(),
                        {"build", record.implementation .. "-stack"}, record.workspace,
                        {capture_stdout = true, timeout = 180000})
                    record.stack_checks = benchmark.array()
                    for _, depth in ipairs({32, 512, options["stack-depth"]}) do
                        local raw = observe(targets[record.implementation .. "-stack"].executable,
                            case, 1, options.seed, depth, true)
                        table.insert(record.stack_checks, raw)
                        benchmark.checkpoint(session)
                        if raw.exit_code == 0 then validate(raw)
                        elseif record.implementation == "carven" then validate(raw) end
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
        -- Native inputs and generated code remain in the build workspace with
        -- content identities. No external archiver is required to inspect them.
        local manifest = session.results.metadata.native_sources.files
        for _, file in ipairs(os.files(path.join(session.artifacts, "**"))) do
            local extension = path.extension(file)
            if extension == ".cpp" or extension == ".hpp" or extension == ".cv" or extension == ".lua" then
                local name = path.relative(file, session.artifacts)
                local digest = hash.sha256(file):lower()
                assert(not manifest[name] or manifest[name].sha256 == digest, "native input changed: %s", name)
                manifest[name] = {sha256 = digest, bytes = os.filesize(file)}
            end
        end
        local file = path.join(session.artifacts, "native-sources.json")
        json.savefile(file, manifest)
        session.results.metadata.native_sources.manifest = file
    end)
end
