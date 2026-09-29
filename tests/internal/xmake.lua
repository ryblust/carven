target("carven-test-internal")
    set_default(false)
    add_deps("carven-modules", "carven-test-support")

    add_includedirs(path.join(os.projectdir(), "crafts"))
    add_files(path.join(os.projectdir(), "tests", "harness", "main.cpp"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cppm"))
    add_files(path.join(os.projectdir(), "tests", "internal", "**.cpp"))

    add_tests("internal", {group = "internal", run_timeout = 60000})
target_end()
