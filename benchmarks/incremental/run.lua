import("core.base.json")
import("core.project.project")
import("benchmarks.harness.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.harness.inputs", {alias = "input_snapshot", rootdir = os.projectdir()})
import("benchmarks.harness.native_build", {rootdir = os.projectdir()})
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
        for _, file in ipairs(os.files(path.join(root, "build", ".gens", "**." .. extension))) do
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

function wait_for_timestamp(root, before)
    local latest = 0
    for _, modified in pairs(before) do
        latest = math.max(latest, modified)
    end
    -- A baseline restore may rewrite sources without changing any object.
    -- Cross source, generated-file and object ticks so rewrites stay observable.
    for _, pattern in ipairs({"*.cv", "build/.gens/**.cpp", "build/.gens/**.hpp"}) do
        for _, file in ipairs(os.files(path.join(root, pattern))) do
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
function write_files(root, files)
    for name, source in pairs(files or {}) do
        local file = path.join(root, name)
        if source then
            io.writefile(file, source)
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
            "Fixture: library -> facade -> app; independent unrelated module. Native mode: debug / C++20.",
            "Includes Xmake startup, Carven generation, native C++ compilation and linking.",
            "Setup, baseline restoration, edits, snapshots, timestamp waits and executable validation are excluded from timings.",
            "Object counts observe new or timestamp-changed files; deleted objects are excluded.",
            "Generated content changes and unchanged-content timestamp changes are observed separately.",
            "Carven invocation counts are observed only with --timings; missing instrumentation is not zero.",
        },
        columns = {
            {key = "carven_invocations", label = "Carven runs"},
            {key = "generated_changes", label = "Artifact changes"},
            {key = "unchanged_generated_touched", label = "Unchanged touched"},
            {key = "changed_objects", label = "Changed objects"},
            {key = "unrelated_objects", label = "Unrelated objects"},
        },
    })
    local rules_prefix, snapshot_root
    local captured_cases = {}
    benchmark.run(session, selected, function (session, record, scenario, root)
        scenario = captured_cases[scenario.id]
        local inputs = workloads.inputs(snapshot_root)
        local crafts = path.join(snapshot_root, "crafts")
        inputs["xmake.lua"] = string.format([[set_project("carven-benchmark")
add_rules("mode.debug")
package("carven")
    set_kind("toolchain")
    set_installdir(%q)
    on_fetch(function () return {} end)
package_end()
add_requires("carven", {system = false})
target("bench")
    set_kind("binary")
    set_languages("c++20")
    add_rules("@carven/carven", {linkage_domain = "benchmark:incremental", timings = %s})
    set_values("carven.program", %q)
    set_values("carven.craftsdir", %q)
    add_files("*.cv")
]], rules_prefix, tostring(options.timings or false),
                session.compiler, crafts)
        record.inputs = inputs
        record.baseline, record.edits = scenario.baseline or {}, scenario.edits or {}
        record.expected_stdout = scenario.stdout
        record.linkage_domain, record.native_mode, record.language = "benchmark:incremental", "debug", "c++20"
        record.crafts_directory = crafts
        benchmark.checkpoint(session)
        write_files(root, inputs)
        write_files(root, scenario.baseline)
        benchmark.step(session, record, "configure", os.programfile(), {"f", "-y", "-m", "debug"}, root,
            {capture_stdout = true, timeout = 300000})
        local initial = benchmark.step(session, record, "initial build", os.programfile(), {"build", "bench"}, root,
            {capture_stdout = true, timeout = 300000})
        if options.timings then
            assert(initial.timings and #initial.timings.reports > 0,
                "fresh build produced no Carven timing report; invocation counts cannot be observed")
        end
        local identity = benchmark.step(session, record, "native toolchain identity", os.programfile(),
            {"lua", "-c", native_build.describe_script()}, root, {capture_stdout = true})
        local targets, errors = json.decode(identity.stdout)
        assert(targets, "native target identity is not valid JSON: %s", errors)
        record.native_targets = targets
        local executable = assert(targets.bench and targets.bench.executable, "Xmake did not report the bench executable")
        return function (ordinal)
            if ordinal > 1 and scenario.edits then
                wait_for_timestamp(root, object_mtimes(root))
                write_files(root, scenario.baseline)
                benchmark.step(session, record, "restore baseline " .. ordinal,
                    os.programfile(), {"build", "bench"}, root, {capture_stdout = true, timeout = 300000})
            end
            local before = object_mtimes(root)
            assert(#table.keys(before) > 0, "baseline build produced no C++ object files")
            local unrelated_before = unrelated_objects(before)
            assert(#table.keys(unrelated_before) > 0, "baseline build produced no unrelated module object")
            wait_for_timestamp(root, before)
            write_files(root, scenario.edits)
            local generated_before = generated_artifacts(root)
            local raw = benchmark.command(os.programfile(), {"build", "bench"}, root,
                {timings = options.timings, capture_stdout = true, timeout = 300000})
            local after = object_mtimes(root)
            local changed = changed_objects(before, after)
            local unrelated_changed = changed_objects(unrelated_before, unrelated_objects(after))
            raw.changed_objects, raw.unrelated_objects = benchmark.array(changed), benchmark.array(unrelated_changed)
            raw.metrics = {changed_objects = #changed, unrelated_objects = #unrelated_changed}
            if options.timings then raw.metrics.carven_invocations = #raw.timings.reports end
            local generated_after = generated_artifacts(root)
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
            raw.artifact_checks = benchmark.array()
            local validation_errors = {}
            for module, count in table.orderpairs(scenario.artifacts or {}) do
                for _, extension in ipairs({"cpp", "hpp"}) do
                    local artifacts = os.files(path.join(root, "build", ".gens", "**", module .. "." .. extension))
                    table.insert(raw.artifact_checks, {module = module, extension = extension,
                        expected = count, actual = #artifacts, paths = benchmark.array(artifacts), passed = #artifacts == count})
                    if #artifacts ~= count then
                        table.insert(validation_errors, string.format("artifact count for %s.%s: expected %d, found %d",
                            module, extension, count, #artifacts))
                    end
                end
            end
            if raw.exit_code == 0 then
                local execution = benchmark.command(executable, {}, root, {capture_stdout = true})
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
        rules_prefix = assert(project.required_package("carven"):installdir(),
            "configure and build the project before running incremental benchmarks")
        snapshot_root = input_snapshot.capture(session)
        for _, case in ipairs(workloads.cases(snapshot_root)) do captured_cases[case.id] = case end
        local metadata = session.results.metadata
        metadata.rules_directory = path.join(rules_prefix, "rules")
        benchmark.checkpoint(session)
    end)
end
