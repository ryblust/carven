function main(target)
    local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})
    local common_prefix = string.rep("x", 200) .. "\n"
    local difference = report.format_text_difference(common_prefix .. "a\0", common_prefix .. "b")
    assert(difference:find("line 2, byte 202 (1-based)", 1, true), difference)
    assert(difference:find("lengths: actual 203, expected 202", 1, true), difference)
    assert(#difference < 300, "text differences must show bounded excerpts")
    assert(report.format_command("tool path", {"one argument", ""}) == [["tool path" "one argument" ""]])
    local program = path.absolute(target:targetfile(), os.projectdir())
    local cases = {
        {name = "success", args = {"--test", "Test framework: success"}, exit = 0,
            contains = {"Tests: 1 passed, 0 failed"}},
        {name = "failure", args = {"--filter", "Test framework failure:*"}, exit = 1,
            contains = {"contracts.cpp:", [[context: "failing input"]], "note: input rejected details",
                [[actual:   "line\n\x00\xff"]], [[expected: "你好\t\"\\"]],
                "lengths: actual 7, expected 9", "first difference at byte 0",
                "actual:   1", "expected: 2", "relation: actual < expected", "actual:   <null>",
                "lengths: actual 2, expected 3", "first difference at index 1",
                "actual element:   2", "expected element: 3",
                "LATER INPUT EXECUTED", "LATER CASE EXECUTED", "Tests: 1 passed, 1 failed"},
            absent = {"UNSAFE CONTINUATION"}},
        {name = "fatal", args = {"--test", "Test framework: fatal premise"}, exit = 73,
            contains = {"actual:   7", "expected: 9", "note: fatal context"},
            absent = {"UNSAFE CONTINUATION"}},
        {name = "selection", args = {"--filter", "Test framework: *", "--exclude", "*success", "--list-tests"}, exit = 0,
            contains = {"Test framework: fatal premise", "Test framework: empty table"},
            absent = {"Test framework: success", "Test framework failure:", "Tests:"}},
        {name = "empty_selection", args = {"--filter", "No such test"}, exit = 1,
            contains = {"No tests matched", [[filter: "No such test"]]}},
        {name = "empty_table", args = {"--test", "Test framework: empty table"}, exit = 1,
            contains = {"empty case table", "Tests: 0 passed, 1 failed"}},
        {name = "no_assertions", args = {"--test", "Test framework: no assertions"}, exit = 1,
            contains = {"contracts.cpp:", "Test executed no assertions", "Tests: 0 passed, 1 failed"}},
        {name = "duplicate", args = {"--duplicate-declaration"}, exit = 1,
            contains = {"contracts.cpp:", [[Duplicate test name: "Duplicate"]], "also declared at"}},
        {name = "invalid_declaration", args = {"--invalid-declaration"}, exit = 1,
            contains = {"contracts.cpp:", "Invalid test declaration"}},
        {name = "unknown_option", args = {"--unknown"}, exit = 2,
            contains = {"Invalid test option: --unknown"}},
        {name = "missing_value", args = {"--test"}, exit = 2,
            contains = {"Missing value for test option: --test"}},
        {name = "option_as_value", args = {"--test", "--list-tests"}, exit = 2,
            contains = {"Missing value for test option: --test"}},
        {name = "empty_value", args = {"--filter", ""}, exit = 2,
            contains = {"Test selection must not be empty"}},
        {name = "repeated_option", args = {"--filter", "*", "--filter", "*"}, exit = 2,
            contains = {"Invalid test option: --filter"}},
    }
    for _, case in ipairs(cases) do
        local stdout_file = os.tmpfile()
        local stderr_file = os.tmpfile()
        local exit_code, run_error = os.execv(program, case.args, {
            try = true, timeout = 10000, stdout = stdout_file, stderr = stderr_file,
        })
        local output = (io.readfile(stdout_file) or "") .. (io.readfile(stderr_file) or "")
        os.tryrm(stdout_file)
        os.tryrm(stderr_file)
        output = output:gsub("\r\n", "\n")
        assert(exit_code == case.exit,
            case.name .. ": unexpected exit " .. tostring(exit_code) .. " " .. tostring(run_error) .. "\n" .. output)
        for _, expected in ipairs(case.contains or {}) do
            assert(output:find(expected, 1, true), case.name .. ": missing " .. expected .. "\n" .. output)
        end
        for _, unexpected in ipairs(case.absent or {}) do
            assert(not output:find(unexpected, 1, true), case.name .. ": unexpected " .. unexpected .. "\n" .. output)
        end
    end
    return true
end
