add_rules("mode.debug", "mode.release")
set_defaultmode("debug")

add_cxxflags("-fno-rtti", { tools = "clang" })
add_cxxflags("/GR-", { tools = { "cl", "clang_cl" } })
add_cxxflags("/D_HAS_EXCEPTIONS=0", "/D_CRT_SECURE_NO_WARNINGS", { tools = { "cl", "clang_cl" } })
add_cxxflags("-Wno-c23-extensions", { tools = { "clang", "clang_cl" } })

set_languages("c++latest")
set_exceptions("no-cxx")
set_warnings("allextra")
set_rundir("$(projectdir)")

target("carven-modules")
    set_kind("moduleonly")
    add_files("src/**.cppm")

target("carven")
    add_deps("carven-modules")
    add_files("src/carven.cpp")

target("carven-unit-test")
    set_default(false)
    add_deps("carven-modules")
    add_files("tests/units/test_*.cpp")
