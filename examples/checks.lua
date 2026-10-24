local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})
local runenvs = import("private.action.run.runenvs")

function pipeline(target, opt)
    local work = os.tmpfile() .. ".pipeline"
    os.mkdir(work)
    local failures = {}
    local input
    for index, stage in ipairs({
        {name = "audio-mixer", args = {"--logs"}},
        {name = "log-analyzer", args = {"--stdin"}},
    }) do
        local application = target:dep("carven-example-" .. stage.name)
        local addenvs, setenvs = runenvs.make(application)
        local output = path.join(work, index .. ".stdout")
        local errors = path.join(work, index .. ".stderr")
        local code, run_error = os.execv(path.absolute(application:targetfile()), stage.args, {
            try = true, timeout = 10000, curdir = work,
            envs = runenvs.join(addenvs, setenvs),
            stdin = input, stdout = output, stderr = errors,
        })
        local diagnostics = io.readfile(errors, {encoding = "binary"})
        if diagnostics == nil then
            table.insert(failures, stage.name .. ": cannot read stderr " .. errors)
            diagnostics = ""
        end
        if code ~= 0 then
            table.insert(failures, stage.name .. ": expected exit 0, got " .. tostring(code)
                .. (run_error and (" (" .. run_error .. ")") or ""))
        end
        if diagnostics ~= "" then
            table.insert(failures, stage.name .. " emitted diagnostics: " .. diagnostics)
        end
        if not os.isfile(output) then
            table.insert(failures, stage.name .. ": missing stdout")
        end
        if #failures > 0 then break end
        -- Pass the producer's actual bytes to the consumer, including its line endings.
        input = output
    end
    if #failures == 0 then
        local output = io.readfile(input, {encoding = "binary"})
        local expected = '{"accepted":2,"rejected":0,"warnings":1,"errors":0,"slow":["mixer clipped=2 samples=10"]}\n'
        if output == nil then
            table.insert(failures, "cannot read pipeline output " .. input)
        elseif output:gsub("\r\n", "\n") ~= expected then
            output = output:gsub("\r\n", "\n")
            table.insert(failures, report.format_text_difference(output, expected))
        end
    end
    if #failures > 0 then
        table.insert(failures, "failure directory retained at: " .. work)
        opt.errors = table.concat(failures, "\n")
        return false
    end
    os.tryrm(work)
    return true
end
