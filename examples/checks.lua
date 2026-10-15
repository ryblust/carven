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

local function diagnostics(name, args)
    local root = os.tmpfile() .. ".dir"
    os.mkdir(root)
    local code = os.execv(project.target("carven-example-" .. name):targetfile(), args, {
        try = true, timeout = 10000,
        stdout = path.join(root, "stdout"), stderr = path.join(root, "stderr"),
    })
    local output, errors = io.readfile(path.join(root, "stdout")), io.readfile(path.join(root, "stderr"))
    assert(code ~= 0, name .. " unexpectedly accepted invalid arguments")
    os.rm(root)
    return output:gsub("\r\n", "\n"), errors
end

function main(name)
    if name == "arguments" then
        for _, program in ipairs({"audio-mixer", "log-analyzer"}) do
            assert(run(program, {"--help"}):find("Usage:", 1, true))
            local output, errors = diagnostics(program, {"--unknown"})
            assert(output == "" and errors:find("Usage:", 1, true), program .. " argument error polluted stdout")
        end
        for _, argument in ipairs({"--voice", "--voice=", "--voice=bad", "--music=-1", "--music=1e99"}) do
            local output, errors = diagnostics("audio-mixer", {argument})
            assert(output == "" and errors:find("Usage:", 1, true), argument)
        end
    elseif name == "record-recovery" then
        local input = "WARN 40  token=a  suffix \r\nINFO 1 " .. string.char(255) .. "\nERROR 41 final"
        local output = run("log-analyzer", {"--stdin"}, input)
        assert(output == '{"accepted":2,"rejected":1,"warnings":1,"errors":1,"slow":[" token=[redacted]  suffix ","final"]}\n', output)
    elseif name == "pipeline" then
        local mixed = run("audio-mixer", {"--logs"})
        local report = run("log-analyzer", {"--stdin"}, mixed)
        assert(report == '{"accepted":2,"rejected":0,"warnings":1,"errors":0,"slow":["mixer clipped=2 samples=10"]}\n', report)
    elseif name == "configuration" then
        local mixed = run("audio-mixer", {"--logs", "--voice=0", "--music=0"})
        assert(mixed == "WARN 37 mixer clipped=0 samples=10\nINFO 12 mixer peak=0.00 rms=0.000\n", mixed)
    end
end
