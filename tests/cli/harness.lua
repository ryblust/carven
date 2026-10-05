local report = import("report", {rootdir = path.join(os.projectdir(), "tests", "harness")})

local function normalize_newlines(value)
    return value:gsub("\r\n", "\n")
end

local function remove_prefix(value, prefix)
    if not prefix then return value end
    local pattern = prefix:gsub("(%W)", "%%%1")
    return value:gsub(pattern, "")
end

local step_fields = {
    args = true, exit_code = true, run_timeout = true, installed_toolchain = true,
    installed_inputs = true, absolute_inputs = true,
    stdout = true, stdout_contains = true, stdout_ordered = true,
    stdout_unordered = true, stdout_not_contains = true,
    stderr = true, stderr_contains = true, stderr_ordered = true,
    stderr_not_contains = true, output_files = true, absent_files = true,
    file_contains = true, file_not_contains = true,
}
local case_fields = {inputs = true, fixtures = true, project = true, steps = true}
for field in pairs(step_fields) do case_fields[field] = true end

local function check_fields(value, allowed, label)
    for field in pairs(value) do
        assert(allowed[field], label .. " has unknown field: " .. tostring(field))
    end
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

local function check_stream(failures, label, actual, exact, contains, ordered, unordered)
    if exact ~= nil then
        compare_output(failures, label, unordered and sorted_lines(actual) or actual,
            unordered and sorted_lines(exact) or exact)
    elseif not contains and not ordered then
        compare_output(failures, label, actual, "")
    end
    for _, expected in ipairs(contains or {}) do
        if not actual:find(expected, 1, true) then
            table.insert(failures, label .. " does not contain: " .. expected)
        end
    end
    local offset = 1
    for _, expected in ipairs(ordered or {}) do
        local found = actual:find(expected, offset, true)
        if not found then
            table.insert(failures, label .. " is missing ordered text: " .. expected)
            break
        end
        offset = found + #expected
    end
end

local function check_file_contents(failures, work_dir, prefix, expectations, must_contain)
    for filename, fragments in table.orderpairs(expectations or {}) do
        local file_path = path.join(work_dir, filename)
        if not os.isfile(file_path) then
            table.insert(failures, prefix .. "missing inspected file: " .. filename)
        else
            local content = normalize_newlines(io.readfile(file_path))
            for _, fragment in ipairs(fragments) do
                local found = content:find(fragment, 1, true) ~= nil
                if found ~= must_contain then
                    local message = must_contain and " does not contain: " or " unexpectedly contains: "
                    table.insert(failures, prefix .. filename .. message .. fragment)
                end
            end
        end
    end
end

function main(target, opt, case_specs, execution)
    local case_name = opt.name:match("^[^/]+/(.+)$") or opt.name
    local case_spec = case_specs[case_name]
    assert(case_spec, "unknown CLI test case: " .. case_name)
    check_fields(case_spec, case_fields, "CLI case " .. case_name)
    assert(not case_spec.project or (not case_spec.inputs and not case_spec.fixtures),
        "CLI case project overrides its inputs or fixtures: " .. case_name)
    if case_spec.steps then
        for field in pairs(step_fields) do
            assert(case_spec[field] == nil,
                "CLI case " .. case_name .. " ignores top-level step field: " .. field)
        end
    end
    local steps = case_spec.steps or {case_spec}
    assert(#steps > 0, "CLI test has no execution steps: " .. case_name)
    for index, step in ipairs(steps) do
        local label = case_name .. " step " .. index
        if case_spec.steps then check_fields(step, step_fields, label) end
        assert(type(step.args) == "table", label .. " has no argument list")
        assert(not step.stdout_unordered or step.stdout,
            label .. " requests unordered output without an exact stdout fixture")
        assert(not step.installed_inputs or step.installed_toolchain,
            label .. " ignores installed inputs without an installed toolchain")
    end
    local case_dir = execution and execution.case_dir
        or path.join(os.projectdir(), "tests", "cli", case_name)
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
    local program = path.absolute(execution and target:targetfile()
        or target:dep("carven"):targetfile(), os.projectdir())
    for index, step in ipairs(steps) do
        local stdout_file = path.join(work_dir, ".stdout-" .. index)
        local stderr_file = path.join(work_dir, ".stderr-" .. index)
        local step_program = program
        local args = table.clone(step.args)
        if step.installed_toolchain then
            local prefix = path.join(work_dir, "installation", "crafts", "toolchain with spaces")
            step_program = path.join(prefix, "bin", path.filename(program))
            os.mkdir(path.directory(step_program))
            os.cp(program, step_program)
            os.cp(path.join(os.projectdir(), "crafts", "carven"), path.join(prefix, "crafts", "carven"))
            for _, source in ipairs(step.installed_inputs or {}) do
                table.insert(args, (path.join(prefix, "crafts", source):gsub("\\", "/")))
            end
        end
        for _, source in ipairs(step.absolute_inputs or {}) do
            table.insert(args, (path.join(work_dir, source):gsub("\\", "/")))
        end
        local exit_code, run_error = os.execv(step_program, args, {
            try = true, timeout = step.run_timeout or 30000, curdir = work_dir,
            stdout = stdout_file, stderr = stderr_file,
        })
        local stdout = normalize_newlines(os.isfile(stdout_file) and io.readfile(stdout_file) or "")
        local stderr = normalize_newlines(os.isfile(stderr_file) and io.readfile(stderr_file) or "")
        local prefix = "step " .. index .. " (" .. report.format_command(step_program, args) .. "): "
        for stream, value in pairs({stdout = stdout, stderr = stderr}) do
            if value ~= "" then
                table.insert(streams[stream], prefix .. value)
            end
        end
        -- Prebuilt fixtures retain their source and module suffixes under one known root.
        if execution then
            stderr = remove_prefix(stderr, execution.source_prefix)
            stderr = remove_prefix(stderr, execution.module_prefix)
        end
        local expected_exit_code = step.exit_code or 0
        if exit_code ~= expected_exit_code then
            local context = run_error and (" (" .. run_error .. ")") or ""
            table.insert(failures, prefix .. string.format(
                "exit code mismatch: expected %d, actual %s%s",
                expected_exit_code, tostring(exit_code), context))
        end
        local function expected_output(filename)
            return filename and normalize_newlines(io.readfile(case_path(filename))) or nil
        end
        check_stream(failures, prefix .. "stdout", stdout, expected_output(step.stdout),
            step.stdout_contains, step.stdout_ordered, step.stdout_unordered)
        check_stream(failures, prefix .. "stderr", stderr, expected_output(step.stderr),
            step.stderr_contains, step.stderr_ordered)
        for stream, value in pairs({stdout = stdout, stderr = stderr}) do
            for _, fragment in ipairs(step[stream .. "_not_contains"] or {}) do
                if value:find(fragment, 1, true) then
                    table.insert(failures, prefix .. stream .. " unexpectedly contains: " .. fragment)
                end
            end
        end
        for _, filename in ipairs(step.output_files or {}) do
            if not os.isfile(path.join(work_dir, filename)) then
                table.insert(failures, prefix .. "missing output file: " .. filename)
            end
        end
        for _, filename in ipairs(step.absent_files or {}) do
            if os.exists(path.join(work_dir, filename)) then
                table.insert(failures, prefix .. "unexpected output file: " .. filename)
            end
        end
        check_file_contents(failures, work_dir, prefix, step.file_contains, true)
        check_file_contents(failures, work_dir, prefix, step.file_not_contains, false)
    end
    opt.stdout = #streams.stdout > 0 and table.concat(streams.stdout, "\n") or nil
    opt.stderr = #streams.stderr > 0 and table.concat(streams.stderr, "\n") or nil
    if #failures > 0 then
        table.insert(failures, "failure directory retained at: " .. work_dir)
        opt.errors = table.concat(failures, "\n")
        print("CLI test " .. case_name .. " failed:\n" .. opt.errors)
        return false
    end
    os.tryrm(work_dir)
    return true
end
