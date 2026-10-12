import("core.project.project")

local function run(name, args, input)
    local input_file = input and os.tmpfile() or nil
    if input_file then io.writefile(input_file, input, {encoding = "binary"}) end
    local output, errors = os.iorunv(project.target("carven-example-" .. name):targetfile(), args, {
        stdin = input_file, timeout = 10000,
    })
    if input_file then os.rm(input_file) end
    assert(errors == "", name .. " emitted diagnostics: " .. errors)
    return output:gsub("\r\n", "\n")
end

function main(name)
    if name == "pipeline" then
        local mixed = run("audio-mixer", {"--logs"})
        local collected = run("stream-collector", {"--stdin", "--records"}, mixed)
        assert(collected == mixed, "TCP collection changed mixer records")
        local report = run("log-analyzer", {"--stdin"}, collected)
        assert(report == '{"accepted":2,"rejected":0,"warnings":1,"errors":0,"slow":["mixer clipped=2 samples=10"]}\n', report)
    elseif name == "configuration" then
        local mixed = run("audio-mixer", {"--logs", "--voice=0", "--music=0"})
        assert(mixed == "WARN 37 mixer clipped=0 samples=10\nINFO 12 mixer peak=0.00 rms=0.000\n", mixed)
    elseif name == "stream-framing" then
        local valid = "INFO 12 peer=\228\184\150\231\149\140\n" .. string.rep("x", 256) .. "\n"
        assert(run("stream-collector", {"--stdin", "--records"}, valid) == valid)
    else
        local input
        if name == "stream-incomplete" then
            input = "INFO 1 incomplete"
        elseif name == "stream-invalid-utf8" then
            input = "INFO 1 " .. string.char(255) .. "\n"
        else
            input = string.rep("x", 257) .. "\n"
        end
        local output = run("stream-collector", {"--stdin"}, input)
        assert(output == "Collected 0 records; rejected feeds: 2; timed out: 1\n", output)
    end
end
