target("carven-test-support")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    add_files("framework.cppm", "framework.cpp")
    add_files("diagnostics.cppm", "diagnostics.cpp")
    add_files("directory.cppm", "directory.cpp")
target_end()

-- Each probe runs the shared runner once and checks its result and captured report.
target("carven-test-framework")
    set_default(false)
    set_kind("binary")
    add_deps("carven-modules", "carven-test-support")
    add_files("probe.cpp")
    for _, mode in ipairs({"passing", "selection", "list", "empty-selection",
                          "duplicate", "no-assertions", "failure"}) do
        add_tests(mode, {group = "internal", runargs = {mode}})
    end
target_end()
