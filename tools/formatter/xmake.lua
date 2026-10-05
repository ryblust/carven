target("formatter-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    add_files(path.join(os.scriptdir(), "src", "**.cppm"))
    add_files(path.join(os.scriptdir(), "src", "**.cpp"))
    remove_files(path.join(os.scriptdir(), "src", "formatter.cpp"))
target_end()

target("carven-format")
    set_default(false)
    set_kind("binary")
    add_deps("formatter-modules")
    add_files(path.join(os.scriptdir(), "src", "formatter.cpp"))
target_end()

target("formatter-test-internal")
    set_default(false)
    set_kind("binary")
    add_deps("formatter-modules", "carven-test-support")
    add_files(path.join(os.scriptdir(), "tests", "internal", "*.cpp"))
    add_files(path.join(os.projectdir(), "tests", "harness", "main.cpp"))
    add_tests("formatter", {group = "formatter", run_timeout = 60000,
        runargs = {"--exclude", "Formatter corpus:*"}})
    add_tests("corpus", {group = "formatter", run_timeout = 60000,
        runargs = {"--filter", "Formatter corpus:*"}})
target_end()

target("formatter-test-cli")
    set_default(false)
    set_kind("phony")
    add_deps("carven-format")
    on_load(function (target)
        local cases = import("cases", {
            rootdir = path.join(os.projectdir(), "tools", "formatter", "tests", "cli"),
        }).main()
        for _, case in ipairs(cases) do
            target:add("tests", case.name, {group = "formatter"})
        end
    end)
    on_test(function (target, opt)
        return import("cli", {
            rootdir = path.join(os.projectdir(), "tools", "formatter", "tests", "cli"),
        }).main(target, opt)
    end)
target_end()
