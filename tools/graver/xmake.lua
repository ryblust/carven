target("graver-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("carven-modules")
    for _, component in ipairs({"source", "layout", "format"}) do
        add_files(path.join(os.scriptdir(), "src", component, "*.cppm"))
        add_files(path.join(os.scriptdir(), "src", component, "*.cpp"))
    end
target_end()

target("graver-cli-modules")
    set_default(false)
    set_kind("moduleonly")
    add_deps("graver-modules")
    for _, component in ipairs({"files", "batch"}) do
        add_files(path.join(os.scriptdir(), "src", component, "*.cppm"))
        add_files(path.join(os.scriptdir(), "src", component, "*.cpp"))
    end
target_end()

target("graver")
    set_default(false)
    set_kind("binary")
    add_deps("graver-cli-modules")
    add_files(path.join(os.scriptdir(), "src", "graver.cpp"))
target_end()

target("graver-test")
    set_default(false)
    set_kind("binary")
    add_deps("graver-cli-modules")
    add_includedirs(path.join(os.projectdir(), "tests", "internal", "thirdparty"))
    add_files(path.join(os.scriptdir(), "tests", "internal", "*.cpp"))
    add_tests("graver", {group = "graver", run_timeout = 60000})
target_end()

target("graver-cli-test")
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
