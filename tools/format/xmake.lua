target("carven-format-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    add_files("src/**.cppm", "src/**.cpp|main.cpp")
target_end()

target("carven-format")
    set_group("toolchain")
    set_default(false)
    set_kind("binary")
    add_deps("carven-format-modules")
    add_files("src/main.cpp")
target_end()

target("carven-format-test-internal")
    set_default(false)
    set_kind("binary")
    add_deps("carven-format-modules", "carven-test-runner")
    add_files("tests/internal/*.cpp")

    add_tests("format", {
        group = "format",
        run_timeout = 60000,
        runargs = {"--exclude", "Format corpus:*"},
    })

    for index = 1, 2 do
        add_tests("corpus-" .. index, {
            group = "format",
            run_timeout = 60000,
            runargs = {"--filter", "Format corpus:*", "--shard", index .. "/2"},
        })
    end
target_end()

target("carven-format-test-cli")
    set_default(false)
    set_kind("phony")
    add_deps("carven-format", "carven", {inherit = false})

    on_load(function (target)
        local cases = import("cases", {
            rootdir = path.join(os.projectdir(), "tools", "format", "tests", "cli"),
        }).main()
        for _, case in ipairs(cases) do
            target:add("tests", case.name, {group = "format"})
        end
        target:add("tests", "help", {group = "format"})
        target:add("tests", "help_topic", {group = "format"})
    end)

    on_test(function (target, opt)
        return import("cli", {
            rootdir = path.join(os.projectdir(), "tools", "format", "tests", "cli"),
        }).main(target, opt)
    end)
target_end()
