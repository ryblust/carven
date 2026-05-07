add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})
set_defaultmode("debug")

target("zero")
    add_cxxflags("-fno-rtti", { tools = { "clang", "gcc" }})
    add_cxxflags("/GR-", { tools = { "clang_cl", "cl" }})

    set_languages("c++latest")
    set_exceptions("no-cxx")
    set_warnings("allextra")
    set_rundir("$(projectdir)")

    add_files("src/common/*.cppm")
    add_files("src/driver/*.cppm")
    add_files("src/frontend/*.cppm")
    add_files("src/backend/*.cppm")
    add_files("src/zero.cpp")

    for _, testfile in ipairs(os.files("tests/test_*.cpp")) do
        add_tests(path.basename(testfile), { files = testfile, remove_files = "src/zero.cpp" })
    end
