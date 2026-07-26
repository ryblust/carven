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
    local bytes = import("core.base.bytes")
    local pipe = import("core.base.pipe")
    local process = import("core.base.process")
    local scheduler = import("core.base.scheduler")
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

    local function read_pipe(reader)
        local chunks = {}
        local buffer = bytes(4096)
        while true do
            local count, data = reader:read(buffer)
            if count > 0 then
                table.insert(chunks, data:str())
            elseif count == 0 then
                if reader:wait(pipe.EV_READ, -1) ~= pipe.EV_READ then
                    break
                end
            else
                break
            end
        end
        reader:close()
        return table.concat(chunks)
    end

    local failures = {}
    local function run_process(args, step_index)
        local stdout_reader, stdout_writer = pipe.openpair("AB")
        local stderr_reader, stderr_writer = pipe.openpair("AB")
        local child = process.openv(
            path.absolute(target:dep("carven"):targetfile(), os.projectdir()),
            args,
            {curdir = work_dir, stdout = stdout_writer, stderr = stderr_writer}
        )
        stdout_writer:close()
        stderr_writer:close()
        local stdout, stderr = "", ""
        local capture_group = "carven-cli-capture-" .. case_name .. "-" .. step_index
        scheduler.co_group_begin(capture_group, function()
            scheduler.co_start(function() stdout = read_pipe(stdout_reader) end)
            scheduler.co_start(function() stderr = read_pipe(stderr_reader) end)
        end)
        local wait_result, exit_code = child:wait(30000)
        if wait_result == 0 then
            child:kill()
            child:wait(-1)
        end
        scheduler.co_group_wait(capture_group)
        child:close()
        return wait_result, exit_code, normalize_newlines(stdout), normalize_newlines(stderr)
    end

    local function inspect_step(step, step_index)
        local wait_result, exit_code, stdout, stderr = run_process(step.args, step_index)
        opt.stdout = stdout ~= "" and stdout or nil
        opt.stderr = stderr ~= "" and stderr or nil
        local prefix = #((case_spec.steps) or {}) > 0 and ("step " .. step_index .. ": ") or ""
        local expected_exit_code = step.exit_code or 0
        if wait_result == 0 then
            table.insert(failures, prefix .. "carven process exceeded 30 second timeout")
        elseif wait_result ~= 1 then
            table.insert(failures, prefix .. "carven process did not exit normally: " .. tostring(exit_code))
        elseif exit_code ~= expected_exit_code then
            table.insert(failures, prefix .. string.format(
                "exit code mismatch: expected %d, actual %s", expected_exit_code, tostring(exit_code)
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
