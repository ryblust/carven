local output = [[
{"accepted":3,"rejected":1,"warnings":1,"errors":1,"slow":["mixer token=[redacted] clipped=2","upstream timed out"]}
]]

target("carven-example-log-analyzer")
    set_kind("binary")
    add_rules("@carven/carven")
    add_files("*.cv")
    add_tests("output", {
        run_timeout = 30000, plain = true,
        pass_outputs = {output, (output:gsub("\n", "\r\n"))},
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

target("carven-example-log-analyzer-checks")
    set_kind("phony")
    add_deps("carven-example-log-analyzer")
    add_tests("unknown", {run_timeout = 30000})
    add_tests("record-recovery", {run_timeout = 30000})
    on_test(function (target, opt)
        if opt.name:endswith("/record-recovery") then
            import("checks", {rootdir = path.join(os.projectdir(), "examples")})
                .record_recovery(target)
            return true
        end
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target:dep("carven-example-log-analyzer"), opt, {
                args = {"--unknown"}, exit_code = 1,
                stderr_contains = {"Usage:", "escaped the program entry"},
            }, target:scriptdir())
    end)
target_end()
