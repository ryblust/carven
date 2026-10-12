target("carven-format-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    add_files(path.join(os.scriptdir(), "src", "**.cppm"))
    add_files(path.join(os.scriptdir(), "src", "**.cpp"))
    remove_files(path.join(os.scriptdir(), "src", "main.cpp"))
target_end()

target("carven-format")
    set_default(false)
    set_kind("binary")
    add_deps("carven-format-modules")
    add_files(path.join(os.scriptdir(), "src", "main.cpp"))
target_end()

target("carven-format-test-internal")
    set_default(false)
    set_kind("binary")
    add_deps("carven-format-modules", "carven-test-runner")
    add_files(path.join(os.scriptdir(), "tests", "internal", "*.cpp"))
    add_tests("format", {group = "format", run_timeout = 60000,
        runargs = {"--exclude", "Format corpus:*"}})
    for index = 1, 2 do
        add_tests("corpus-" .. index, {group = "format", run_timeout = 60000,
            runargs = {"--filter", "Format corpus:*", "--shard", index .. "/2"}})
    end
target_end()

target("carven-format-test-cli")
    set_default(false)
    set_kind("phony")
    add_deps("carven-format")
    on_load(function (target)
        local cases = import("cases", {
            rootdir = path.join(os.projectdir(), "tools", "format", "tests", "cli"),
        }).main()
        for _, case in ipairs(cases) do
            target:add("tests", case.name, {group = "format"})
        end
    end)
    on_test(function (target, opt)
        return import("cli", {
            rootdir = path.join(os.projectdir(), "tools", "format", "tests", "cli"),
        }).main(target, opt)
    end)
target_end()
