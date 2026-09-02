local function normalize_newlines(value)
    return value:gsub("\r\n", "\n")
end

local function sorted_keys(values)
    local keys = {}
    for key in pairs(values or {}) do
        table.insert(keys, key)
    end
    table.sort(keys)
    return keys
end

local function case_name_from_test_name(fullname)
    return fullname:match("^[^/]+/(.+)$") or fullname
end

local function compare_output(failures, stream, actual, expected)
    if actual ~= expected then
        table.insert(
            failures,
            string.format(
                "%s mismatch\nexpected:\n%s\nactual:\n%s",
                stream,
                expected == "" and "<empty>" or expected,
                actual == "" and "<empty>" or actual
            )
        )
    end
end

function main(target, opt, case_spec)
    local case_name = case_name_from_test_name(opt.name)
    local case_dir = path.join(os.projectdir(), "tests", "cli", case_name)
    if not case_spec then
        raise("unknown CLI test case: " .. case_name)
    end
    local work_dir = os.tmpfile("carven-cli-" .. case_name) .. ".dir"
    os.tryrm(work_dir)
    local function case_path(relative)
        return path.normalize(path.absolute(relative, case_dir))
    end
    if case_spec.project then
        os.cp(case_path(case_spec.project), work_dir)
    else
        os.mkdir(work_dir)
        for _, filename in ipairs(case_spec.inputs or {}) do
            local destination = path.join(work_dir, filename)
            os.mkdir(path.directory(destination))
            os.cp(path.join(case_dir, filename), destination)
        end
        for source_file, destination in pairs(case_spec.fixtures or {}) do
            local file_path = path.join(work_dir, destination)
            os.mkdir(path.directory(file_path))
            os.cp(case_path(source_file), file_path)
        end
    end

    local failures = {}
    local function run_process(args, step_index)
        local stdout_file = path.join(work_dir, ".stdout-" .. step_index)
        local stderr_file = path.join(work_dir, ".stderr-" .. step_index)
        local exit_code, run_error = os.execv(
            path.absolute(target:dep("carven"):targetfile(), os.projectdir()),
            args,
            {
                try = true,
                timeout = 30000,
                curdir = work_dir,
                stdout = stdout_file,
                stderr = stderr_file,
            }
        )
        local stdout = os.isfile(stdout_file) and io.readfile(stdout_file) or ""
        local stderr = os.isfile(stderr_file) and io.readfile(stderr_file) or ""
        os.tryrm(stdout_file)
        os.tryrm(stderr_file)
        return exit_code, run_error, normalize_newlines(stdout), normalize_newlines(stderr)
    end

    local function inspect_step(step, step_index)
        local exit_code, run_error, stdout, stderr = run_process(step.args, step_index)
        opt.stdout = stdout ~= "" and stdout or nil
        opt.stderr = stderr ~= "" and stderr or nil
        local prefix = #((case_spec.steps) or {}) > 0 and ("step " .. step_index .. ": ") or ""
        local expected_exit_code = step.exit_code or 0
        if exit_code ~= expected_exit_code then
            local error_context = run_error and (" (" .. run_error .. ")") or ""
            table.insert(failures, prefix .. string.format(
                "exit code mismatch: expected %d, actual %s%s",
                expected_exit_code,
                tostring(exit_code),
                error_context
            ))
        end

        if step.stdout then
            compare_output(failures, prefix .. "stdout", stdout,
                normalize_newlines(io.readfile(case_path(step.stdout))))
        elseif not step.stdout_contains and not step.stdout_ordered then
            compare_output(failures, prefix .. "stdout", stdout, "")
        end
        if step.stderr then
            compare_output(failures, prefix .. "stderr", stderr,
                normalize_newlines(io.readfile(case_path(step.stderr))))
        elseif not step.stderr_contains then
            compare_output(failures, prefix .. "stderr", stderr, "")
        end
        for _, expected in ipairs(step.stdout_contains or {}) do
            if not stdout:find(expected, 1, true) then
                table.insert(failures, prefix .. "stdout does not contain: " .. expected)
            end
        end
        local prior_position = 0
        for _, expected in ipairs(step.stdout_ordered or {}) do
            local position = stdout:find(expected, prior_position + 1, true)
            if not position then
                table.insert(failures, prefix .. "stdout is missing ordered text: " .. expected)
                break
            end
            prior_position = position
        end
        for _, expected in ipairs(step.stderr_contains or {}) do
            if not stderr:find(expected, 1, true) then
                table.insert(failures, prefix .. "stderr does not contain: " .. expected)
            end
        end
        for _, filename in ipairs(step.output_files or {}) do
            if not os.isfile(path.join(work_dir, filename)) then
                table.insert(failures, prefix .. "missing output file: " .. filename)
            end
        end
        for _, pattern in ipairs(step.output_globs or {}) do
            if #os.files(path.join(work_dir, pattern)) == 0 then
                table.insert(failures, prefix .. "missing output matching: " .. pattern)
            end
        end
        for pattern, count in pairs(step.output_glob_counts or {}) do
            local actual = #os.files(path.join(work_dir, pattern))
            if actual ~= count then
                table.insert(failures, prefix .. string.format(
                    "output count mismatch for %s: expected %d, actual %d",
                    pattern,
                    count,
                    actual
                ))
            end
        end
        for _, filename in ipairs(step.absent_files or {}) do
            if os.exists(path.join(work_dir, filename)) then
                table.insert(failures, prefix .. "unexpected output file: " .. filename)
            end
        end
        for _, pattern in ipairs(step.absent_globs or {}) do
            if #os.files(path.join(work_dir, pattern)) ~= 0 then
                table.insert(failures, prefix .. "unexpected output matching: " .. pattern)
            end
        end
        for _, filename in ipairs(sorted_keys(step.file_contains)) do
            local file_path = path.join(work_dir, filename)
            if not os.isfile(file_path) then
                table.insert(failures, prefix .. "missing inspected file: " .. filename)
            else
                local content = normalize_newlines(io.readfile(file_path))
                for _, expected in ipairs(step.file_contains[filename]) do
                    if not content:find(expected, 1, true) then
                        table.insert(failures,
                            prefix .. filename .. " does not contain: " .. expected)
                    end
                end
            end
        end
        for _, filename in ipairs(sorted_keys(step.file_excludes)) do
            local file_path = path.join(work_dir, filename)
            if not os.isfile(file_path) then
                table.insert(failures, prefix .. "missing inspected file: " .. filename)
            else
                local content = normalize_newlines(io.readfile(file_path))
                for _, unexpected in ipairs(step.file_excludes[filename]) do
                    if content:find(unexpected, 1, true) then
                        table.insert(failures,
                            prefix .. filename .. " unexpectedly contains: " .. unexpected)
                    end
                end
            end
        end
        for _, filename in ipairs(sorted_keys(step.file_snapshots)) do
            local file_path = path.join(work_dir, filename)
            if not os.isfile(file_path) then
                table.insert(failures, prefix .. "missing snapshot file: " .. filename)
            else
                compare_output(
                    failures,
                    prefix .. filename,
                    normalize_newlines(io.readfile(file_path)),
                    normalize_newlines(io.readfile(case_path(step.file_snapshots[filename])))
                )
            end
        end
    end

    for index, step in ipairs(case_spec.steps or {case_spec}) do
        inspect_step(step, index)
    end

    if #failures > 0 then
        table.insert(failures, "failure directory retained at: " .. work_dir)
        opt.errors = table.concat(failures, "\n")
        print("CLI test " .. case_name .. " failed:\n" .. opt.errors)
        return false
    end
    os.tryrm(work_dir)
    return true
end
