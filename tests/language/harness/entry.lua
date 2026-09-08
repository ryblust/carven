local function run(target, scenario, payloads)
    local stdout_file = os.tmpfile()
    local stderr_file = os.tmpfile()
    local code = os.execv(target:targetfile(), {"alpha", "beta"}, {
        try = true, timeout = 30000,
        envs = {
            CARVEN_ENTRY_TEST_SCENARIO = scenario,
            CARVEN_ENTRY_TEST_PAYLOADS = payloads,
        },
        stdout = stdout_file, stderr = stderr_file,
    })
    local stdout = os.isfile(stdout_file) and io.readfile(stdout_file) or ""
    local stderr = os.isfile(stderr_file) and io.readfile(stderr_file) or ""
    os.tryrm(stdout_file)
    os.tryrm(stderr_file)
    assert(stderr == "", "language entry produced stderr:\n" .. stderr)
    return code, stdout
end

function main(target, entry)
    if entry == "reporting" then
        local status, output = run(target, "success", "0")
        assert(status == 0 and output == "", "reporting entry failed: " .. tostring(status)
            .. "\n" .. output)
        return
    end
    local query_status, output = run(target, "failure-status", "1")
    local failure_status = tonumber(output)
    assert(query_status == 0 and failure_status and failure_status ~= 0,
        "entry did not supply the host failure status: exit " .. tostring(query_status)
            .. "\nstdout:\n" .. output)
    local scenarios = {
        {name = "success", status = 0, payloads = "1"},
        {name = "throw", status = failure_status, payloads = "1"},
        {name = "recover", status = 0, payloads = "2"},
        {name = "propagate", status = failure_status, payloads = "1"},
    }
    for _, scenario in ipairs(scenarios) do
        local status, stdout = run(target, scenario.name, scenario.payloads)
        assert(status == scenario.status and stdout == "", "entry " .. scenario.name
            .. ": expected exit " .. scenario.status .. ", got " .. tostring(status)
            .. "\n" .. stdout)
    end
end
