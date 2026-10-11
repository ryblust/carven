target("carven-workspace-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    add_files(path.join(os.scriptdir(), "src", "**.cppm"))
    add_files(path.join(os.scriptdir(), "src", "**.cpp"))
target_end()

target("carven-test-workspace")
    set_default(false)
    set_group("tests/workspace")
    set_kind("binary")
    add_deps("carven-workspace-modules", "carven-test-runner")
    add_includedirs(path.join(os.projectdir(), "crafts"))
    add_files(path.join(os.scriptdir(), "tests", "*.cpp"))
    add_tests("workspace", {
        group = "workspace", run_timeout = has_config("sanitizers") and 180000 or 60000,
    })
target_end()
