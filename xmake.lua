set_project("carven")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_languages("c++26")
set_exceptions("no-cxx")
add_cxxflags("-fno-rtti")
set_warnings("allextra")
set_rundir("$(projectdir)")
set_policy("build.progress_style", "multirow")
set_policy("build.c++.modules.non_cascading_changes", true)

if is_plat("windows") then
    set_toolchains("mingw[clang]")
    set_runtimes("c++_shared")
else
    set_toolchains("llvm")
    if is_plat("linux") then
        set_runtimes("c++_shared")
        add_syslinks("c++abi")
    end
end

option("sanitizers")
    set_default(false)
    set_showmenu(true)
    set_description("Enable AddressSanitizer and UndefinedBehaviorSanitizer")
option_end()

if has_config("sanitizers") then
    set_policy("build.sanitizer.address", true)
    set_policy("build.sanitizer.undefined", true)
    -- O1 keeps sanitizer runs practical; these flags preserve useful failure traces.
    set_optimize("fast")
    add_cxflags("-fno-omit-frame-pointer", "-fno-optimize-sibling-calls",
        "-fsanitize-address-use-after-scope", "-fno-sanitize-recover=all")
end

-- Keep sanitizer failures distinct from program errors and observed SIGABRT.
for _, name in ipairs({"ASAN_OPTIONS", "UBSAN_OPTIONS"}) do
    set_runenv(name, (os.getenv(name) or "") .. ":halt_on_error=1:abort_on_error=0:exitcode=86")
end

local carven_xmake_repo_dir = os.getenv("CARVEN_XMAKE_REPO_DIR")
if carven_xmake_repo_dir and #carven_xmake_repo_dir > 0 then
    carven_xmake_repo_dir = path.absolute(carven_xmake_repo_dir, os.projectdir())
    add_repositories("carven-xmake-repo " .. carven_xmake_repo_dir)
else
    add_repositories("carven-xmake-repo https://github.com/ryblust/carven-xmake-repo.git")
end

add_requires("carven-xmake-repo@carven", {
    alias = "carven",
    system = false,
    configs = {rules_only = true},
})

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
    add_deps("carven-modules")
    add_files("src/carven.cpp")
    add_installfiles("(crafts/carven/**)")
    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
target_end()

includes("examples")
includes("tests/harness")
includes("tests/crafts")
includes("tests/cli")
includes("tests/interop")
includes("tests/internal")
includes("tests/language")
includes("tools/format")
includes("tools/workspace")
includes("benchmarks/async")
includes("benchmarks/simd")
includes("benchmarks/json")
includes("benchmarks/incremental")

task("bench")
    set_menu({
        usage = "xmake bench [options] <name> [options]",
        description = "Run a Carven benchmark with a built compiler",
        options = {
            function ()
                return import("benchmarks.harness.options", {rootdir = os.projectdir()}).menu()
            end,
        },
    })
    on_run(function ()
        local settings = import("benchmarks.harness.options", {rootdir = os.projectdir()}).configure()
        if not settings then return end
        import("benchmarks." .. settings.name .. ".run", {rootdir = os.projectdir()}).main(settings)
    end)
task_end()

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
