function main(target, arguments, expected_code, description, expected_stdout)
    local stdout_file = os.tmpfile()
    local stderr_file = os.tmpfile()
    local code = os.execv(target:targetfile(), arguments, {
        try = true,
        timeout = 30000,
        stdout = stdout_file,
        stderr = stderr_file,
    })
    local stdout = os.isfile(stdout_file) and io.readfile(stdout_file, {encoding = "binary"}) or ""
    local stderr = os.isfile(stderr_file) and io.readfile(stderr_file, {encoding = "binary"}) or ""
    os.tryrm(stdout_file)
    os.tryrm(stderr_file)
    assert(code == expected_code, description .. ": expected exit " .. expected_code
        .. ", got " .. tostring(code) .. "\nstdout:\n" .. stdout .. "\nstderr:\n" .. stderr)
    if expected_stdout ~= nil then
        assert(stdout == expected_stdout, description .. ": unexpected output before termination:\n" .. stdout)
    end
end
