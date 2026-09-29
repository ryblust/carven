target("carven-test-crafts")
    set_default(false)
    set_languages("c++20")
    add_rules("@carven/carven", {tests = "default"})
    add_files("carven/**.cv")
    add_tests("crafts", {group = "crafts", run_timeout = 30000})
    on_test(function (target)
        return import("generated", {
            rootdir = path.join(os.projectdir(), "tests", "harness"),
        }).main(target, "Crafts")
    end)
target_end()
