target("carven-test-internal")
    set_default(false)
    add_deps("carven-modules")

    add_includedirs(path.join(os.projectdir(), "crafts"))
    add_includedirs(path.join(os.projectdir(), "tests", "internal", "vendor"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cppm"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cpp"))

    add_tests("internal", {realtime_output = false, group = "internal"})
