add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

target("zero")
    add_cxxflags("-fno-rtti")
    set_exceptions("no-cxx")
    set_languages("c++26")
    set_optimize("none")
    set_warnings("allextra")

    add_files("src/transpiler/*.cppm")
    add_files("src/main.cpp")