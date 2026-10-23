local output = [[
Mixed 10 samples; clipped: 2
Peak: 1.00; RMS: 0.652
[
    0.5,
    -0.5,
    1,
    -1,
    0.25,
    0.5,
    -0.5,
    0.25,
    0.75,
    -0.75,
]
]]
local silent = "WARN 37 mixer clipped=0 samples=10\nINFO 12 mixer peak=0.00 rms=0.000\n"

target("carven-example-audio-mixer")
    set_kind("binary")
    add_rules("carven")
    add_files("*.cv")
    add_tests("output", {
        run_timeout = 30000, plain = true,
        pass_outputs = {output, (output:gsub("\n", "\r\n"))},
    })
    add_tests("configuration", {
        run_timeout = 30000, plain = true,
        runargs = {"--logs", "--voice=0", "--music=0"},
        pass_outputs = {silent, (silent:gsub("\n", "\r\n"))},
    })
    add_tests("help", {
        run_timeout = 30000, runargs = {"--help"}, pass_outputs = {".*Usage:.*"},
    })
    after_test(function (target, opt)
        if opt.name:endswith("/help") then
            assert(not opt.stderr or opt.stderr == "", opt.stderr)
        end
    end)
target_end()

target("carven-example-audio-mixer-tests")
    set_kind("binary")
    add_rules("carven", {tests = true})
    add_files("*.cv")
    add_tests("inline", {run_timeout = 30000})
target_end()

local arguments = {
    unknown = "--unknown",
    ["invalid-voice"] = "--voice=bad",
    ["negative-music"] = "--music=-1",
}

target("carven-example-audio-mixer-checks")
    set_kind("phony")
    add_deps("carven-example-audio-mixer")
    for _, name in ipairs(table.orderkeys(arguments)) do
        add_tests(name, {run_timeout = 30000})
    end
    on_test(function (target, opt)
        local name = opt.name:match("([^/]+)$")
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target:dep("carven-example-audio-mixer"), opt, {
                args = {arguments[name]}, exit_code = 1,
                stderr_contains = {"Usage:", "escaped the program entry"},
            }, target:scriptdir())
    end)
target_end()
