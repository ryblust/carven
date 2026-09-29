function main(target, description)
    local program = path.absolute(target:targetfile(), os.projectdir())
    local stdout_file, stderr_file = os.tmpfile(), os.tmpfile()
    local status, run_error = os.execv(program, {}, {
        try = true, timeout = 30000, stdout = stdout_file, stderr = stderr_file,
    })
    local stdout = io.readfile(stdout_file) or ""
    local stderr = (io.readfile(stderr_file) or ""):gsub("\r\n", "\n")
    os.tryrm(stdout_file)
    os.tryrm(stderr_file)
    local context = description .. " (" .. program .. ")"
        .. "\nstdout:\n" .. stdout .. "\nstderr:\n" .. stderr
    assert(status == 0, "generated tests failed with exit " .. tostring(status)
        .. " (" .. tostring(run_error) .. "): " .. context)
    local passed = stderr:match("carven: tests: (%d+) passed; 0 failed\n$")
    assert(passed and tonumber(passed) > 0,
        "generated runner executed no passing tests or omitted its summary: " .. context)
    return true
end
