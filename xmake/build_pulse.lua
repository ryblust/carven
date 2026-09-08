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
    local package = project.required_package("carven")
    assert(package, "configure the project with the Carven rule package first")
    local manifest = io.load(path.join(package:installdir(), "manifest.txt"))
    local name = package:requirestr():match("^(.-)@carven$")
    local candidates = {}
    if name then
        table.insert(candidates, path.join(repository.directory(false), name))
    end
    if manifest and manifest.repo and manifest.repo.url then
        table.insert(candidates, manifest.repo.url)
    end
    for _, candidate in ipairs(candidates) do
        if os.isfile(path.join(candidate, "packages/c/carven/rules/carven.lua")) then
            return path.absolute(candidate)
        end
    end
    raise("configured Carven rule repository is unavailable; configure and build the project first")
end

function batch(compiler, count, samples, warmups)
    return benchmark.temporary(function (root)
        local inputs = {}
        for index = 0, count - 1 do
            local filename = string.format("module_%03d.cv", index)
            io.writefile(path.join(root, filename), string.format("export struct Value%03d { value: i32, }\n", index))
            table.insert(inputs, filename)
        end
        return benchmark.measure(samples, warmups, function (ordinal)
            os.iorunv(compiler, table.join({"--output-dir", "out-" .. ordinal,
                "--linkage-domain=benchmark:build-pulse:" .. count}, inputs), {curdir = root})
        end)
    end)
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

function private_edit(compiler, rules_repo)
    return benchmark.temporary(function (root)
        io.writefile(path.join(root, "library.cv"), library)
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
    add_packages("carven")
    add_rules("@carven/carven", {linkage_domain = "benchmark:build-pulse:edit"})
    set_values("carven.program", %q)
    set_values("carven.includedir", %q)
    add_files("*.cv")
]], "carven-benchmark " .. rules_repo, compiler, path.join(os.projectdir(), "crafts")))
        os.iorunv(os.programfile(), {"f", "-m", "debug"}, {curdir = root})
        os.iorunv(os.programfile(), {"build", "bench"}, {curdir = root})
        local before = object_mtimes(root)
        assert(#table.keys(before) > 0, "warm build produced no C++ object files")
        local latest = 0
        for _, modified in pairs(before) do
            latest = math.max(latest, modified)
        end
        -- os.mtime has whole-second precision. Cross a filesystem timestamp tick
        -- before editing so a fast rebuild cannot retain the observed timestamp.
        local marker = path.join(root, "timestamp")
        local started = os.mclock()
        repeat
            assert(os.mclock() - started < 10000, "filesystem timestamp did not advance")
            os.sleep(100)
            io.writefile(marker, "tick")
        until os.mtime(marker) > latest
        io.writefile(path.join(root, "library.cv"), (library:gsub("number %+ 1", "number + 2")))
        os.iorunv(os.programfile(), {"build", "bench"}, {curdir = root})
        local after = object_mtimes(root)
        local changed = {}
        for file, modified in pairs(before) do
            if after[file] ~= modified then table.insert(changed, file) end
        end
        for file in pairs(after) do
            if before[file] == nil then table.insert(changed, file) end
        end
        table.sort(changed)
        return changed, #table.keys(after)
    end)
end

function main()
    local compiler, samples, warmups = benchmark.settings()
    local rules_repo = rules_repository()
    local small = batch(compiler, 16, samples, warmups)
    local large = batch(compiler, 128, samples, warmups)
    local changed, count = private_edit(compiler, rules_repo)
    print("\nFresh batch\n  Modules      Median")
    print("%9d    %.2f ms", 16, benchmark.median(small))
    print("%9d    %.2f ms", 128, benchmark.median(large))
    print("   Growth    %.2fx for 8x input", benchmark.median(large) / benchmark.median(small))
    print("\nPrivate edit\n  Rebuilt    %d / %d objects", #changed, count)
    if option.get("verbose") then
        print("\nDetails")
        for _, case in ipairs({{16, small}, {128, large}}) do
            local values = {}
            for _, value in ipairs(case[2]) do table.insert(values, string.format("%.2f ms", value)) end
            print("  %d modules    %s", case[1], table.concat(values, ", "))
        end
        print("  Recompiled objects")
        for _, file in ipairs(changed) do print("    %s", file) end
    else
        print("\nUse --verbose to show individual samples and object paths.")
    end
end
