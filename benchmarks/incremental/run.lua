import("core.project.project")
import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.harness.inputs", {alias = "input_snapshot", rootdir = os.projectdir()})
import("benchmarks.incremental.workloads", {rootdir = os.projectdir()})

function object_mtimes(root)
    local result = {}
    for _, pattern in ipairs({"**.o", "**.obj"}) do
        for _, file in ipairs(os.files(path.join(root, pattern))) do
            result[path.relative(file, root)] = os.mtime(file)
        end
    end
    return result
end

function changed_objects(before, after)
    local changed = {}
    for file, modified in pairs(after) do
        if before[file] ~= modified then table.insert(changed, file) end
    end
    table.sort(changed)
    return changed
end

function unrelated_objects(objects)
    local result = {}
    for file, modified in pairs(objects) do
        if path.filename(file):match("^unrelated%.cpp%.") then
            result[file] = modified
        end
    end
    return result
end

function generated_artifacts(root)
    local result = {}
    for _, extension in ipairs({"cpp", "hpp"}) do
        for _, file in ipairs(os.files(path.join(root, "**." .. extension))) do
            result[path.relative(file, root)] = {sha256 = hash.sha256(file):lower(), mtime = os.mtime(file)}
        end
    end
    return result
end

function artifact_changes(before, after)
    local result = {added = {}, changed = {}, removed = {}, unchanged_touched = {}}
    for file, current in pairs(after) do
        local previous = before[file]
        if not previous then
            table.insert(result.added, file)
        elseif current.sha256 ~= previous.sha256 then
            table.insert(result.changed, file)
        elseif current.mtime ~= previous.mtime then
            table.insert(result.unchanged_touched, file)
        end
    end
    for file in pairs(before) do
        if not after[file] then table.insert(result.removed, file) end
    end
    for name, files in pairs(result) do
        table.sort(files)
        result[name] = benchmark.array(files)
    end
    return result
end

function wait_for_timestamp(root, before, generated)
    local latest = 0
    for _, modified in pairs(before) do
        latest = math.max(latest, modified)
    end
    -- A baseline restore may rewrite sources without changing any object.
    -- Cross source, generated-file and object ticks so rewrites stay observable.
    for _, pattern in ipairs({path.join(root, "*.cv"), path.join(generated, "**.cpp"), path.join(generated, "**.hpp")}) do
        for _, file in ipairs(os.files(pattern)) do
            latest = math.max(latest, os.mtime(file))
        end
    end
    local marker = path.join(root, "timestamp")
    local started = os.mclock()
    repeat
        assert(os.mclock() - started < 10000, "filesystem timestamp did not advance")
        os.sleep(100)
        io.writefile(marker, "tick")
    until os.mtime(marker) > latest
end

-- False denotes an absent file in a baseline or edit.
function write_files(root, files, fixtures)
    for name, source in pairs(files or {}) do
        local file = path.join(root, name)
        if source then
            io.writefile(file, io.readfile(path.join(fixtures, source)))
        else
            os.tryrm(file)
        end
    end
end

function main(options)
    local selected = benchmark.select(workloads.cases(), options)
    if not selected then return end
    local session = benchmark.session("incremental", options, {
        description = {
            "Fixture: library -> facade -> app; independent unrelated module. Native settings: C++20 / O0.",
            "Includes Xmake startup, Carven generation, native C++ compilation and linking.",
            "Setup, baseline restoration, edits, snapshots, timestamp waits and executable validation are excluded from timings.",
            "Object counts observe new or timestamp-changed files; deleted objects are excluded.",
            "Generated content changes and unchanged-content timestamp changes are observed separately.",
            "Carven stage timings identify semantic analysis and C++ generation; --timings prints stage details.",
        },
        columns = {
            {key = "carven_invocations", label = "Carven runs"},
            {key = "generated_changes", label = "Artifact changes"},
            {key = "unchanged_generated_touched", label = "Unchanged touched"},
            {key = "changed_objects", label = "Changed objects"},
            {key = "unrelated_objects", label = "Unrelated objects"},
        },
    })
    local snapshot_root
    benchmark.run(session, selected, function (session, record, scenario, root)
        local inputs = workloads.inputs()
        local fixtures = path.join(snapshot_root, "benchmarks/incremental")
        record.inputs = inputs
        record.baseline, record.edits = scenario.baseline or {}, scenario.edits or {}
        record.expected_stdout = scenario.stdout
        local name = "benchmarks::incremental::workload"
        root = assert(project.target(name)):rundir()
        record.workspace = root
        record.native_mode, record.language = import("core.project.config").mode(), "c++20"
        record.crafts_directory = session.results.metadata.crafts_root
        benchmark.checkpoint(session)
        write_files(root, inputs, fixtures)
        write_files(root, scenario.baseline, fixtures)
        local build = {"build", name}
        benchmark.step(session, record, "initial build", os.programfile(), build, os.projectdir(),
            {timings = true, capture_stdout = true, timeout = 300000})
        local targets = import("benchmarks.harness.targets", {rootdir = os.projectdir()})
            .main("benchmarks::incremental", {"workload"})
        record.native_targets = targets
        local target = targets.workload
        local executable = target.executable
        local objects, generated_directory = target.object_directory, target.generated_directory
        return function (ordinal)
            if ordinal > 1 and scenario.edits then
                wait_for_timestamp(root, object_mtimes(objects), generated_directory)
                write_files(root, scenario.baseline, fixtures)
                benchmark.step(session, record, "restore baseline " .. ordinal,
                    os.programfile(), build, os.projectdir(), {timings = true, capture_stdout = true, timeout = 300000})
            end
            local before = object_mtimes(objects)
            assert(#table.keys(before) > 0, "baseline build produced no C++ object files")
            local unrelated_before = unrelated_objects(before)
            assert(#table.keys(unrelated_before) > 0, "baseline build produced no unrelated module object")
            wait_for_timestamp(root, before, generated_directory)
            write_files(root, scenario.edits, fixtures)
            local generated_before = generated_artifacts(generated_directory)
            local raw = benchmark.command(os.programfile(), build, os.projectdir(),
                {timings = true, capture_stdout = true, timeout = 300000})
            local after = object_mtimes(objects)
            local changed = changed_objects(before, after)
            local unrelated_changed = changed_objects(unrelated_before, unrelated_objects(after))
            raw.changed_objects, raw.unrelated_objects = benchmark.array(changed), benchmark.array(unrelated_changed)
            raw.metrics = {changed_objects = #changed, unrelated_objects = #unrelated_changed}
            do
                raw.metrics.carven_invocations = #raw.timings.reports
                raw.metrics.semantic_analyses, raw.metrics.cpp_generations = 0, 0
                for _, report in ipairs(raw.timings.reports) do
                    for _, stage in ipairs(report.stages) do
                        if stage.label == "Semantic analysis" then
                            raw.metrics.semantic_analyses = raw.metrics.semantic_analyses + 1
                        elseif stage.label == "C++ generation" then
                            raw.metrics.cpp_generations = raw.metrics.cpp_generations + 1
                        end
                    end
                end
            end
            local generated_after = generated_artifacts(generated_directory)
            local generated = artifact_changes(generated_before, generated_after)
            raw.generated_artifacts = {before = generated_before, after = generated_after, changes = generated}
            raw.metrics.generated_added = #generated.added
            raw.metrics.generated_changed = #generated.changed
            raw.metrics.generated_removed = #generated.removed
            raw.metrics.generated_changes = #generated.added + #generated.changed + #generated.removed
            raw.metrics.unchanged_generated_touched = #generated.unchanged_touched
            raw.details = benchmark.array()
            for _, file in ipairs(changed) do table.insert(raw.details, {label = "changed", value = file}) end
            for _, name in ipairs({"added", "changed", "removed", "unchanged_touched"}) do
                for _, file in ipairs(generated[name]) do
                    table.insert(raw.details, {label = "generated " .. name, value = file})
                end
            end
            local validation_errors = {}
            if raw.metrics.unrelated_objects ~= 0 then
                table.insert(validation_errors, "source edit rebuilt the unrelated module")
            end
            if scenario.unchanged then
                for _, metric in ipairs({"carven_invocations", "changed_objects",
                    "generated_changes", "unchanged_generated_touched"}) do
                    if raw.metrics[metric] ~= 0 then
                        table.insert(validation_errors, string.format(
                            "unchanged inputs repeated build work: %s = %d", metric, raw.metrics[metric]))
                    end
                end
            end
            if raw.exit_code == 0 then
                local execution = benchmark.command(executable, {}, root,
                    {target = target.name, capture_stdout = true})
                execution.expected_stdout = scenario.stdout
                execution.passed = execution.exit_code == 0
                    and execution.stdout:gsub("\r\n", "\n") == scenario.stdout
                raw.execution_validation = execution
                if not execution.passed then
                    table.insert(validation_errors, string.format(
                        "generated executable: expected exit 0 and stdout %q, got exit %s and stdout %q",
                        scenario.stdout, execution.exit_code, execution.stdout))
                end
            end
            if #validation_errors > 0 then raw.validation_error = table.concat(validation_errors, "; ") end
            return raw
        end
    end, function (session)
        snapshot_root = input_snapshot.capture(session)
        benchmark.checkpoint(session)
    end)
end
