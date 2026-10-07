import("core.base.json")
import("core.project.project")
import("core.package.repository")
import("xmake.benchmarks.runner", {alias = "benchmark", rootdir = os.projectdir()})
import("benchmarks.incremental.workloads", {rootdir = os.projectdir()})

function rules_repository()
    -- Read the declaration: --compiler can skip rebuilding stale package metadata.
    local name
    for _, requirement in ipairs(table.wrap(project.get("requires"))) do
        name = requirement:match("^(.-)@carven$")
        if name then break end
    end
    assert(name, "declare the Carven rule repository in the project first")
    for _, repo in ipairs(repository.repositories({global = false})) do
        if repo:name() == name and os.isdir(repo:directory()) then
            return repo:directory()
        end
    end
    raise("configured Carven rule repository is unavailable; configure and build the project first")
end

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

function wait_for_timestamp(root, before)
    local latest = 0
    for _, modified in pairs(before) do
        latest = math.max(latest, modified)
    end
    -- A baseline restore may rewrite sources without changing any object.
    -- Cross both source and object ticks so edits and rebuilds stay observable.
    for _, file in ipairs(os.files(path.join(root, "*.cv"))) do
        latest = math.max(latest, os.mtime(file))
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
            "Setup, baseline restoration, edits and timestamp waits are excluded from timings.",
            "Object counts observe new or timestamp-changed files; deleted objects are excluded.",
        },
        columns = {
            {key = "changed_objects", label = "Changed objects"},
            {key = "unrelated_objects", label = "Unrelated objects"},
        },
    })
    local rules_repo
    benchmark.run(session, selected, function (session, record, scenario, root)
        local inputs = workloads.inputs()
        inputs["xmake.lua"] = string.format([[set_project("carven-benchmark")
add_rules("mode.debug")
add_repositories(%q)
add_requires("carven-benchmark@carven", {alias = "carven", system = false, configs = {rules_only = true}})
target("bench")
    set_kind("binary")
    set_languages("c++20")
    add_rules("@carven/carven", {linkage_domain = "benchmark:incremental", timings = %s})
    set_values("carven.program", %q)
    set_values("carven.craftsdir", %q)
    add_files("*.cv")
]], "carven-benchmark " .. rules_repo, tostring(options.timings or false),
                session.compiler, path.join(os.projectdir(), "crafts"))
        record.inputs = inputs
        record.baseline, record.edits = scenario.baseline or {}, scenario.edits or {}
        record.linkage_domain, record.native_mode, record.language = "benchmark:incremental", "debug", "c++20"
        record.crafts_directory = path.join(os.projectdir(), "crafts")
        benchmark.checkpoint(session)
        write_files(root, inputs)
        write_files(root, scenario.baseline)
        benchmark.step(session, record, "configure", os.programfile(), {"f", "-y", "-m", "debug"}, root,
            {capture_stdout = true, timeout = 300000})
        benchmark.step(session, record, "initial build", os.programfile(), {"build", "bench"}, root,
            {capture_stdout = true, timeout = 300000})
        if session.output then
            local identity = benchmark.step(session, record, "native toolchain identity", os.programfile(),
                {"lua", "-c", [[
local config = import("core.project.config")
config.load()
local project = import("core.project.project")
local target = project.target("bench")
local program, name = target:tool("cxx")
local find_tool = import("lib.detect.find_tool")
local tool = find_tool(name, {program = program, version = true})
local json = import("core.base.json")
print(json.encode({program = program, name = name, version = tool and tool.version or "unobserved",
    platform = target:plat(), arch = target:arch(), mode = config.mode(), languages = target:get("languages")}))
]]}, root, {capture_stdout = true})
            local native_toolchain, errors = json.decode(identity.stdout)
            assert(native_toolchain, "native toolchain identity is not valid JSON: %s", errors)
            record.native_toolchain = native_toolchain
        end
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
            local raw = benchmark.command(os.programfile(), {"build", "bench"}, root,
                {timings = options.timings, capture_stdout = true, timeout = 300000})
            local after = object_mtimes(root)
            local changed = changed_objects(before, after)
            local unrelated_changed = changed_objects(unrelated_before, unrelated_objects(after))
            raw.changed_objects, raw.unrelated_objects = benchmark.array(changed), benchmark.array(unrelated_changed)
            raw.metrics = {changed_objects = #changed, unrelated_objects = #unrelated_changed}
            raw.details = benchmark.array()
            for _, file in ipairs(changed) do table.insert(raw.details, {label = "changed", value = file}) end
            raw.artifact_checks = benchmark.array()
            local artifact_errors = {}
            for module, count in table.orderpairs(scenario.artifacts or {}) do
                for _, extension in ipairs({"cpp", "hpp"}) do
                    local artifacts = os.files(path.join(root, "build", ".gens", "**", module .. "." .. extension))
                    table.insert(raw.artifact_checks, {module = module, extension = extension,
                        expected = count, actual = #artifacts, paths = benchmark.array(artifacts), passed = #artifacts == count})
                    if #artifacts ~= count then
                        table.insert(artifact_errors, string.format("artifact count for %s.%s: expected %d, found %d",
                            module, extension, count, #artifacts))
                    end
                end
            end
            if #artifact_errors > 0 then raw.validation_error = table.concat(artifact_errors, "; ") end
            return raw
        end
    end, function (session)
        rules_repo = rules_repository()
        if session.output then
            local metadata = session.results.metadata
            metadata.rules_repository = benchmark.revision(rules_repo)
            metadata.rules_repository.path = rules_repo
            metadata.rule_crafts_root = path.join(os.projectdir(), "crafts", "carven")
            benchmark.checkpoint(session)
        end
    end)
end
