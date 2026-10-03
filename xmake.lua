set_project("carven")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_languages("c++26")
set_exceptions("no-cxx")
set_warnings("allextra")
set_rundir("$(projectdir)")
set_policy("build.progress_style", "multirow")

if is_plat("windows") then
    set_toolchains("clang-cl[llvm]")
else
    set_toolchains("llvm")
    if os.isfile(path.join(os.programdir(), "modules/private/action/build/content_depend.lua")) then
        set_policy("build.c++.modules.non_cascading_changes", true)
    end
end

add_cxxflags("-fno-rtti", {tools = "clang"})
add_cxxflags("/GR-", {tools = {"cl", "clang_cl"}})
add_cxxflags("/D_HAS_EXCEPTIONS=0", "/D_CRT_SECURE_NO_WARNINGS", {tools = {"cl", "clang_cl"}})

local carven_xmake_repo_dir = os.getenv("CARVEN_XMAKE_REPO_DIR")
local carven_repository = "carven-xmake-repo"
if carven_xmake_repo_dir and #carven_xmake_repo_dir > 0 then
    carven_xmake_repo_dir = path.absolute(carven_xmake_repo_dir, os.projectdir())
    carven_repository = "carven-xmake-local"
    add_repositories(carven_repository .. " " .. carven_xmake_repo_dir)
else
    add_repositories("carven-xmake-repo https://github.com/ryblust/carven-xmake-repo.git")
end

add_requires(carven_repository .. "@carven", {
    alias = "carven",
    system = false,
    configs = {rules_only = true},
})

rule("carven-host-stack")
    on_load(function (target)
        -- Recursive parsing needs more than the default 1 MiB Windows stack.
        -- Reserve address space without increasing the initial committed stack.
        if target:is_plat("mingw") then
            target:add("ldflags", "-Wl,--stack,16777216", {force = true})
        elseif target:is_plat("windows") then
            target:add("ldflags", "/STACK:16777216", {force = true})
        end
    end)
rule_end()

rule("generated-clang-tidy")
    on_config(function (target)
        os.cp(path.join(os.projectdir(), ".clang-tidy"),
              path.join(target:autogendir(), ".clang-tidy"))
        os.cp(path.join(os.projectdir(), "xmake", "generated.clang-tidy"),
              path.join(target:autogendir(), "rules", ".clang-tidy"))
    end)
rule_end()

add_rules("generated-clang-tidy")

target("carven-modules")
    set_default(false)
    set_kind("moduleonly")
    add_files("src/**.cppm")
    add_files("src/**.cpp|carven.cpp")
target_end()

target("carven")
    add_rules("carven-host-stack")
    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    add_deps("carven-modules")
    add_files("src/carven.cpp")
    add_installfiles("(crafts/carven/**)")
target_end()

includes("examples")
includes("tests/harness")
includes("tests/internal")
includes("tests/language")
includes("tests/crafts")
includes("tests/interop")
includes("tests/cli")
includes("tools/graver")

task("format")
    set_menu({
        usage = "xmake format [options]",
        description = "Format C++ and Carven sources",
        options = {},
    })
    on_run(function ()
        import("xmake.format").main(false)
    end)
task_end()

task("format-check")
    set_menu({
        usage = "xmake format-check [options]",
        description = "Check C++ and Carven formatting",
        options = {},
    })
    on_run(function ()
        import("xmake.format").main(true)
    end)
task_end()

task("bench")
    set_menu({
        usage = "xmake bench [options] <name>",
        description = "Build the compiler and run a Carven benchmark",
        options = {
            {nil, "samples", "kv", nil, "Number of measured runs (default: 3)"},
            {nil, "warmups", "kv", "1", "Number of warmup runs"},
            {nil, "compiler", "kv", nil, "Use another Carven executable without building"},
            {nil, "name", "v", nil, "Benchmark to run", values = {"compile", "incremental"}},
        },
    })
    on_run(function ()
        import("core.base.option")
        local name = option.get("name")
        assert(name == "compile" or name == "incremental",
            "select compile or incremental: ./xmakew bench <name>")
        import("xmake." .. name .. "_bench").main()
    end)
task_end()
