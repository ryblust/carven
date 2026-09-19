target("graver-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    add_files(path.join(os.scriptdir(), "src", "**.cppm"))
    add_files(path.join(os.scriptdir(), "src", "**.cpp"))
    remove_files(path.join(os.scriptdir(), "src", "graver.cpp"))
target_end()

target("graver")
    set_default(false)
    set_kind("binary")
    add_deps("graver-modules")
    add_files(path.join(os.scriptdir(), "src", "graver.cpp"))
target_end()

target("graver-test-internal")
    set_default(false)
    set_kind("binary")
    add_deps("graver-modules")
    add_includedirs(path.join(os.projectdir(), "tests", "internal", "thirdparty"))
    add_files(path.join(os.scriptdir(), "tests", "internal", "*.cpp"))
    add_tests("graver", {group = "graver", run_timeout = 60000,
        runargs = {"--test-case-exclude=Graver corpus:*"}})
    add_tests("corpus", {group = "graver", run_timeout = 60000,
        runargs = {"--test-case=Graver corpus:*"}})
target_end()

target("graver-test-cli")
    set_default(false)
    set_kind("phony")
    add_deps("graver")
    on_load(function (target)
        local cases = import("cases", {
            rootdir = path.join(os.projectdir(), "tools", "graver", "tests", "cli"),
        }).main()
        for _, case in ipairs(cases) do
            target:add("tests", case.name, {group = "graver"})
        end
    end)
    on_test(function (target, opt)
        return import("cli", {
            rootdir = path.join(os.projectdir(), "tools", "graver", "tests", "cli"),
        }).main(target, opt)
    end)
target_end()
