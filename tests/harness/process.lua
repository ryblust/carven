local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})
local runenvs = import("private.action.run.runenvs")

local function normalize_newlines(value)
    return value:gsub("\r\n", "\n")
end

local function compare_output(failures, label, actual, expected)
    if actual ~= expected then
        table.insert(failures, label .. " mismatch: " .. report.format_text_difference(actual, expected))
    end
end

local function sorted_lines(value)
    local lines = value:split("\n", {plain = true, strict = true})
    table.sort(lines)
    return table.concat(lines, "\n")
end

local function check_stream(failures, label, actual, exact, contains, ordered, unordered, patterns)
    if exact ~= nil then
        compare_output(failures, label, unordered and sorted_lines(actual) or actual,
            unordered and sorted_lines(exact) or exact)
    elseif not contains and not ordered and not patterns then
        compare_output(failures, label, actual, "")
    end
    for _, expected in ipairs(contains or {}) do
        if not actual:find(expected, 1, true) then
            table.insert(failures, label .. " does not contain: " .. expected)
        end
    end
    for _, pattern in ipairs(patterns or {}) do
        if not actual:find(pattern) then
            table.insert(failures, label .. " does not match pattern: " .. pattern)
        end
    end
    local sequences = ordered and (type(ordered[1]) == "table" and ordered or {ordered}) or {}
    for _, sequence in ipairs(sequences) do
        local offset = 1
        for _, expected in ipairs(sequence) do
            local found = actual:find(expected, offset, true)
            if not found then
                table.insert(failures, label .. " is missing ordered text: " .. expected)
                break
            end
            offset = found + #expected
        end
    end
end

local function check_file_contents(failures, work_dir, prefix, expectations)
    for filename, fragments in table.orderpairs(expectations or {}) do
        local file_path = path.join(work_dir, filename)
        if not os.isfile(file_path) then
            table.insert(failures, prefix .. "missing inspected file: " .. filename)
        else
            local content = normalize_newlines(io.readfile(file_path))
            for _, fragment in ipairs(fragments) do
                if content:find(fragment, 1, true) then
                    table.insert(failures, prefix .. filename .. " unexpectedly contains: " .. fragment)
                end
            end
        end
    end
end

function main(target, opt, case_spec, case_dir)
    local steps = case_spec.steps or {case_spec}
    local work_dir = os.tmpfile() .. ".dir"
    local function case_path(relative)
        return path.normalize(path.absolute(relative, case_dir))
    end
    local function copy_fixture(source, destination)
        local file_path = path.join(work_dir, destination)
        os.mkdir(path.directory(file_path))
        os.cp(case_path(source), file_path)
    end
    if case_spec.project then
        os.cp(case_path(case_spec.project), work_dir)
    else
        os.mkdir(work_dir)
        for _, filename in ipairs(case_spec.inputs or {}) do
            copy_fixture(filename, filename)
        end
        for source, destination in pairs(case_spec.fixtures or {}) do
            copy_fixture(source, destination)
        end
    end

    local failures = {}
    local streams = {stdout = {}, stderr = {}}
    local program = path.absolute(target:targetfile(), os.projectdir())
    local additions, overrides = runenvs.make(target)
    -- Child commands use tools from this build, independently of installed versions.
    additions.PATH = table.join({path.directory(program)}, additions.PATH or {})
    local envs = runenvs.join(additions, overrides)
    for index, step in ipairs(steps) do
        for filename, content in pairs(step.write_files or {}) do
            local file_path = path.join(work_dir, filename)
            os.mkdir(path.directory(file_path))
            io.writefile(file_path, content, {encoding = "binary"})
        end
        local stdout_file = path.join(work_dir, ".stdout-" .. index)
        local stderr_file = path.join(work_dir, ".stderr-" .. index)
        local args = table.clone(step.args)
        for _, source in ipairs(step.absolute_inputs or {}) do
            table.insert(args, (path.join(work_dir, source):gsub("\\", "/")))
        end
        local stdin_file = step.stdin and case_path(step.stdin) or nil
        if step.stdin_bytes then
            stdin_file = path.join(work_dir, ".stdin-" .. index)
            io.writefile(stdin_file, step.stdin_bytes, {encoding = "binary"})
        end
        local step_program = step.program and path.join(work_dir, step.program) or program
        local step_envs = table.clone(envs or {})
        for name, value in pairs(step.envs or {}) do step_envs[name] = value end
        local exit_code, run_error = os.execv(step_program, args, {
            try = true, timeout = step.run_timeout or 30000, curdir = work_dir, envs = step_envs,
            stdout = stdout_file, stderr = stderr_file,
            stdin = stdin_file,
        })
        local stdout = normalize_newlines(os.isfile(stdout_file) and io.readfile(stdout_file, {encoding = "binary"}) or "")
        local stderr = normalize_newlines(os.isfile(stderr_file) and io.readfile(stderr_file, {encoding = "binary"}) or "")
        local prefix = "step " .. index .. " (" .. report.format_command(step_program, args) .. "): "
        for stream, value in pairs({stdout = stdout, stderr = stderr}) do
            if value ~= "" then
                table.insert(streams[stream], prefix .. value)
            end
        end
        local expected_code = step.exit_code or 0
        if exit_code ~= expected_code then
            local context = run_error and (" (" .. run_error .. ")") or ""
            table.insert(failures, prefix .. string.format(
                "expected exit %s, got %s%s", expected_code, tostring(exit_code), context))
        end
        local function expected_output(filename, bytes)
            if bytes ~= nil then return normalize_newlines(bytes) end
            return filename and normalize_newlines(io.readfile(case_path(filename), {encoding = "binary"})) or nil
        end
        check_stream(failures, prefix .. "stdout", stdout, expected_output(step.stdout, step.stdout_bytes),
            step.stdout_contains, step.stdout_ordered, step.stdout_unordered, step.stdout_patterns)
        check_stream(failures, prefix .. "stderr", stderr, expected_output(step.stderr, step.stderr_bytes),
            step.stderr_contains, step.stderr_ordered, nil, step.stderr_patterns)
        for stream, value in pairs({stdout = stdout, stderr = stderr}) do
            for _, fragment in ipairs(step[stream .. "_not_contains"] or {}) do
                if value:find(fragment, 1, true) then
                    table.insert(failures, prefix .. stream .. " unexpectedly contains: " .. fragment)
                end
            end
        end
        for _, filename in ipairs(step.output_files or {}) do
            if #os.files(path.join(work_dir, filename)) == 0 then
                table.insert(failures, prefix .. "missing output file: " .. filename)
            end
        end
        for _, filename in ipairs(step.absent_files or {}) do
            if #os.match(path.join(work_dir, filename), "a") > 0 then
                table.insert(failures, prefix .. "unexpected output file: " .. filename)
            end
        end
        check_file_contents(failures, work_dir, prefix, step.file_not_contains)
        for filename, expected in pairs(step.file_contents or {}) do
            local actual = io.readfile(path.join(work_dir, filename), {encoding = "binary"})
            compare_output(failures, prefix .. filename, actual or "", expected)
        end
    end
    opt.stdout = #streams.stdout > 0 and table.concat(streams.stdout, "\n") or nil
    opt.stderr = #streams.stderr > 0 and table.concat(streams.stderr, "\n") or nil
    if #failures > 0 then
        table.insert(failures, "failure directory retained at: " .. work_dir)
        opt.errors = table.concat(failures, "\n")
        print("Test " .. opt.name .. " failed:\n" .. opt.errors)
        return false
    end
    os.tryrm(work_dir)
    return true
end
