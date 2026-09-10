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
    local expected_status = 0
    if entry == "throw" or entry == "propagate" then
        local query_status, output = run(target, "failure-status", "1")
        expected_status = tonumber(output)
        assert(query_status == 0 and expected_status and expected_status ~= 0,
            "entry did not supply the host failure status: " .. tostring(query_status) .. "\n" .. output)
    end
    local status, stdout = run(target, entry, entry == "recover" and "2" or "1")
    assert(status == expected_status and stdout == "", "entry " .. entry
        .. ": expected exit " .. expected_status .. ", got " .. tostring(status) .. "\n" .. stdout)
end
