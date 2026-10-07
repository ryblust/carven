import("core.base.json")
import("core.project.config")
import("core.project.project")
import("xmake.benchmarks.report", {rootdir = os.projectdir()})
import("xmake.benchmarks.timings", {rootdir = os.projectdir()})

function array(values)
    return json.mark_as_array(values or {})
end

-- Selection precedes compiler lookup, configuration and builds.
function select(cases, options)
    local selected = {}
    for _, case in ipairs(cases) do
        if not options.case or case.id == options.case then table.insert(selected, case) end
    end
    assert(#selected > 0, "unknown benchmark case: %s", options.case or "<empty selection>")
    if options.list then
        for _, case in ipairs(selected) do
            print("%-32s %-28s %s", case.id, case.label, case.size or "")
        end
        return nil
    end
    return selected
end

local function compiler_identity(compiler, external)
    return {path = compiler,
        build_mode = external and "external (unknown)" or config.mode() or "unknown"}
end

local function failure_output(result)
    local streams = {}
    if result.process_error then table.insert(streams, tostring(result.process_error)) end
    for _, name in ipairs({"stdout", "stderr"}) do
        local output = result[name]
        if output and output:trim() ~= "" then
            table.insert(streams, name .. ":\n" .. output)
        end
    end
    return #streams > 0 and table.concat(streams, "\n") or "no process output"
end

function setup(session)
    local external = session.options.compiler
    if not external then
        print("Preparing compiler...")
        local raw = command(os.programfile(), {"build", "carven"}, os.projectdir(),
            {capture_stdout = true, timeout = 600000})
        raw.operation = "compiler build"
        table.insert(session.results.setup, raw)
        checkpoint(session)
        if session.options.verbose and raw.exit_code == 0 and raw.stdout ~= "" then io.write(raw.stdout) end
        assert(raw.exit_code == 0, "compiler build failed (exit %d): %s", raw.exit_code,
            failure_output(raw))
    end
    config.load()
    session.compiler = path.absolute(external or project.target("carven"):targetfile(), os.projectdir())
    if session.output then
        session.results.metadata.compiler = compiler_identity(session.compiler, external)
        checkpoint(session)
    end
    assert(os.isfile(session.compiler), "Carven compiler does not exist: %s; build it first", session.compiler)
    session.compiler_mode = external and "external (unknown)" or config.mode() or "unknown"
end

function median(samples)
    if #samples == 0 then return nil end
    local sorted = table.copy(samples)
    table.sort(sorted)
    local middle = math.floor(#sorted / 2)
    return #sorted % 2 == 1 and sorted[middle + 1] or (sorted[middle] + sorted[middle + 1]) / 2
end

function summary(runs)
    local measured, metrics = {}, {}
    for _, run in ipairs(runs) do
        if not run.warmup and run.exit_code == 0 and not run.validation_error then
            table.insert(measured, run.wall_ms)
            for key, value in pairs(run.metrics or {}) do
                local metric = metrics[key] or {samples = {}}
                table.insert(metric.samples, value)
                metrics[key] = metric
            end
        end
    end
    table.sort(measured)
    for _, metric in pairs(metrics) do
        table.sort(metric.samples)
        metric.count = #metric.samples
        metric.minimum, metric.maximum = metric.samples[1], metric.samples[#metric.samples]
        metric.median = median(metric.samples)
        metric.samples = nil
    end
    return {measured_runs = #measured, median_wall_ms = median(measured),
        minimum_wall_ms = measured[1], maximum_wall_ms = measured[#measured], metrics = metrics}
end

function revision(root)
    local commit = try {function () return os.iorunv("git", {"rev-parse", "HEAD"}, {curdir = root}) end}
    local changes = try {function () return os.iorunv("git", {"status", "--porcelain"}, {curdir = root}) end}
    return {revision = commit and commit:trim() or "unavailable",
        dirty = changes and (#changes > 0 and "dirty" or "clean") or "unknown",
        changes = changes or "unavailable"}
end

local function metadata(compiler, options)
    local checkout_crafts_root = not options.compiler
        and path.join(os.projectdir(), "crafts", "carven") or nil
    return {
        compiler = compiler_identity(compiler, options.compiler),
        argv = array(table.copy(xmake.argv())), effective_options = table.copy(options),
        cwd = os.curdir(), harness = revision(os.projectdir()),
        xmake = {path = os.programfile(), version = tostring(xmake.version())},
        host = os.host(), arch = os.arch(), started_at = os.date("!%Y-%m-%dT%H:%M:%SZ"),
        timings_enabled = options.timings or false,
        checkout_crafts_root = checkout_crafts_root,
    }
end

function output_path(topic, options)
    local output = options.output or path.join(os.projectdir(), "build", "benchmarks", topic,
        os.date("!%Y%m%dT%H%M%SZ") .. "-" .. path.filename(os.tmpfile()) .. ".json")
    output = path.absolute(output, os.projectdir())
    local artifacts = path.join(os.projectdir(), "build", "benchmarks", topic,
        path.basename(output) .. "-artifacts")
    assert(not os.isfile(output) and not os.isdir(artifacts),
        "benchmark output exists; choose a fresh --output path: %s", output)
    return output, artifacts
end

function session(topic, options, specification)
    specification = specification or {}
    local output, artifacts = output_path(topic, options)
    local result = {options = options, samples = options.samples, warmups = options.warmups,
        description = specification.description or {}, columns = specification.columns or {}, primary = specification.primary,
        output = output, artifacts = artifacts,
        results = {schema_version = 2, topic = topic, status = "running", cases = array(),
            measurement = {description = array(table.copy(specification.description or {})),
                columns = array(table.copy(specification.columns or {})), primary = specification.primary},
            setup = array()}}
    if output then
        local external = options.compiler
        result.results.metadata = metadata(external and path.absolute(external, os.projectdir()) or nil, options)
        result.results.metadata.sampling = {samples = result.samples, warmups = result.warmups}
    end
    result.results.metadata.artifacts = artifacts
    checkpoint(result)
    return result
end

function checkpoint(session)
    if session.output then
        os.mkdir(path.directory(session.output))
        json.savefile(session.output, session.results)
    end
end

function new_case(session, specification)
    local record = table.copy(specification)
    record.runs, record.steps, record.status = array(), array(), "running"
    table.insert(session.results.cases, record)
    checkpoint(session)
    return record
end

-- Wall time excludes retained-output reads and JSON serialization. Inspection
-- C++ remains discarded even when the benchmark retains its results.
function command(program, argv, cwd, options)
    options = options or {}
    if program == os.programfile() and path.absolute(cwd) ~= os.projectdir() then
        argv = table.copy(argv)
        table.insert(argv, 2, cwd)
        table.insert(argv, 2, "-P")
    end
    local stderr_file = os.tmpfile()
    local stdout_file = options.capture_stdout and os.tmpfile() or nil
    local started = os.mclock()
    local status, errors = os.execv(program, argv, {curdir = cwd,
        stdout = stdout_file or os.nuldev(), stderr = stderr_file,
        timeout = options.timeout or 60000, envs = options.envs, try = true})
    local elapsed = os.mclock() - started
    local result = {wall_ms = elapsed, exit_code = status == nil and -1 or status,
        stdout_mode = stdout_file and "captured" or "discarded",
        stderr = io.readfile(stderr_file) or "",
        command = {program = program, argv = array(table.copy(argv)), cwd = cwd}}
    if stdout_file then result.stdout = io.readfile(stdout_file) or "" end
    if errors then result.process_error = tostring(errors) end
    os.tryrm(stderr_file)
    if stdout_file then os.tryrm(stdout_file) end
    if options.timings then
        result.timings = timings.parse(result.stderr)
        result.timings.reports = array(result.timings.reports)
        for _, report in ipairs(result.timings.reports) do report.stages = array(report.stages) end
    end
    return result
end

function step(session, case, label, program, argv, cwd, options)
    options = table.copy(options or {})
    options.timings = session.options.timings
    local result = command(program, argv, cwd, options)
    result.operation = label
    table.insert(case.steps, result)
    checkpoint(session)
    assert(result.exit_code == 0, "%s: %s failed (exit %d): %s", case.id, label,
        result.exit_code, failure_output(result))
    return result
end

function measure_rounds(session, records, action)
    local random = 1729
    for ordinal = 1, session.warmups + session.samples do
        local order = table.copy(records)
        for index = #order, 2, -1 do
            random = (random * 1664525 + 1013904223) % 4294967296
            local selected = random % index + 1
            order[index], order[selected] = order[selected], order[index]
        end
        for position, case in ipairs(order) do
            local result = action(case, ordinal)
            result.ordinal, result.order = ordinal, position
            result.warmup = ordinal <= session.warmups
            table.insert(case.runs, result)
            case.summary = summary(case.runs)
            if result.exit_code ~= 0 or result.validation_error then
                case.status, session.results.status = "failed", "failed"
            end
            checkpoint(session)
            report.sample(session, case, result)
            if result.validation_error then raise("%s: %s", case.id, result.validation_error) end
            assert(result.exit_code == 0, "%s: measured command failed (exit %d): %s", case.id,
                result.exit_code, failure_output(result))
        end
    end
    for _, case in ipairs(records) do case.status = "complete" end
    checkpoint(session)
end

function measure_case(session, case, action)
    measure_rounds(session, {case}, function (_, ordinal) return action(ordinal) end)
    return case.summary
end

function perform(session, action)
    local failure
    try {action, catch {function (errors) failure = errors end}}
    session.results.status = failure and "failed" or "complete"
    if failure then
        session.results.error = tostring(failure)
        for _, case in ipairs(session.results.cases) do
            if case.status == "running" then case.status = "failed" end
        end
    end
    checkpoint(session)
    report.summary(session)
    if failure then raise(failure) end
end

-- Workloads supply setup and the measured action. Sampling, persistence and
-- reporting have one lifecycle regardless of the command being measured.
function run(session, cases, prepare, configure)
    perform(session, function ()
        setup(session)
        if configure then configure(session) end
        report.header(session)
        for index, specification in ipairs(cases) do
            local record = new_case(session, {id = specification.id, label = specification.label,
                size = specification.size, group = specification.group})
            report.begin_case(record, index, #cases)
            local root = path.join(session.artifacts, specification.id)
            os.mkdir(root)
            record.workspace = root
            local action = prepare(session, record, specification, root)
            checkpoint(session)
            measure_case(session, record, action)
            report.finish_case(record, session)
        end
    end)
end
