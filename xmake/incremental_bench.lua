import("core.base.option")
import("core.project.project")
import("core.package.repository")
import("xmake.benchmark", {rootdir = os.projectdir()})

local library = [[export struct Value { number: i32, }
private fn adjust(number: i32) -> i32 { return number + 1; }
export fn make_value(number: i32) -> Value {
    return Value { number: adjust(number) };
}
]]

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

function count_range(counts)
    table.sort(counts)
    return counts[1] == counts[#counts] and tostring(counts[1])
        or string.format("%d-%d", counts[1], counts[#counts])
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

function workloads()
    local extra = "export struct Extra { number: i32 }\n"
    local public = library:gsub("number: i32,", "number: i32, extra: i32,")
    public = public:gsub("number: adjust%(number%)", "number: adjust(number), extra: 0")
    return {
        {label = "No changes"},
        {label = "Identical content rewrite", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = library}},
        {label = "Private function edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = (library:gsub("number %+ 1", "number + 2"))}},
        {label = "Public interface edit", baseline = {["library.cv"] = library},
            edits = {["library.cv"] = public}},
        {label = "Add module", baseline = {["extra.cv"] = false},
            edits = {["extra.cv"] = extra}, artifacts = {extra = 1}},
        {label = "Remove module", baseline = {["extra.cv"] = extra},
            edits = {["extra.cv"] = false}, artifacts = {extra = 0}},
    }
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

function main()
    local compiler, samples, warmups = benchmark.settings("incremental", 3)
    local rules_repo = rules_repository()
    print("%-15s %s", "Fixture:", "library -> facade -> app; unrelated module")
    print("%-15s %s", "Native mode:", "debug / C++20")
    print("Includes Xmake startup, Carven generation, native C++ compilation and linking.")
    print("Setup, baseline restoration and timestamp waits are excluded from timings.")
    print("Rebuilt objects: new or timestamp-changed files per run (range if counts vary).")
    print("Unrelated: rebuilt objects belonging to the unchanged, independent module.")
    local scenarios = workloads()
    local rows = {}
    for index, scenario in ipairs(scenarios) do
        print("")
        benchmark.progress(index, #scenarios, scenario.label)
        benchmark.temporary(function (root)
            io.writefile(path.join(root, "library.cv"), library)
            io.writefile(path.join(root, "unrelated.cv"), "export fn unrelated_value() -> i32 { return 7; }\n")
            io.writefile(path.join(root, "facade.cv"), [[import library using { Value, make_value, };
export fn create_value(number: i32) -> Value { return make_value(number); }
]])
            io.writefile(path.join(root, "app.cv"), [[import facade using create_value;
fn main() { let value = create_value(41); }
]])
            io.writefile(path.join(root, "xmake.lua"), string.format([[set_project("carven-benchmark")
add_rules("mode.debug")
add_repositories(%q)
add_requires("carven-benchmark@carven", {alias = "carven", system = false, configs = {rules_only = true}})
target("bench")
    set_kind("binary")
    set_languages("c++20")
    add_rules("@carven/carven", {linkage_domain = "benchmark:incremental"})
    set_values("carven.program", %q)
    set_values("carven.craftsdir", %q)
    add_files("*.cv")
]], "carven-benchmark " .. rules_repo, compiler, path.join(os.projectdir(), "crafts")))
            write_files(root, scenario.baseline)
            os.iorunv(os.programfile(), {"f", "-y", "-m", "debug"}, {curdir = root})
            os.iorunv(os.programfile(), {"build", "bench"}, {curdir = root})
            local results, counts, unrelated_counts = {}, {}, {}
            for ordinal = 1, warmups + samples do
                if ordinal <= warmups then
                    print("  Warmup %d/%d ...", ordinal, warmups)
                else
                    print("  Sample %d/%d ...", ordinal - warmups, samples)
                end
                io.flush()
                -- The first baseline is already built; no-change runs need no reset.
                if ordinal > 1 and scenario.edits then
                    wait_for_timestamp(root, object_mtimes(root))
                    write_files(root, scenario.baseline)
                    os.iorunv(os.programfile(), {"build", "bench"}, {curdir = root})
                end
                local before = object_mtimes(root)
                assert(#table.keys(before) > 0, "baseline build produced no C++ object files")
                local unrelated_before = unrelated_objects(before)
                assert(#table.keys(unrelated_before) > 0, "baseline build produced no unrelated module object")
                wait_for_timestamp(root, before)
                write_files(root, scenario.edits)
                local started = os.mclock()
                os.iorunv(os.programfile(), {"build", "bench"}, {curdir = root})
                local elapsed = os.mclock() - started
                local after = object_mtimes(root)
                local changed = changed_objects(before, after)
                local unrelated_changed = changed_objects(unrelated_before, unrelated_objects(after))
                for module, count in pairs(scenario.artifacts or {}) do
                    for _, extension in ipairs({"cpp", "hpp"}) do
                        local artifacts = os.files(path.join(root, "build", ".gens", "**", module .. "." .. extension))
                        assert(#artifacts == count, scenario.label .. " produced an unexpected artifact set")
                    end
                end
                if ordinal > warmups then
                    table.insert(results, elapsed)
                    table.insert(counts, #changed)
                    table.insert(unrelated_counts, #unrelated_changed)
                    if option.get("verbose") then
                        print("  Sample %d: %.2f ms; %d rebuilt objects", ordinal - warmups, elapsed, #changed)
                        for _, file in ipairs(changed) do print("    %s", file) end
                    end
                end
            end
            local count = count_range(counts)
            local unrelated_count = count_range(unrelated_counts)
            local median = benchmark.median(results)
            local row = string.format("%-27s %12.2f %15s %12s", scenario.label, median, count, unrelated_count)
            table.insert(rows, row)
            print("  Median: %.2f ms; rebuilt objects: %s; unrelated: %s", median, count, unrelated_count)
            io.flush()
        end)
    end
    print("\n%-27s %12s %15s %12s", "Scenario", "Median (ms)", "Rebuilt objects", "Unrelated")
    for _, row in ipairs(rows) do print(row) end
    print("\nArtifact checks: passed (generated module files added and removed)")
    print("Complete. Use --verbose to show individual samples and changed object paths.")
end
