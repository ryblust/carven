add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", { outputdir = ".vscode" })
set_defaultmode("debug")
-- set_policy("build.c++.modules.non_cascading_changes", true)
-- set_policy("build.c++.modules.reuse.strict", true)
-- set_policy("build.progress_style", "multirow")

add_cxxflags("-fno-rtti", { tools = { "clang", "gcc" }})
add_cxxflags("/GR-", { tools = { "clang_cl", "cl" }})
set_languages("c++latest")
set_exceptions("no-cxx")
set_warnings("allextra")
set_rundir("$(projectdir)")

target("carven")
    add_files("src/**.cppm", { public = true })
    add_files("src/carven.cpp")

    for _, file in ipairs(os.files("tests/test_*.cpp")) do
        local name = path.basename(file)
        target("carven_" .. name)
            add_deps("carven")
            add_files("tests/" .. name .. ".cpp")
            add_defines("DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN", "DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS")
            add_tests(name, { realtime_output = true })
    end
