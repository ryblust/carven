import("core.base.text")

local function flush()
    io.flush()
end

local function print_table(rows, indent)
    rows.align = "r"
    rows.sep = "  "
    rows[1].style = "${bright}"
    for _, row in ipairs(rows) do
        row[1] = {(indent or "") .. row[1], align = "l"}
    end
    io.write(text.table(rows, {plain = not io.isatty()}))
end

local function decimal(value)
    return value ~= nil and string.format("%.2f", value) or "-"
end

local function failure(run)
    local reasons = {}
    if run.validation_error then table.insert(reasons, "validation failed") end
    if run.exit_code ~= 0 then table.insert(reasons, "exit " .. tostring(run.exit_code)) end
    return #reasons > 0 and table.concat(reasons, "; ") or nil
end

local function stage_timings(run)
    local reports = run.timings and run.timings.reports or {}
    if #reports == 0 then print("    Carven timings: no report observed") end
    for index, report in ipairs(reports) do
        print("    Carven invocation %d/%d: %s in %s", index, #reports, report.label, report.total.raw)
        local rows = {{"Stage", "Time", "% total"}}
        for _, stage in ipairs(report.stages) do
            -- Rounded durations and upper bounds cannot supply exact shares.
            table.insert(rows, {stage.label, stage.duration.raw, stage.share_raw or "-"})
        end
        print_table(rows, "      ")
    end
end

function header(session)
    local options = session.options
    local topic = session.results.topic:gsub("^%l", string.upper)
    print("\n%s benchmark", topic)
    print("  Compiler: %s", session.compiler)
    print("  Mode:     %s", options.compiler and "external (unknown)" or session.compiler_mode)
    print("  Sampling: %d warmup%s, %d measured sample%s; %s",
        session.warmups, session.warmups == 1 and "" or "s",
        session.samples, session.samples == 1 and "" or "s",
        session.primary and session.primary.unit or "process wall time in ms")
    if options.timings then
        print("  Timings:  %s", options.verbose and "each sample and warmup"
            or "sample nearest the median wall time")
    end
    if #session.description > 0 then
        print("  Measurement:")
        for _, line in ipairs(session.description) do print("    %s", line) end
    end
    flush()
end

function begin_case(record, index, total)
    print("\n[%d/%d] %s - %s (%s)", index, total,
        record.id, record.label, record.size)
    flush()
end

function sample(session, record, run)
    local failed = failure(run)
    if not session.options.verbose and not failed then return end
    local ordinal = run.warmup and run.ordinal or run.ordinal - session.warmups
    local count = run.warmup and session.warmups or session.samples
    local progress = ordinal .. "/" .. count
    local suffix = failed and "  " .. failed or ""
    if session.options.verbose then
        local values = {decimal(run.wall_ms) .. " ms"}
        for _, column in ipairs(session.columns) do
            local value = run.metrics and run.metrics[column.key]
            table.insert(values, column.label .. ": " .. (value ~= nil and tostring(value) or "-"))
        end
        print("  %s %s: %s%s", run.warmup and "Warmup" or "Sample", progress,
            table.concat(values, "; "), suffix)
        for _, detail in ipairs(run.details or {}) do print("    %s: %s", detail.label, detail.value) end
        if session.options.timings then stage_timings(run) end
    else
        print("  %s %s: %s", run.warmup and "Warmup" or "Sample", progress, failed)
    end
    flush()
end

local function primary_metric(session, summary)
    if session.primary then return summary.metrics and summary.metrics[session.primary.key] or {} end
    return {median = summary.median_wall_ms, minimum = summary.minimum_wall_ms, maximum = summary.maximum_wall_ms}
end

function finish_case(record, session)
    local summary = record.summary or {}
    local measured = summary.measured_runs or 0
    local metric = primary_metric(session, summary)
    print("  %s: median %s %s; range %s-%s (%d measured sample%s)", record.id,
        decimal(metric.median), session.primary and session.primary.unit or "ms",
        decimal(metric.minimum), decimal(metric.maximum), measured, measured == 1 and "" or "s")
    if session and session.options.timings and not session.options.verbose and summary.median_wall_ms then
        local selected, distance
        for _, run in ipairs(record.runs or {}) do
            if not run.warmup and not failure(run) then
                local difference = math.abs(run.wall_ms - summary.median_wall_ms)
                if not distance or difference < distance then selected, distance = run, difference end
            end
        end
        if selected then
            print("  Sample %d/%d nearest median: %s ms wall",
                selected.ordinal - session.warmups, session.samples, decimal(selected.wall_ms))
            stage_timings(selected)
        end
    end
    flush()
end

local function range(metric)
    if not metric then return "-" end
    if metric.minimum == metric.maximum then return tostring(metric.minimum) end
    return string.format("%s-%s", metric.minimum, metric.maximum)
end

function summary(session)
    local unit = session.primary and session.primary.unit or "ms"
    local columns = {"Case", "Samples", "Median (" .. unit .. ")", "Min (" .. unit .. ")", "Max (" .. unit .. ")"}
    for _, column in ipairs(session.columns) do table.insert(columns, column.label) end
    local rows, notes = {columns}, {}
    for _, record in ipairs(session.results.cases) do
        local summary = record.summary or {}
        local measured = summary.measured_runs or 0
        local incomplete = record.status ~= "complete" or measured ~= session.samples
        local metric = primary_metric(session, summary)
        local row = {record.id,
            incomplete and string.format("%d/%d", measured, session.samples) or tostring(measured),
            decimal(metric.median), decimal(metric.minimum), decimal(metric.maximum)}
        for _, column in ipairs(session.columns) do
            table.insert(row, column.statistic == "median"
                and decimal(summary.metrics and summary.metrics[column.key]
                    and summary.metrics[column.key].median)
                or range(summary.metrics and summary.metrics[column.key]))
        end
        table.insert(rows, row)
        if incomplete then
            local reason = record.status == "failed" and "failed" or "partial"
            for _, run in ipairs(record.runs or {}) do
                local failed = failure(run)
                if failed then reason = reason .. ": " .. failed; break end
            end
            local note = measured == 0 and reason .. "; no successful measured samples"
                or string.format("%s; statistics from %d/%d successful measured samples",
                    reason, measured, session.samples)
            table.insert(notes, record.id .. ": " .. note)
        end
    end
    local status = session.results.status
    print("\nResults%s", status == "failed" and " (FAILED)" or status ~= "complete" and " (partial)" or "")
    print_table(rows)
    for _, note in ipairs(notes) do print("  %s", note) end
    if #rows == 1 then print("  No cases reached measurement.") end
    if session.output then print("\nJSON: %s", session.output) end
    flush()
end
