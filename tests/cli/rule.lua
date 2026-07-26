local function write_file(filename, contents)
    os.mkdir(path.directory(filename))
    io.writefile(filename, contents)
    assert(os.isfile(filename), "cannot write fixture: " .. filename)
end

local function only_file(pattern)
    local files = os.files(pattern)
    assert(#files == 1, string.format("expected one file for %s, found %d", pattern, #files))
    return files[1]
end

local function snapshot_tree(root)
    local snapshot = {}
    for _, filename in ipairs(os.files(path.join(root, "**"))) do
        if os.isfile(filename) then
            snapshot[path.unix(path.relative(filename, root))] = {
                contents = io.readfile(filename),
                mtime = os.mtime(filename),
            }
        end
    end
    return snapshot
end

local function set_file_mtime(filename, mtime)
    os.touch(filename, {mtime = mtime})
    assert(os.mtime(filename) == mtime, "cannot set fixture mtime: " .. filename)
end

local function set_tree_mtime(root, mtime)
    for _, filename in ipairs(os.files(path.join(root, "**"))) do
        if os.isfile(filename) then
            set_file_mtime(filename, mtime)
        end
    end
end

local function assert_snapshot_equal(actual, expected)
    for logical_path, expected_file in pairs(expected) do
        local actual_file = actual[logical_path]
        assert(actual_file, "live artifact disappeared after failed generation: " .. logical_path)
        assert(
            actual_file.contents == expected_file.contents,
            "live artifact content changed after failed generation: " .. logical_path
        )
        assert(
            actual_file.mtime == expected_file.mtime,
            "live artifact mtime changed after failed generation: " .. logical_path
        )
    end
    for logical_path in pairs(actual) do
        assert(expected[logical_path], "live artifact appeared after failed generation: " .. logical_path)
    end
end

local function run_xmake(work_dir, arguments)
    local envs = os.joinenvs(os.getenvs(), {
        XMAKE_PKG_CACHEDIR = path.join(work_dir, ".xmake-package-cache"),
        XMAKE_PKG_INSTALLDIR = path.join(work_dir, ".xmake-packages"),
    })
    os.vrunv(os.programfile(), arguments, {curdir = work_dir, envs = envs})
end

function main(target)
    local compiler = path.absolute(target:dep("carven"):targetfile(), os.projectdir())
    local runtime = path.join(os.projectdir(), "crafts")
    local package = target:pkg("carven")
    assert(package, "Carven rules-only package is unavailable")
    local installed_rule = path.join(package:installdir(), "rules", "carven.lua")
    assert(os.isfile(installed_rule), "installed Carven rule is unavailable")

    local work_dir = os.tmpfile("carven-xmake-rule") .. ".dir"
    os.tryrm(work_dir)
    os.mkdir(work_dir)

    local package_recipe = path.join(work_dir, "repository", "packages", "c", "carven", "xmake.lua")
    write_file(package_recipe, string.format([[
package("carven")
    set_kind("toolchain")
    on_install(function (package)
        os.mkdir(package:installdir("rules"))
        os.cp(%q, package:installdir("rules"))
    end)
]], installed_rule))

    write_file(path.join(work_dir, "xmake.lua"), string.format([[
set_project("carven-xmake-rule-test")
set_toolchains("llvm")

add_repositories("carven-rule-test %s")
add_requires("carven-rule-test@carven", {alias = "carven", system = false})

target("sample")
    set_kind("object")
    add_packages("carven")
    add_rules("@carven/carven")
    set_values("carven.program", %q)
    set_values("carven.includedir", %q)
    add_files("a.cv", "b.cv")
]], path.join(work_dir, "repository"), compiler, runtime))

    local source_a = path.join(work_dir, "a.cv")
    local source_a_exported = [[export fn value_a() -> i32 {
    return 1;
}
]]
    local source_a_private = [[private fn value_a() -> i32 {
    return 1;
}
]]
    write_file(source_a, source_a_exported)
    write_file(path.join(work_dir, "b.cv"), [[private fn value_b() -> i32 {
    return 2;
}
]])

    try
    {
        function ()
            run_xmake(work_dir, {"build", "-y", "sample"})

            local live_a = only_file(path.join(
                work_dir,
                "build/.gens/sample/**/rules/carven/a.cpp"
            ))
            local live_root = path.directory(live_a)
            local live_b = path.join(live_root, "b.cpp")
            local live_interface = path.join(live_root, "carven/generated/a.hpp")
            assert(os.isfile(live_b) and os.isfile(live_interface), "initial live artifacts are incomplete")

            set_tree_mtime(live_root, os.time() - 10)
            local initial_live = snapshot_tree(live_root)
            run_xmake(work_dir, {"build", "-r", "sample"})
            assert_snapshot_equal(snapshot_tree(live_root), initial_live)

            write_file(source_a, source_a_private)
            local source_mtime = os.time() + 1
            set_file_mtime(source_a, source_mtime)
            run_xmake(work_dir, {"build", "sample"})
            assert(os.isfile(live_a) and os.isfile(live_b), "topology change lost implementation artifacts")
            assert(not os.exists(live_interface), "stale interface artifact survived topology change")

            set_file_mtime(source_a, os.time() - 10)
            os.rm(live_b)
            run_xmake(work_dir, {"build", "sample"})
            assert(os.isfile(live_b), "missing tracked artifact did not trigger regeneration")

            local live_before_failure = snapshot_tree(live_root)
            write_file(source_a, "private fn value_a(\n")
            local generation_failed = false
            try
            {
                function ()
                    run_xmake(work_dir, {"build", "-r", "sample"})
                end,
                catch
                {
                    function ()
                        generation_failed = true
                    end
                }
            }
            assert(generation_failed, "invalid Carven input unexpectedly built")
            assert_snapshot_equal(snapshot_tree(live_root), live_before_failure)
            assert(not os.exists(live_root .. ".staging"), "failed staging directory was retained")
        end,
        finally
        {
            function (ok, errors)
                if ok then
                    os.tryrm(work_dir)
                else
                    print("Xmake rule test directory retained at: " .. work_dir)
                    raise(errors)
                end
            end
        }
    }
    return true
end
