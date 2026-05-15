add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})
set_defaultmode("debug")
-- set_policy("build.c++.modules.non_cascading_changes", true)
-- set_policy("build.c++.modules.reuse.strict", true)
set_policy("build.progress_style", "multirow")

target("zero")
    add_cxxflags("-fno-rtti", { tools = { "clang", "gcc" }})
    add_cxxflags("/GR-", { tools = { "clang_cl", "cl" }})

    set_languages("c++latest")
    set_exceptions("no-cxx")
    set_warnings("allextra")
    set_rundir("$(projectdir)")

    add_files("src/**.cppm", "src/**.cpp")

    for _, testfile in ipairs(os.files("tests/test_*.cpp")) do
        add_tests(path.basename(testfile), {
            realtime_output = true,
            files = { "tests/utils.cppm", testfile },
            remove_files = "src/zero.cpp",
        })
    end
