set_project("carven")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
set_defaultmode("debug")
set_policy("build.progress_style", "multirow")

task("format")
    set_menu({
        usage = "xmake format [options]",
        description = "Format C++ sources with clang-format 23",
        options = {},
    })
    on_run(function ()
        import("xmake.format").main(false)
    end)
task_end()

task("format-check")
    set_menu({
        usage = "xmake format-check [options]",
        description = "Check C++ formatting with clang-format 23",
        options = {},
    })
    on_run(function ()
        import("xmake.format").main(true)
    end)
task_end()

if is_plat("windows") then
    set_toolchains("clang-cl[llvm]")
else
    set_toolchains("llvm")
end

add_cxxflags("-fno-rtti", {tools = "clang"})
add_cxxflags("/GR-", {tools = {"cl", "clang_cl"}})
add_cxxflags("/D_HAS_EXCEPTIONS=0", "/D_CRT_SECURE_NO_WARNINGS", {tools = {"cl", "clang_cl"}})
set_languages("c++26")
set_exceptions("no-cxx")
set_warnings("allextra")
set_rundir("$(projectdir)")

target("carven-modules")
    set_default(false)
    set_kind("moduleonly")
    add_files("src/**.cppm")
    add_files("src/**.cpp|carven.cpp")

target("carven")
    if is_mode("release") then
        set_policy("build.optimization.lto", true)
    end
    add_deps("carven-modules")
    add_files("src/carven.cpp")
    add_installfiles("crafts/(carven/**.hpp)", {prefixdir = "include"})

option("build_tests", {default = true, description = "Enable test and example targets"})

if has_config("build_tests") then
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

    includes("examples")
    includes("tests/internal")
    includes("tests/language")
    includes("tests/interop")
    includes("tests/cli")
end
