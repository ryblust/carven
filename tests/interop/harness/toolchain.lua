import("core.project.project")

function main()
    local repository = os.projectdir()
    local temporary = os.tmpfile() .. "-toolchain"
    local fixture = path.join(repository, "tests", "interop", "toolchain")
    local prefix = assert(project.required_package("carven"):installdir())
    local carven = path.absolute(project.target("carven"):targetfile())
    os.mkdir(temporary)
    os.cp(path.join(repository, "crafts"), path.join(temporary, "installed-crafts"))
    local original = io.readfile(path.join(fixture, "app.cv.fixture"))
    io.writefile(path.join(temporary, "app.cv"), original)
    os.cp(path.join(fixture, "provider.*"), temporary)
    local function write_project()
        io.writefile(path.join(temporary, "xmake.lua"), string.format([[
set_project("carven-toolchain-support")
set_languages("c++20")
add_rules("mode.debug")
package("carven")
    set_kind("toolchain")
    set_installdir(%q)
    on_fetch(function () return {} end)
package_end()
add_requires("carven", {system = false})
target("consumer")
    set_kind("binary")
    add_rules("@carven/carven")
    set_values("carven.program", %q)
    set_values("carven.craftsdir", %q)
    add_includedirs(".")
    add_files("*.cv", "provider.cpp")
target_end()
]], prefix, carven, path.join(temporary, "installed-crafts")))
    end
    write_project()
    local serial = 0
    local function command(args, expected_failure)
        serial = serial + 1
        local output, errors = path.join(temporary, serial .. ".stdout"), path.join(temporary, serial .. ".stderr")
        local code = os.execv(os.programfile(), args, {curdir = temporary, try = true, timeout = 300000,
            stdout = output, stderr = errors})
        local log = (io.readfile(output) or "") .. (io.readfile(errors) or "")
        assert(expected_failure and code ~= 0 or not expected_failure and code == 0,
            "toolchain command failed (artifacts: " .. temporary .. "):\n" .. log)
        return log
    end
    local function configure() command({"f", "-y", "-m", "debug"}) end
    local function build() command({"build", "-y"}) end
    local function run(expected)
        local output = command({"run", "-q", "consumer"}):gsub("\r\n", "\n")
        assert(output == expected .. "\n", "consumer used stale inputs: " .. output)
    end
    local function snapshot(patterns)
        local result = {}
        for _, pattern in ipairs(patterns) do
            for _, file in ipairs(os.files(path.join(temporary, pattern))) do
                result[path.relative(file, temporary)] = {hash.sha256(file), os.mtime(file)}
            end
        end
        return result
    end
    local live = {"build/.gens/**/rules/carven/**.cpp", "build/.gens/**/rules/carven/**.hpp"}
    local function unchanged(before, patterns)
        local after = snapshot(patterns)
        for file, observed in pairs(before) do
            assert(after[file] and after[file][1] == observed[1] and after[file][2] == observed[2],
                "unexpected artifact publication: " .. file .. " (artifacts: " .. temporary .. ")")
            after[file] = nil
        end
        assert(table.empty(after), "unexpected new artifact (artifacts: " .. temporary .. ")")
    end
    configure()
    build()
    run("7")
    local patterns = table.join(live, {"build/.objs/**.o", "build/.objs/**.obj"})
    local before = snapshot(patterns)
    build()
    unchanged(before, patterns)

    os.sleep(1100)
    io.writefile(path.join(temporary, "app.cv"), (original:gsub("=> 7;", "=> 22;")))
    build()
    run("8")
    os.sleep(1100)
    io.writefile(path.join(temporary, "provider.hpp"), (io.readfile(path.join(fixture, "provider.hpp")):gsub("= 42;", "= 59;")))
    build()
    run("9")

    -- New installed source/native inventory must be discovered after configuration.
    io.writefile(path.join(temporary, "installed-crafts/carven/std/inventory.cv"),
        'import <carven/std/inventory.hpp>; export fn discovered() -> i32 => ::carven_inventory_probe();\n')
    io.writefile(path.join(temporary, "installed-crafts/carven/std/inventory.hpp"),
        "int carven_inventory_probe() noexcept;\n")
    io.writefile(path.join(temporary, "installed-crafts/carven/std/inventory.cpp"), "int carven_inventory_probe() noexcept { return 1; }\n")
    configure()
    build()
    io.writefile(path.join(temporary, "app.cv"),
        'import std::inventory using discovered;\n'
        .. (io.readfile(path.join(temporary, "app.cv")):gsub("sqrt%(%)%);", "sqrt() + (discovered() as f64));")))
    build()
    run("10")

    before = snapshot(live)
    os.sleep(1100)
    io.writefile(path.join(temporary, "app.cv"), "fn main() { missing(); }\n")
    command({"build", "-y"}, true)
    unchanged(before, live)

    -- C++ checks whether the black-box call is valid in its declared environment.
    io.writefile(path.join(temporary, "app.cv"), original)
    io.writefile(path.join(temporary, "provider.hpp"), 'auto provided(int missing) -> int;\n')
    io.writefile(path.join(temporary, "provider.cpp"), '#include "provider.hpp"\nauto provided(int missing) -> int { return missing; }\n')
    local mismatch = command({"build", "-y"}, true)
    assert(mismatch:find("provided", 1, true) and mismatch:find("error", 1, true), mismatch)

    os.sleep(1100)
    io.writefile(path.join(temporary, "app.cv"), 'fn main() { println(7); }\n')
    io.writefile(path.join(temporary, "provider.cpp"), "// No native imports remain.\n")
    os.rm(path.join(temporary, "installed-crafts/carven/std/inventory.cv"))
    os.rm(path.join(temporary, "installed-crafts/carven/std/inventory.cpp"))
    os.rm(path.join(temporary, "installed-crafts/carven/std/inventory.hpp"))
    configure()
    build()
    assert(#os.files(path.join(temporary, "build/.gens/**/carven/providers/app.hpp")) == 0, "obsolete provider support survives")
    assert(#os.files(path.join(temporary, "build/.gens/**/crafts/carven/std/inventory.cpp")) == 0, "obsolete module survives")
    run("7")
    os.rm(temporary)
end
