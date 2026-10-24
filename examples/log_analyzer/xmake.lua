local output = [[
{"accepted":3,"rejected":1,"warnings":1,"errors":1,"slow":["mixer token=[redacted] clipped=2","upstream timed out"]}
]]

target("carven-example-log-analyzer")
    set_kind("binary")
    add_rules("carven")
    add_files("*.cv")
    add_tests("output", {
        run_timeout = 30000, plain = true,
        pass_outputs = {output, (output:gsub("\n", "\r\n"))},
    })
target_end()

target("carven-example-log-analyzer-tests")
    set_kind("binary")
    add_rules("carven", {tests = true})
    add_files("*.cv")
    add_tests("inline", {run_timeout = 30000})
target_end()

target("carven-example-log-analyzer-checks")
    set_kind("phony")
    add_deps("carven-example-log-analyzer")
    add_tests("unknown", {run_timeout = 30000})
    add_tests("help", {run_timeout = 30000})
    add_tests("record-recovery", {run_timeout = 30000})
    on_test(function (target, opt)
        if opt.name:endswith("/record-recovery") then
            return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
                .main(target:dep("carven-example-log-analyzer"), opt, {
                    args = {"--stdin"}, run_timeout = 10000,
                    stdin_bytes = "WARN 40  token=a  suffix \r\nINFO 1 " .. string.char(255) .. "\nERROR 41 final",
                    stdout_bytes = '{"accepted":2,"rejected":1,"warnings":1,"errors":1,"slow":[" token=[redacted]  suffix ","final"]}\n',
                }, target:scriptdir())
        end
        local help = opt.name:endswith("/help")
        return import("process", {rootdir = path.join(os.projectdir(), "tests", "harness")})
            .main(target:dep("carven-example-log-analyzer"), opt, {
                args = {help and "--help" or "--unknown"}, exit_code = help and 0 or 1,
                stdout_contains = help and {"Usage:"} or nil,
                stderr_contains = not help and {"Usage:", "escaped the program entry"} or nil,
            }, target:scriptdir())
    end)
target_end()
