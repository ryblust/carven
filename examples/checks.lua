local runenvs = import("private.action.run.runenvs")

local function run(target, name, args, input)
    local input_file = input and os.tmpfile() or nil
    if input_file then io.writefile(input_file, input, {encoding = "binary"}) end
    local application = target:dep("carven-example-" .. name)
    local addenvs, setenvs = runenvs.make(application)
    local output, errors = os.iorunv(application:targetfile(), args, {
        stdin = input_file, timeout = 10000, envs = runenvs.join(addenvs, setenvs),
    })
    if input_file then os.rm(input_file) end
    assert(errors == "", name .. " emitted diagnostics: " .. errors)
    return output:gsub("\r\n", "\n")
end

function record_recovery(target)
    local input = "WARN 40  token=a  suffix \r\nINFO 1 " .. string.char(255) .. "\nERROR 41 final"
    local output = run(target, "log-analyzer", {"--stdin"}, input)
    assert(output == '{"accepted":2,"rejected":1,"warnings":1,"errors":1,"slow":[" token=[redacted]  suffix ","final"]}\n', output)
end

function pipeline(target)
    local mixed = run(target, "audio-mixer", {"--logs"})
    local report = run(target, "log-analyzer", {"--stdin"}, mixed)
    assert(report == '{"accepted":2,"rejected":0,"warnings":1,"errors":0,"slow":["mixer clipped=2 samples=10"]}\n', report)
end
